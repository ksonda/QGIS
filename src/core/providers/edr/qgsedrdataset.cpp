/***************************************************************************
    qgsedrdataset.cpp
    ---------------------
    begin                : September 2026
    copyright            : (C) 2026 by Kyle Onda
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsedrdataset.h"

#include <cmath>
#include <limits>

#include "qgscoordinatetransform.h"
#include "qgsedrclient.h"
#include "qgsgeometry.h"
#include "qgsjsonutils.h"
#include "qgswkbtypes.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QSet>
#include <QString>

using namespace Qt::StringLiterals;

QgsEdrDataset qgsEdrLoadDataset( const QgsEdrQuery &q, QgsFeedback *feedback, bool refresh )
{
  QgsEdrClient client( q, feedback, refresh );
  QgsEdrCoverageJsonReader reader( [&client]( const QUrl &url ) { return client.get( url ); }, q.limits, feedback );
  QgsEdrDataset dataset;
  qint64 cells = 0;
  for ( const QgsEdrDocument &doc : client.pages( q.request() ) )
  {
    const QString type = doc.object.value( u"type"_s ).toString();
    if ( type == "Coverage"_L1 || type == "CoverageCollection"_L1 )
    {
      for ( QgsEdrCoverage c : reader.read( doc ) )
      {
        if ( !q.coverage.isEmpty() && q.coverage != c.id )
          continue;
        cells += c.cellCount;
        dataset.metadata.append( c.metadata );
        dataset.coverages.append( std::move( c ) );
      }
    }
    else if ( type == "FeatureCollection"_L1 || type == "Feature"_L1 )
    {
      if ( type == "FeatureCollection"_L1 && !doc.object.value( u"features"_s ).isArray() )
        throw QgsEdrError( QObject::tr( "FeatureCollection has no features array" ) );
      cells += type == "Feature"_L1 ? 1 : doc.object.value( u"features"_s ).toArray().size();
      dataset.geojson.append( doc );
    }
    else
      throw QgsEdrError( QObject::tr( "This resource is metadata, not a layer. Select a data query, item or location" ) );
    if ( cells > q.limits.maximumCells )
      throw QgsEdrError( QObject::tr( "Combined result exceeds maxCells; results have not been truncated" ) );
  }
  if ( !q.coverage.isEmpty() && dataset.coverages.isEmpty() )
    throw QgsEdrError( QObject::tr( "Coverage identifier was not found: %1" ).arg( q.coverage ) );
  return dataset;
}

QgsCoordinateReferenceSystem qgsEdrCoverageCrs( const QgsEdrCoverage &c )
{
  for ( const QJsonValue &r : c.referencing )
  {
    const QJsonObject ref = r.toObject();
    const QJsonArray coords = ref.value( u"coordinates"_s ).toArray();
    if ( !coords.contains( u"x"_s ) || !coords.contains( u"y"_s ) )
      continue;
    const QJsonObject system = ref.value( u"system"_s ).toObject();
    const QString id = system.value( u"id"_s ).toString();
    QgsCoordinateReferenceSystem crs;
    if ( id.endsWith( "/CRS84"_L1 ) || id == "OGC:CRS84"_L1 )
      crs = QgsCoordinateReferenceSystem( u"OGC:CRS84"_s );
    else
      crs = QgsCoordinateReferenceSystem( id );
    if ( !crs.isValid() )
      throw QgsEdrError( QObject::tr( "QGIS cannot resolve the horizontal CRS: %1" ).arg( id ) );
    return crs;
  }
  throw QgsEdrError( QObject::tr( "Coverage has no horizontal x/y CRS reference" ) );
}

static double coordinate( const QJsonValue &v )
{
  if ( !v.isDouble() || !std::isfinite( v.toDouble() ) )
    throw QgsEdrError( QObject::tr( "Spatial coordinates must be finite numbers" ) );
  return v.toDouble();
}

static QgsGeometry geometryForCell( const QgsEdrCoverage &c, const QMap<QString, int> &index, const QJsonObject &coords )
{
  for ( auto a = c.axes.begin(); a != c.axes.end(); ++a )
  {
    if ( a.value().dataType != "polygon"_L1 )
      continue;
    if ( a.value().coordinates != QStringList { u"x"_s, u"y"_s } )
      throw QgsEdrError( QObject::tr( "Polygon axis must use x,y coordinates" ) );
    const QJsonArray rings = a.value().values.at( index[a.key()] ).toArray();
    if ( rings.isEmpty() )
      throw QgsEdrError( QObject::tr( "Polygon has no rings" ) );
    QgsPolygonXY polygon;
    for ( const QJsonValue &ringValue : rings )
    {
      QgsPolylineXY ring;
      for ( const QJsonValue &vertex : ringValue.toArray() )
      {
        const QJsonArray pair = vertex.toArray();
        if ( pair.size() != 2 )
          throw QgsEdrError( QObject::tr( "Polygon vertex must have two coordinates" ) );
        ring.append( QgsPointXY( coordinate( pair[0] ), coordinate( pair[1] ) ) );
      }
      if ( ring.size() < 4 || ring.first() != ring.last() )
        throw QgsEdrError( QObject::tr( "Polygon rings must be closed and contain at least four vertices" ) );
      polygon.append( ring );
    }
    return QgsGeometry::fromPolygonXY( polygon );
  }
  if ( !coords.contains( u"x"_s ) || !coords.contains( u"y"_s ) )
    throw QgsEdrError( QObject::tr( "Domain cannot be mapped: no horizontal x,y coordinates" ) );
  return QgsGeometry::fromPointXY( QgsPointXY( coordinate( coords.value( u"x"_s ) ), coordinate( coords.value( u"y"_s ) ) ) );
}

static QString categoryLabel( const QJsonObject &parameter, const QJsonValue &value )
{
  if ( value.isNull() )
    return {};
  const QJsonObject encoding = parameter.value( u"categoryEncoding"_s ).toObject();
  QString id;
  for ( auto i = encoding.begin(); i != encoding.end(); ++i )
  {
    const QJsonArray codes = i.value().isArray() ? i.value().toArray() : QJsonArray { i.value() };
    if ( codes.contains( value ) )
    {
      id = i.key();
      break;
    }
  }
  for ( const QJsonValue &c : parameter.value( u"observedProperty"_s ).toObject().value( u"categories"_s ).toArray() )
    if ( c.toObject().value( u"id"_s ).toString() == id )
      return qgsEdrTextLabel( c.toObject().value( u"label"_s ) );
  return id;
}

static QVariant fieldValue( const QJsonValue &v, QMetaType::Type type )
{
  if ( v.isNull() || v.isUndefined() )
    return QVariant( QMetaType( type ) );
  if ( type == QMetaType::QString )
    return qgsEdrCompactJson( v );
  if ( type == QMetaType::Double )
    return v.isDouble() ? QVariant( v.toDouble() ) : QVariant( QMetaType( QMetaType::Double ) );
  if ( type == QMetaType::Bool )
    return v.toBool();
  return v.toVariant();
}

QgsEdrVectorData qgsEdrVectorData( const QgsEdrDataset &dataset, const QgsEdrQuery &q, const QgsCoordinateTransformContext &context )
{
  QgsEdrVectorData data;
  data.metadata = dataset.metadata;
  data.fields.append( QgsField( u"_edr_coverage"_s, QMetaType::QString ) );
  data.fields.append( QgsField( u"_edr_domain"_s, QMetaType::QString ) );
  data.fields.append( QgsField( u"_edr_time"_s, QMetaType::QString ) );
  data.fields.append( QgsField( u"_edr_datetime"_s, QMetaType::QDateTime ) );
  data.fields.append( QgsField( u"_edr_z"_s, QMetaType::Double ) );
  data.fields.append( QgsField( u"_edr_axes"_s, QMetaType::QString ) );
  QMap<QString, QMetaType::Type> types;
  QMap<QString, QJsonObject> parameters;
  QSet<QString> categories;
  auto mergeType = [&types]( const QString &name, QMetaType::Type type ) {
    if ( !types.contains( name ) )
      types.insert( name, type );
    else if ( types[name] != type )
      types[name] = QMetaType::QString;
  };
  for ( const QgsEdrCoverage &c : dataset.coverages )
    for ( auto r = c.ranges.begin(); r != c.ranges.end(); ++r )
    {
      mergeType( r.key(), r.value().dataType == "string"_L1 ? QMetaType::QString : QMetaType::Double );
      const QJsonObject parameter = c.parameters.value( r.key() ).toObject();
      parameters.insert( r.key(), parameter );
      if ( qgsEdrIsCategoricalParameter( parameter ) && parameter.contains( u"categoryEncoding"_s ) )
        categories.insert( r.key() );
    }
  QJsonArray features;
  for ( const QgsEdrDocument &doc : dataset.geojson )
  {
    const QJsonArray list = doc.object.value( u"type"_s ) == "Feature"_L1 ? QJsonArray { doc.object } : doc.object.value( u"features"_s ).toArray();
    for ( const QJsonValue &f : list )
    {
      if ( !f.isObject() || f.toObject().value( u"type"_s ) != "Feature"_L1 )
        throw QgsEdrError( QObject::tr( "Invalid GeoJSON feature" ) );
      features.append( f );
      const QJsonObject properties = f.toObject().value( u"properties"_s ).toObject();
      for ( auto p = properties.begin(); p != properties.end(); ++p )
      {
        if ( p.value().isNull() )
          continue;
        mergeType( p.key(), p.value().isDouble() ? QMetaType::Double : p.value().isBool() ? QMetaType::Bool : QMetaType::QString );
      }
    }
  }
  // Include fields whose only values are null.
  for ( const QJsonValue &f : std::as_const( features ) )
    for ( const QString &key : f.toObject().value( u"properties"_s ).toObject().keys() )
      if ( !types.contains( key ) )
        types.insert( key, QMetaType::QString );
  QMap<QString, int> fields, labels;
  auto addField = [&data]( QString name, QMetaType::Type type, const QString &comment ) {
    while ( data.fields.indexOf( name ) >= 0 )
      name = u"parameter_"_s + name;
    int index = data.fields.size();
    QgsField f( name, type );
    f.setComment( comment );
    data.fields.append( f );
    return index;
  };
  for ( auto t = types.begin(); t != types.end(); ++t )
  {
    fields.insert( t.key(), addField( t.key(), t.value(), qgsEdrCompactJson( parameters.value( t.key() ) ) ) );
  }
  for ( const QString &key : std::as_const( categories ) )
    labels.insert( key, addField( key + u"_label"_s, QMetaType::QString, u"CoverageJSON category label"_s ) );
  if ( !q.targetCrs.isEmpty() )
  {
    data.crs = QgsCoordinateReferenceSystem( q.targetCrs );
    if ( !data.crs.isValid() )
      throw QgsEdrError( QObject::tr( "Invalid targetCrs" ) );
  }
  auto append = [&]( QgsFeature f, const QgsCoordinateReferenceSystem &source ) {
    const Qgis::GeometryType gtype = f.geometry().type();
    if ( !q.geometry.isEmpty() && !f.geometry().isNull() && gtype != ( q.geometry == "polygon"_L1 ? Qgis::GeometryType::Polygon : Qgis::GeometryType::Point ) )
      return;
    if ( !data.crs.isValid() )
      data.crs = source;
    if ( !f.geometry().isNull() && source != data.crs )
    {
      try
      {
        QgsGeometry geom = f.geometry();
        QgsCoordinateTransform transform( source, data.crs, context );
        geom.transform( transform );
        f.setGeometry( geom );
      }
      catch ( const QgsCsException &e )
      {
        throw QgsEdrError( QObject::tr( "CRS transformation failed: %1" ).arg( e.what() ) );
      }
    }
    if ( !f.geometry().isNull() )
    {
      const Qgis::WkbType type = QgsWkbTypes::singleType( f.geometry().wkbType() );
      if ( data.wkb == Qgis::WkbType::Unknown )
        data.wkb = type;
      if ( QgsWkbTypes::geometryType( type ) != QgsWkbTypes::geometryType( data.wkb ) )
        throw QgsEdrError( QObject::tr( "Mixed geometry families require separate layers; set geometry=point or geometry=polygon" ) );
      if ( f.geometry().isMultipart() )
        data.wkb = QgsWkbTypes::multiType( data.wkb );
      if ( QgsWkbTypes::hasZ( type ) )
        data.wkb = QgsWkbTypes::addZ( data.wkb );
      if ( QgsWkbTypes::hasM( type ) )
        data.wkb = QgsWkbTypes::addM( data.wkb );
    }
    f.setId( data.features.size() + 1 );
    data.features.append( std::move( f ) );
  };
  for ( const QgsEdrCoverage &c : dataset.coverages )
  {
    const QgsCoordinateReferenceSystem crs = qgsEdrCoverageCrs( c );
    bool gregorian = true;
    for ( const QJsonValue &ref : c.referencing )
    {
      const QJsonObject system = ref.toObject().value( u"system"_s ).toObject();
      if ( system.value( u"type"_s ) == "TemporalRS"_L1 )
        gregorian = QStringList { u"Gregorian"_s, u"gregorian"_s, u"proleptic_gregorian"_s, u""_s }.contains( system.value( u"calendar"_s ).toString() );
    }
    c.eachCell( [&]( const QMap<QString, int> &index, const QJsonObject &coords ) {
      QgsFeature f( data.fields );
      f.setGeometry( geometryForCell( c, index, coords ) );
      f.setAttribute( 0, c.id );
      f.setAttribute( 1, c.domainType );
      f.setAttribute( 2, qgsEdrCompactJson( coords.value( u"t"_s ) ) );
      const QDateTime time = QDateTime::fromString( coords.value( u"t"_s ).toString(), Qt::ISODateWithMs );
      if ( gregorian && time.isValid() )
        f.setAttribute( 3, time );
      f.setAttribute( 4, fieldValue( coords.value( u"z"_s ), QMetaType::Double ) );
      f.setAttribute( 5, qgsEdrCompactJson( coords ) );
      for ( auto r = c.ranges.begin(); r != c.ranges.end(); ++r )
      {
        const QJsonValue value = r.value().at( index );
        f.setAttribute( fields[r.key()], fieldValue( value, types[r.key()] ) );
        if ( labels.contains( r.key() ) )
          f.setAttribute( labels[r.key()], categoryLabel( c.parameters.value( r.key() ).toObject(), value ) );
      }
      append( std::move( f ), crs );
    } );
  }
  const QgsCoordinateReferenceSystem geoCrs( u"OGC:CRS84"_s );
  for ( const QJsonValue &v : std::as_const( features ) )
  {
    const QJsonObject object = v.toObject();
    QgsFeature f( data.fields );
    const QgsFeatureList parsed = QgsJsonUtils::stringToFeatureList( qgsEdrCompactJson( object ) );
    if ( parsed.size() != 1 )
      throw QgsEdrError( QObject::tr( "QGIS cannot parse a GeoJSON feature" ) );
    f.setGeometry( parsed.first().geometry() );
    if ( !object.value( u"geometry"_s ).isNull() && f.geometry().isNull() )
      throw QgsEdrError( QObject::tr( "Invalid GeoJSON geometry" ) );
    f.setAttribute( 0, object.value( u"id"_s ).toVariant().toString() );
    f.setAttribute( 1, u"GeoJSON"_s );
    const QJsonObject properties = object.value( u"properties"_s ).toObject();
    for ( auto p = properties.begin(); p != properties.end(); ++p )
      f.setAttribute( fields[p.key()], fieldValue( p.value(), types[p.key()] ) );
    append( std::move( f ), geoCrs );
  }
  if ( !data.crs.isValid() )
    data.crs = geoCrs;
  if ( data.wkb == Qgis::WkbType::Unknown )
    data.wkb = Qgis::WkbType::NoGeometry;
  for ( QgsFeature &f : data.features )
  {
    QgsGeometry g = f.geometry();
    if ( g.isNull() )
      continue;
    if ( QgsWkbTypes::isMultiType( data.wkb ) )
      g.convertToMultiType();
    if ( QgsWkbTypes::hasZ( data.wkb ) && !QgsWkbTypes::hasZ( g.wkbType() ) )
      g.get()->addZValue( std::numeric_limits<double>::quiet_NaN() );
    if ( QgsWkbTypes::hasM( data.wkb ) && !QgsWkbTypes::hasM( g.wkbType() ) )
      g.get()->addMValue( std::numeric_limits<double>::quiet_NaN() );
    f.setGeometry( g );
  }
  return data;
}
