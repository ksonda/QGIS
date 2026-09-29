/***************************************************************************
    qgsedrcoveragejson.cpp
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

#include "qgsedrcoveragejson.h"

#include <cmath>
#include <limits>

#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QString>

using namespace Qt::StringLiterals;

bool qgsEdrIsCategoricalParameter( const QJsonObject &parameter )
{
  return !parameter.value( u"observedProperty"_s ).toObject().value( u"categories"_s ).toArray().isEmpty();
}

static int positiveInteger( const QJsonValue &v, const QString &label )
{
  double n = v.toDouble( -1 );
  if ( !v.isDouble() || n < 1 || n > 2147483647 || std::floor( n ) != n )
    throw QgsEdrError( QObject::tr( "%1 must be a positive integer" ).arg( label ) );
  return int( n );
}

static qint64 product( const QVector<int> &shape, qint64 maximum )
{
  qint64 result = 1;
  for ( int n : shape )
  {
    if ( n < 1 || result > maximum / n )
      throw QgsEdrError( QObject::tr( "Coverage exceeds maxCells; narrow the query or raise the limit" ) );
    result *= n;
  }
  return result;
}

static void indices( const QVector<int> &shape, const std::function<void( const QVector<int> & )> &callback )
{
  QVector<int> index( shape.size(), 0 );
  while ( true )
  {
    callback( index );
    int j = index.size() - 1;
    for ( ; j >= 0; --j )
    {
      if ( ++index[j] < shape[j] )
        break;
      index[j] = 0;
    }
    if ( j < 0 )
      break;
  }
}

QJsonValue QgsEdrArray::at( const QMap<QString, int> &index ) const
{
  int offset = 0;
  for ( int i = 0; i < axes.size(); ++i )
    offset = offset * shape[i] + index.value( axes[i] );
  return values.at( offset );
}

void QgsEdrCoverage::eachCell( const std::function<void( const QMap<QString, int> &, const QJsonObject & )> &callback ) const
{
  QStringList names = axes.keys();
  QVector<int> shape;
  for ( const QString &name : std::as_const( names ) )
    shape.append( axes[name].values.size() );
  indices( shape, [&]( const QVector<int> &index ) {
    QMap<QString, int> position;
    QJsonObject coordinates;
    for ( int i = 0; i < names.size(); ++i )
    {
      const QgsEdrAxis &axis = axes[names[i]];
      position.insert( names[i], index[i] );
      const QJsonValue value = axis.values.at( index[i] );
      coordinates.insert( names[i], value );
      if ( axis.dataType == "tuple"_L1 )
        for ( int j = 0; j < axis.coordinates.size(); ++j )
          coordinates.insert( axis.coordinates[j], value.toArray().at( j ) );
      else if ( axis.dataType != "polygon"_L1 && axis.coordinates.size() == 1 )
        coordinates.insert( axis.coordinates.first(), value );
    }
    callback( position, coordinates );
  } );
}

QgsEdrArray QgsEdrCoverageJsonReader::array( const QgsEdrDocument &doc )
{
  checkCanceled();
  const QJsonObject &o = doc.object;
  QgsEdrArray result;
  result.dataType = o.value( u"dataType"_s ).toString();
  if ( !QStringList { u"float"_s, u"integer"_s, u"string"_s }.contains( result.dataType ) )
    throw QgsEdrError( QObject::tr( "Unsupported range dataType: %1" ).arg( result.dataType ) );
  QSet<QString> names;
  for ( const QJsonValue &v : o.value( u"axisNames"_s ).toArray() )
  {
    if ( !v.isString() || v.toString().isEmpty() || names.contains( v.toString() ) )
      throw QgsEdrError( QObject::tr( "Invalid or duplicate range axis name" ) );
    names.insert( v.toString() );
    result.axes.append( v.toString() );
  }
  for ( const QJsonValue &v : o.value( u"shape"_s ).toArray() )
    result.shape.append( positiveInteger( v, u"Range shape"_s ) );
  if ( result.axes.size() != result.shape.size() )
    throw QgsEdrError( QObject::tr( "Range axisNames and shape differ in length" ) );
  const qint64 count = product( result.shape, mLimits.maximumCells );
  if ( o.value( u"type"_s ) == "NdArray"_L1 )
    result.values = o.value( u"values"_s ).toArray();
  else if ( o.value( u"type"_s ) == "TiledNdArray"_L1 )
  {
    const QJsonArray sets = o.value( u"tileSets"_s ).toArray();
    if ( sets.isEmpty() || result.axes.isEmpty() )
      throw QgsEdrError( QObject::tr( "TiledNdArray requires axes and a tileset" ) );
    // All tilesets encode the same array. Choose the one requiring the fewest requests.
    QJsonObject selected;
    qint64 fewest = std::numeric_limits<qint64>::max();
    for ( const QJsonValue &setValue : sets )
    {
      const QJsonObject set = setValue.toObject();
      const QJsonArray shape = set.value( u"tileShape"_s ).toArray();
      if ( shape.size() != result.shape.size() )
        throw QgsEdrError( QObject::tr( "tileShape has incorrect dimensionality" ) );
      qint64 requests = 1;
      for ( int i = 0; i < shape.size(); ++i )
      {
        int n = shape[i].isNull() ? result.shape[i] : positiveInteger( shape[i], u"Tile shape"_s );
        if ( n > result.shape[i] )
          throw QgsEdrError( QObject::tr( "Tile shape exceeds array shape" ) );
        requests *= ( result.shape[i] + qint64( n ) - 1 ) / n;
      }
      if ( requests < fewest )
      {
        selected = set;
        fewest = requests;
      }
    }
    if ( fewest > mLimits.maximumDocuments )
      throw QgsEdrError( QObject::tr( "Tileset exceeds maxDocuments" ) );
    QVector<int> tileShape, tileCounts;
    const QJsonArray shape = selected.value( u"tileShape"_s ).toArray();
    const QString pattern = selected.value( u"urlTemplate"_s ).toString();
    if ( pattern.isEmpty() )
      throw QgsEdrError( QObject::tr( "Missing tile URL template" ) );
    QSet<QString> templateVariables;
    QRegularExpressionMatchIterator matches = QRegularExpression( u"\\{([^{}]+)\\}"_s ).globalMatch( pattern );
    while ( matches.hasNext() )
      for ( const QString &variable : matches.next().captured( 1 ).split( ',' ) )
        templateVariables.insert( variable );
    for ( int i = 0; i < shape.size(); ++i )
    {
      int n = shape[i].isNull() ? result.shape[i] : shape[i].toInt();
      tileShape.append( n );
      tileCounts.append( int( ( result.shape[i] + qint64( n ) - 1 ) / n ) );
      if ( !shape[i].isNull() && !templateVariables.contains( result.axes[i] ) )
        throw QgsEdrError( QObject::tr( "Tile template is missing an axis variable" ) );
    }
    QVector<QJsonValue> values( static_cast<qsizetype>( count ), QJsonValue( QJsonValue::Null ) );
    indices( tileCounts, [&]( const QVector<int> &tileIndex ) {
      QString url = pattern;
      QRegularExpressionMatchIterator variables = QRegularExpression( u"\\{([^{}]+)\\}"_s ).globalMatch( pattern );
      while ( variables.hasNext() )
      {
        const QRegularExpressionMatch match = variables.next();
        QStringList values;
        for ( const QString &name : match.captured( 1 ).split( ',' ) )
        {
          const int i = result.axes.indexOf( name );
          if ( i < 0 )
            throw QgsEdrError( QObject::tr( "Unknown tile template variable: %1" ).arg( name ) );
          values.append( QString::number( tileIndex[i] ) );
        }
        url.replace( match.captured( 0 ), values.join( ',' ) );
      }
      if ( url.contains( '{' ) || url.contains( '}' ) )
        throw QgsEdrError( QObject::tr( "Unsupported or unresolved tile template variable" ) );
      const QgsEdrDocument tileDoc = resolve( QJsonValue( url ), doc.url );
      if ( tileDoc.object.value( u"type"_s ) != "NdArray"_L1 )
        throw QgsEdrError( QObject::tr( "A tile must be an NdArray" ) );
      const QgsEdrArray tile = array( tileDoc );
      if ( tile.axes != result.axes || tile.dataType != result.dataType )
        throw QgsEdrError( QObject::tr( "Tile axes or data type differ from the parent array" ) );
      for ( int i = 0; i < tileShape.size(); ++i )
        if ( tile.shape[i] != std::min( tileShape[i], result.shape[i] - tileIndex[i] * tileShape[i] ) )
          throw QgsEdrError( QObject::tr( "Incorrect edge tile shape" ) );
      int source = 0;
      indices( tile.shape, [&]( const QVector<int> &position ) {
        int offset = 0;
        for ( int i = 0; i < position.size(); ++i )
          offset = offset * result.shape[i] + tileIndex[i] * tileShape[i] + position[i];
        values[offset] = tile.values.at( source++ );
      } );
    } );
    for ( const QJsonValue &v : std::as_const( values ) )
      result.values.append( v );
  }
  else
    throw QgsEdrError( QObject::tr( "Unsupported range object: %1" ).arg( o.value( u"type"_s ).toString() ) );
  if ( result.values.size() != count )
    throw QgsEdrError( QObject::tr( "Range values length does not match shape" ) );
  static const QRegularExpression decimal( u"^[+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?$"_s );
  for ( int i = 0; i < result.values.size(); ++i )
  {
    QJsonValue v = result.values.at( i );
    if ( v.isNull() )
      continue;
    if ( result.dataType == "string"_L1 )
    {
      if ( !v.isString() )
        throw QgsEdrError( QObject::tr( "Non-string value in string range" ) );
    }
    else
    {
      // USGS serializes observations as decimal strings even in float ranges.
      if ( v.isString() )
      {
        const QString text = v.toString().trimmed();
        bool ok = false;
        const double number = text.toDouble( &ok );
        if ( !decimal.match( text ).hasMatch() || !ok || !std::isfinite( number ) )
          throw QgsEdrError( QObject::tr( "Invalid numeric range value" ) );
        v = number;
      }
      if ( !v.isDouble() || !std::isfinite( v.toDouble() ) || ( result.dataType == "integer"_L1 && ( std::floor( v.toDouble() ) != v.toDouble() || std::abs( v.toDouble() ) > 9007199254740991.0 ) ) )
        throw QgsEdrError( QObject::tr( "Invalid or unsafe numeric range value" ) );
      result.values[i] = v;
    }
  }
  return result;
}

QgsEdrCoverage QgsEdrCoverageJsonReader::coverage( const QgsEdrDocument &doc, const QJsonObject &parent )
{
  if ( doc.object.value( u"type"_s ) != "Coverage"_L1 )
    throw QgsEdrError( QObject::tr( "Expected Coverage object" ) );
  QgsEdrCoverage c;
  c.metadata = doc.object;
  c.id = doc.object.value( u"id"_s ).toVariant().toString();
  const QgsEdrDocument domainDoc = resolve( doc.object.value( u"domain"_s ), doc.url );
  c.domain = domainDoc.object;
  if ( c.domain.value( u"type"_s ) != "Domain"_L1 )
    throw QgsEdrError( QObject::tr( "Coverage domain is not a Domain object" ) );
  c.domainType = c.domain.value( u"domainType"_s ).toString( doc.object.value( u"domainType"_s ).toString( parent.value( u"domainType"_s ).toString() ) );
  c.parameters = parent.value( u"parameters"_s ).toObject();
  const QJsonObject ownParameters = doc.object.value( u"parameters"_s ).toObject();
  for ( auto i = ownParameters.begin(); i != ownParameters.end(); ++i )
    c.parameters.insert( i.key(), i.value() );
  c.referencing = c.domain.value( u"referencing"_s ).toArray();
  if ( c.referencing.isEmpty() )
    c.referencing = parent.value( u"referencing"_s ).toArray();
  c.metadata.insert( u"parameters"_s, c.parameters );
  if ( !c.metadata.contains( u"parameterGroups"_s ) )
    c.metadata.insert( u"parameterGroups"_s, parent.value( u"parameterGroups"_s ) );
  c.metadata.remove( u"ranges"_s );
  c.metadata.insert( u"domain"_s, c.domain );
  c.metadata.insert( u"referencing"_s, c.referencing );
  if ( !parent.isEmpty() )
  {
    QJsonObject metadata = parent;
    metadata.remove( u"coverages"_s );
    c.metadata.insert( u"collection"_s, metadata );
  }
  const QJsonObject axes = c.domain.value( u"axes"_s ).toObject();
  if ( axes.isEmpty() )
    throw QgsEdrError( QObject::tr( "Domain has no axes" ) );
  QVector<int> shape;
  QSet<QString> coordinateNames;
  for ( auto i = axes.begin(); i != axes.end(); ++i )
  {
    QgsEdrAxis axis;
    axis.name = i.key();
    const QJsonObject a = i.value().toObject();
    axis.dataType = a.value( u"dataType"_s ).toString();
    for ( const QJsonValue &v : a.value( u"coordinates"_s ).toArray() )
      axis.coordinates.append( v.toString() );
    if ( axis.coordinates.isEmpty() && axis.dataType != "tuple"_L1 && axis.dataType != "polygon"_L1 )
      axis.coordinates.append( axis.name );
    for ( const QString &name : std::as_const( axis.coordinates ) )
    {
      if ( name.isEmpty() || coordinateNames.contains( name ) )
        throw QgsEdrError( QObject::tr( "Duplicate or empty domain coordinate identifier" ) );
      coordinateNames.insert( name );
    }
    if ( axis.dataType != "tuple"_L1 && axis.dataType != "polygon"_L1 && axis.coordinates.size() != 1 )
      throw QgsEdrError( QObject::tr( "Primitive axes require one coordinate identifier" ) );
    axis.values = a.value( u"values"_s ).toArray();
    axis.bounds = a.value( u"bounds"_s ).toArray();
    if ( !a.contains( u"values"_s ) )
    {
      const int count = positiveInteger( a.value( u"num"_s ), u"Regular axis num"_s );
      if ( count > mLimits.maximumCells )
        throw QgsEdrError( QObject::tr( "Axis exceeds maxCells" ) );
      if ( !a.value( u"start"_s ).isDouble() || !a.value( u"stop"_s ).isDouble() )
        throw QgsEdrError( QObject::tr( "Regular axes require numeric start and stop" ) );
      const double start = a.value( u"start"_s ).toDouble(), stop = a.value( u"stop"_s ).toDouble();
      if ( count == 1 && start != stop )
        throw QgsEdrError( QObject::tr( "Singleton regular axis start must equal stop" ) );
      for ( int j = 0; j < count; ++j )
        axis.values.append( count == 1 ? start : start + ( stop - start ) * j / ( count - 1 ) );
    }
    if ( axis.values.isEmpty() )
      throw QgsEdrError( QObject::tr( "Empty domain axis: %1" ).arg( i.key() ) );
    if ( !axis.bounds.isEmpty() && axis.bounds.size() != 2 * axis.values.size() )
      throw QgsEdrError( QObject::tr( "Axis bounds must contain two values per coordinate" ) );
    if ( axis.dataType == "tuple"_L1 )
      for ( const QJsonValue &v : std::as_const( axis.values ) )
        if ( !v.isArray() || v.toArray().size() != axis.coordinates.size() || axis.coordinates.isEmpty() )
          throw QgsEdrError( QObject::tr( "Tuple axis coordinate count mismatch" ) );
    if ( !axis.dataType.isEmpty() && axis.dataType != "tuple"_L1 && axis.dataType != "polygon"_L1 && axis.dataType != "primitive"_L1 )
      throw QgsEdrError( QObject::tr( "Unsupported axis dataType: %1" ).arg( axis.dataType ) );
    c.axes.insert( i.key(), axis );
    shape.append( axis.values.size() );
  }
  c.cellCount = product( shape, mLimits.maximumCells );
  mCells += c.cellCount;
  if ( mCells > mLimits.maximumCells )
    throw QgsEdrError( QObject::tr( "Combined coverage cells exceed maxCells" ) );
  const QJsonObject ranges = doc.object.value( u"ranges"_s ).toObject();
  if ( ranges.isEmpty() )
    throw QgsEdrError( QObject::tr( "Coverage has no ranges" ) );
  for ( auto i = ranges.begin(); i != ranges.end(); ++i )
  {
    if ( !c.parameters.contains( i.key() ) )
      throw QgsEdrError( QObject::tr( "Range has no parameter metadata: %1" ).arg( i.key() ) );
    const QgsEdrArray a = array( resolve( i.value(), doc.url ) );
    for ( int j = 0; j < a.axes.size(); ++j )
      if ( !c.axes.contains( a.axes[j] ) || c.axes[a.axes[j]].values.size() != a.shape[j] )
        throw QgsEdrError( QObject::tr( "Range shape differs from domain: %1" ).arg( i.key() ) );
    for ( auto j = c.axes.begin(); j != c.axes.end(); ++j )
      if ( j.value().values.size() > 1 && !a.axes.contains( j.key() ) )
        throw QgsEdrError( QObject::tr( "Range omits a non-singleton domain axis: %1" ).arg( j.key() ) );
    const QJsonObject parameter = c.parameters.value( i.key() ).toObject();
    const QJsonObject encoding = parameter.value( u"categoryEncoding"_s ).toObject();
    // Some servers place palette thresholds here without defining categories.
    // Preserve those as metadata instead of rejecting continuous observations.
    if ( qgsEdrIsCategoricalParameter( parameter ) && !encoding.isEmpty() )
    {
      QSet<QString> encoded;
      for ( auto category = encoding.begin(); category != encoding.end(); ++category )
      {
        QJsonArray codes = category.value().isArray() ? category.value().toArray() : QJsonArray { category.value() };
        for ( const QJsonValue &code : std::as_const( codes ) )
          encoded.insert( qgsEdrCompactJson( code ) );
      }
      for ( const QJsonValue &value : a.values )
        if ( !value.isNull() && !encoded.contains( qgsEdrCompactJson( value ) ) )
          throw QgsEdrError( QObject::tr( "Range value is absent from categoryEncoding" ) );
    }
    c.ranges.insert( i.key(), a );
  }
  return c;
}

QVector<QgsEdrCoverage> QgsEdrCoverageJsonReader::read( const QgsEdrDocument &doc )
{
  mCells = 0;
  QVector<QgsEdrCoverage> result;
  if ( doc.object.value( u"type"_s ) == "Coverage"_L1 )
    result.append( coverage( doc, {} ) );
  else if ( doc.object.value( u"type"_s ) == "CoverageCollection"_L1 )
  {
    if ( !doc.object.value( u"coverages"_s ).isArray() )
      throw QgsEdrError( QObject::tr( "CoverageCollection has no coverages array" ) );
    for ( const QJsonValue &v : doc.object.value( u"coverages"_s ).toArray() )
    {
      checkCanceled();
      result.append( coverage( resolve( v, doc.url ), doc.object ) );
    }
  }
  else
    throw QgsEdrError( QObject::tr( "Expected Coverage or CoverageCollection" ) );
  return result;
}

QgsEdrDocument QgsEdrCoverageJsonReader::resolve( const QJsonValue &value, const QUrl &base )
{
  if ( value.isObject() )
    return { value.toObject(), base };
  if ( !value.isString() || value.toString().isEmpty() )
    throw QgsEdrError( QObject::tr( "Expected a JSON object or linked document URL" ) );
  if ( !mLoader )
    throw QgsEdrError( QObject::tr( "No loader was supplied for a linked CoverageJSON document" ) );
  const QUrl url = base.resolved( QUrl( value.toString() ) );
  if ( !base.isLocalFile() && url.isLocalFile() )
    throw QgsEdrError( QObject::tr( "A remote document cannot reference a local file" ) );
  return mLoader( url );
}

void QgsEdrCoverageJsonReader::checkCanceled() const
{
  if ( mFeedback && mFeedback->isCanceled() )
    throw QgsEdrError( QObject::tr( "CoverageJSON reading canceled" ) );
}
