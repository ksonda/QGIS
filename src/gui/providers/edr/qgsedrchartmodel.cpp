/***************************************************************************
    qgsedrchartmodel.cpp
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

#include "qgsedrchartmodel.h"

#include <algorithm>
#include <cmath>

#include "qgsedrcoveragejson.h"
#include "qgsfeatureiterator.h"
#include "qgsvectorlayer.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QLocale>
#include <QObject>
#include <QString>

using namespace Qt::StringLiterals;

namespace
{
  QString localized( const QJsonValue &value )
  {
    if ( value.isString() )
      return value.toString();
    const QJsonObject object = value.toObject();
    const QString language = QLocale().name().section( '_', 0, 0 );
    if ( object.contains( language ) )
      return object.value( language ).toString();
    if ( object.contains( u"en"_s ) )
      return object.value( u"en"_s ).toString();
    return object.isEmpty() ? QString() : object.begin().value().toString();
  }

  QJsonObject seriesCoordinates( const QgsFeature &feature )
  {
    QJsonObject axes = QJsonDocument::fromJson( feature.attribute( u"_edr_axes"_s ).toString().toUtf8() ).object();
    axes.remove( u"t"_s );
    const QString domain = feature.attribute( u"_edr_domain"_s ).toString();
    // Tuple members are already expanded into named coordinates by the reader.
    if ( axes.contains( u"x"_s ) && axes.contains( u"y"_s ) )
      axes.remove( u"composite"_s );
    if ( domain == "Trajectory"_L1 || domain == "Section"_L1 )
    {
      axes.remove( u"x"_s );
      axes.remove( u"y"_s );
      if ( domain == "Trajectory"_L1 )
        axes.remove( u"z"_s );
    }
    return axes;
  }

  QString seriesLabel( const QgsFeature &feature, const QJsonObject &axes )
  {
    QStringList parts;
    const QString coverage = feature.attribute( u"_edr_coverage"_s ).toString();
    if ( !coverage.isEmpty() )
      parts.append( coverage );
    const QString domain = feature.attribute( u"_edr_domain"_s ).toString();
    if ( domain == "Trajectory"_L1 || domain == "Section"_L1 )
      parts.append( domain );
    for ( auto i = axes.begin(); i != axes.end(); ++i )
      parts.append( i.key() + '=' + qgsEdrCompactJson( i.value() ) );
    return parts.isEmpty() ? QObject::tr( "Series" ) : parts.join( " | "_L1 );
  }
} //namespace

QString QgsEdrChartParameter::title() const
{
  const QString text = label.isEmpty() ? name : label;
  return unit.isEmpty() ? text : text + u" ("_s + unit + ')';
}

QVector<QgsEdrChartParameter> qgsEdrChartParameters( const QgsFields &fields )
{
  QVector<QgsEdrChartParameter> result;
  for ( int i = 0; i < fields.size(); ++i )
  {
    const QgsField &field = fields[i];
    if ( field.name().startsWith( "_edr_"_L1 ) || !field.isNumeric() )
      continue;
    const QJsonObject metadata = QJsonDocument::fromJson( field.comment().toUtf8() ).object();
    if ( qgsEdrIsCategoricalParameter( metadata ) )
      continue;
    QgsEdrChartParameter parameter;
    parameter.field = i;
    parameter.name = field.name();
    parameter.label = localized( metadata.value( u"observedProperty"_s ).toObject().value( u"label"_s ) );
    if ( parameter.label.isEmpty() )
      parameter.label = localized( metadata.value( u"description"_s ) );
    if ( parameter.label.isEmpty() )
      parameter.label = field.displayName();
    const QJsonObject unit = metadata.value( u"unit"_s ).toObject();
    const QJsonValue symbol = unit.value( u"symbol"_s );
    parameter.unit = symbol.isString() ? symbol.toString() : symbol.toObject().value( u"value"_s ).toString();
    if ( parameter.unit.isEmpty() )
      parameter.unit = localized( unit.value( u"label"_s ) );
    result.append( parameter );
  }
  return result;
}

QgsEdrChartData qgsEdrChartData( QgsVectorLayer *layer, int field, int maximumRows )
{
  QgsEdrChartData result;
  if ( !layer || field < 0 || field >= layer->fields().size() )
    return result;
  const int timeField = layer->fields().indexOf( "_edr_datetime"_L1 );
  if ( timeField < 0 )
  {
    result.error = QObject::tr( "This layer has no EDR datetime field." );
    return result;
  }
  QHash<QByteArray, int> groups;
  QgsFeatureRequest request;
  request.setFlags( Qgis::FeatureRequestFlag::NoGeometry );
  request.setLimit( qint64( maximumRows ) + 1 );
  QgsFeatureIterator features = layer->getFeatures( request );
  QgsFeature feature;
  int rows = 0;
  while ( features.nextFeature( feature ) )
  {
    if ( ++rows > maximumRows )
    {
      result = {};
      result.error = QObject::tr( "The chart limit is %1 observations. Filter the layer or narrow the EDR query." ).arg( maximumRows );
      return result;
    }
    const QDateTime time = feature.attribute( timeField ).toDateTime();
    if ( !time.isValid() )
    {
      ++result.missingTimes;
      continue;
    }
    const QJsonObject axes = seriesCoordinates( feature );
    const QJsonObject identity { { u"coverage"_s, feature.attribute( u"_edr_coverage"_s ).toString() }, { u"domain"_s, feature.attribute( u"_edr_domain"_s ).toString() }, { u"axes"_s, axes } };
    const QByteArray key = QJsonDocument( identity ).toJson( QJsonDocument::Compact );
    auto group = groups.constFind( key );
    int index;
    if ( group == groups.constEnd() )
    {
      index = result.series.size();
      groups.insert( key, index );
      result.series.append( { QString::fromUtf8( key ), seriesLabel( feature, axes ), {} } );
    }
    else
      index = group.value();
    const QVariant value = feature.attribute( field );
    bool ok = false;
    double number = value.toDouble( &ok );
    if ( !ok || value.isNull() || !std::isfinite( number ) )
      number = std::numeric_limits<double>::quiet_NaN();
    result.series[index].samples.append( { time.toMSecsSinceEpoch(), number, feature.id() } );
    result.featureSeries.insert( feature.id(), index );
  }
  for ( QgsEdrChartSeries &series : result.series )
    std::stable_sort( series.samples.begin(), series.samples.end(), []( const QgsEdrChartSample &a, const QgsEdrChartSample &b ) { return a.time < b.time; } );
  if ( result.series.isEmpty() )
    result.error = QObject::tr( "No observations with Gregorian timestamps. Load a time series or change the layer filter." );
  return result;
}
