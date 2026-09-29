/***************************************************************************
    testqgsedrchart.cpp
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

#include <cmath>
#include <memory>

#include "providers/edr/qgsedrchart.h"
#include "providers/edr/qgsedrguiprovider.h"
#include "qgsapplication.h"
#include "qgscoordinatetransform.h"
#include "qgsedrquery.h"
#include "qgsfeatureiterator.h"
#include "qgsgui.h"
#include "qgsmapcanvas.h"
#include "qgsmaplayeraction.h"
#include "qgsmaplayeractioncontext.h"
#include "qgsmaplayeractionregistry.h"
#include "qgsproject.h"
#include "qgsproviderregistry.h"
#include "qgstest.h"
#include "qgsvectordataprovider.h"
#include "qgsvectorlayer.h"

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMainWindow>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>
#include <QWheelEvent>

using namespace Qt::StringLiterals;

class TestQgsEdrChart final : public QObject
{
    Q_OBJECT
    std::unique_ptr<QgsVectorLayer> layer( const QString &name ) const
    {
      QgsEdrQuery q;
      q.root = QUrl::fromLocalFile( QStringLiteral( TEST_DATA_DIR ) + u"/edr"_s + '/' + name + u".covjson"_s );
      return std::make_unique<QgsVectorLayer>( q.toUri(), name, u"edr"_s );
    }
    QJsonObject fixture( const QString &name ) const
    {
      QFile file( QStringLiteral( TEST_DATA_DIR ) + u"/edr"_s + '/' + name + u".covjson"_s );
      if ( !file.open( QIODevice::ReadOnly ) )
        throw QgsEdrError( u"Missing chart fixture"_s );
      return QJsonDocument::fromJson( file.readAll() ).object();
    }
  private slots:
    void initTestCase()
    {
      QgsApplication::init();
      QgsApplication::initQgis();
    }
    void cleanupTestCase() { QgsApplication::exitQgis(); }
    void cleanup() { QgsProject::instance()->removeAllMapLayers(); }
    void grouping_data()
    {
      QTest::addColumn<QString>( "name" );
      QTest::addColumn<int>( "groups" );
      QTest::addColumn<int>( "samples" );
      QTest::newRow( "station" ) << QString( u"PointSeries"_s ) << 1 << 2;
      QTest::newRow( "two-stations" ) << QString( u"MultiPointSeries"_s ) << 2 << 2;
      QTest::newRow( "grid-cells" ) << QString( u"Grid"_s ) << 6 << 2;
      QTest::newRow( "polygon" ) << QString( u"PolygonSeries"_s ) << 1 << 2;
      QTest::newRow( "two-polygons" ) << QString( u"MultiPolygonSeries"_s ) << 2 << 2;
      QTest::newRow( "moving-trajectory" ) << QString( u"Trajectory"_s ) << 1 << 2;
      QTest::newRow( "section-levels" ) << QString( u"Section"_s ) << 3 << 2;
    }
    void grouping()
    {
      QFETCH( QString, name );
      QFETCH( int, groups );
      QFETCH( int, samples );
      auto source = layer( name );
      QVERIFY( source->isValid() );
      const QgsEdrChartData data = qgsEdrChartData( source.get(), source->fields().indexOf( "temperature"_L1 ) );
      QVERIFY2( data.error.isEmpty(), qPrintable( data.error ) );
      QCOMPARE( data.series.size(), groups );
      for ( const QgsEdrChartSeries &series : data.series )
        QCOMPARE( series.samples.size(), samples );
      QCOMPARE( data.featureSeries.size(), groups * samples );
    }
    void unitsAndCategories()
    {
      auto source = layer( u"PointSeries"_s );
      const QVector<QgsEdrChartParameter> parameters = qgsEdrChartParameters( source->fields() );
      QCOMPARE( parameters.size(), 1 );
      QCOMPARE( parameters[0].name, QString( u"temperature"_s ) );
      QCOMPARE( parameters[0].label, QString( u"Temperature"_s ) );
      QCOMPARE( parameters[0].unit, QString( u"K"_s ) );
      QCOMPARE( parameters[0].title(), QString( u"Temperature (K)"_s ) );
      auto categorical = layer( u"categories"_s );
      QVERIFY( categorical->isValid() );
      QVERIFY( qgsEdrChartParameters( categorical->fields() ).isEmpty() );
    }
    void sortingMissingValuesAndDistinctDimensions()
    {
      auto source = layer( u"PointSeries"_s );
      QgsVectorLayer memory( u"Point?crs=EPSG:4326"_s, u"time samples"_s, u"memory"_s );
      memory.dataProvider()->addAttributes( source->fields().toList() );
      memory.updateFields();
      QgsFeature templateFeature;
      source->getFeatures().nextFeature( templateFeature );
      QgsFeatureList features;
      const QStringList dates { u"2026-01-01T02:00:00Z"_s, u"2026-01-01T01:00:00+01:00"_s, u"2026-01-01T01:00:00Z"_s, u"2026-01-01T02:00:00Z"_s, u""_s };
      for ( int i = 0; i < dates.size(); ++i )
      {
        QgsFeature feature = templateFeature;
        feature.setId( FID_NULL );
        feature.setAttribute( u"_edr_datetime"_s, QDateTime::fromString( dates[i], Qt::ISODate ) );
        feature.setAttribute( u"temperature"_s, i == 2 ? QVariant() : QVariant( i ) );
        features.append( feature );
      }
      // A different member, level, and coverage at the same point must stay separate.
      for ( const QString &axis : { QString( u"ensemble"_s ), QString( u"z"_s ), QString( u"coverage"_s ) } )
      {
        QgsFeature feature = features[0];
        if ( axis == "coverage"_L1 )
          feature.setAttribute( u"_edr_coverage"_s, u"other"_s );
        else
        {
          QJsonObject coords = QJsonDocument::fromJson( feature.attribute( u"_edr_axes"_s ).toString().toUtf8() ).object();
          coords[axis] = axis == "z"_L1 ? QJsonValue( 850 ) : QJsonValue( u"member2"_s );
          feature.setAttribute( u"_edr_axes"_s, QString::fromUtf8( QJsonDocument( coords ).toJson( QJsonDocument::Compact ) ) );
        }
        features.append( feature );
      }
      QVERIFY( memory.dataProvider()->addFeatures( features ) );
      const QgsEdrChartData data = qgsEdrChartData( &memory, memory.fields().indexOf( "temperature"_L1 ) );
      QCOMPARE( data.series.size(), 4 );
      QCOMPARE( data.missingTimes, 1 );
      const QVector<QgsEdrChartSample> samples = data.series[0].samples;
      QCOMPARE( samples.size(), 4 );
      QCOMPARE( samples[0].time, QDateTime::fromString( u"2026-01-01T00:00:00Z"_s, Qt::ISODate ).toMSecsSinceEpoch() );
      QVERIFY( std::isnan( samples[1].value ) );
      QCOMPARE( samples[2].time, samples[3].time ); // Duplicates are retained, not averaged.
      QCOMPARE( samples[2].value, 0.0 );
      QCOMPARE( samples[3].value, 3.0 );
    }
    void calendarsLimitsAndFilters()
    {
      auto source = layer( u"Grid"_s );
      const int field = source->fields().indexOf( "temperature"_L1 );
      QgsEdrChartData data = qgsEdrChartData( source.get(), field, 3 );
      QVERIFY( !data.error.isEmpty() );
      QVERIFY( data.series.isEmpty() );
      QVERIFY( source->setSubsetString( u"\"temperature\" < 2"_s ) );
      data = qgsEdrChartData( source.get(), field, 3 );
      QVERIFY( data.error.isEmpty() );
      QCOMPARE( data.featureSeries.size(), 2 );
      QTemporaryDir directory;
      QJsonObject object = fixture( u"PointSeries"_s );
      QJsonObject domain = object[u"domain"_s].toObject();
      QJsonArray refs = domain[u"referencing"_s].toArray();
      refs[1] = QJsonObject { { u"coordinates"_s, QJsonArray { u"t"_s } }, { u"system"_s, QJsonObject { { u"type"_s, u"TemporalRS"_s }, { u"calendar"_s, u"360_day"_s } } } };
      domain[u"referencing"_s] = refs;
      object[u"domain"_s] = domain;
      QFile file( directory.filePath( u"calendar.covjson"_s ) );
      QVERIFY( file.open( QIODevice::WriteOnly ) );
      file.write( QJsonDocument( object ).toJson() );
      file.close();
      QgsEdrQuery q;
      q.root = QUrl::fromLocalFile( file.fileName() );
      QgsVectorLayer calendar( q.toUri(), u"calendar"_s, u"edr"_s );
      QVERIFY( calendar.isValid() );
      data = qgsEdrChartData( &calendar, calendar.fields().indexOf( "temperature"_L1 ) );
      QCOMPARE( data.missingTimes, 2 );
      QVERIFY( data.series.isEmpty() );
      QVERIFY( !data.error.isEmpty() );
    }
    void nativeDockSelectionPickingAndRemoval()
    {
      auto owned = layer( u"MultiPointSeries"_s );
      QgsVectorLayer *source = owned.release();
      QgsProject::instance()->addMapLayer( source );
      QMainWindow window;
      QgsMapCanvas *canvas = new QgsMapCanvas( &window );
      window.setCentralWidget( canvas );
      canvas->setDestinationCrs( QgsCoordinateReferenceSystem( u"EPSG:3857"_s ) );
      canvas->setLayers( { source } );
      const QgsCoordinateTransform transform( source->crs(), canvas->mapSettings().destinationCrs(), QgsProject::instance() );
      const QgsPointXY station = transform.transform( QgsPointXY( -74, 41 ) );
      canvas->setExtent( QgsRectangle( station.x() - 30000, station.y() - 30000, station.x() + 30000, station.y() + 30000 ) );
      QgsEdrChartDock *dock = new QgsEdrChartDock( &window, canvas );
      window.addDockWidget( Qt::BottomDockWidgetArea, dock );
      window.resize( 1000, 750 );
      window.show();
      dock->show();
      QCoreApplication::processEvents();
      QComboBox *series = dock->findChild<QComboBox *>( u"edrChartSeries"_s );
      QgsEdrChartPlot *plot = dock->findChild<QgsEdrChartPlot *>();
      QCOMPARE( series->count(), 2 );
      QCOMPARE( plot->samples().size(), 2 );
      const QgsFeatureId lastFeature = dock->data().series[1].samples[1].feature;
      source->selectByIds( { lastFeature } );
      QCOMPARE( series->currentIndex(), 1 );
      QCOMPARE( plot->samples().size(), 2 );
      series->setCurrentIndex( 0 );
      dock->pickAt( station );
      QCOMPARE( series->currentIndex(), 1 );
      QCOMPARE( plot->samples().size(), 2 );
      QSignalSpy clicked( plot, &QgsEdrChartPlot::sampleSelected );
      QTest::mouseClick( plot, Qt::LeftButton, Qt::NoModifier, plot->samplePosition( 1 ).toPoint() );
      QCOMPARE( clicked.count(), 1 );
      QVERIFY( source->selectedFeatureIds().contains( lastFeature ) );
      const auto second = layer( u"PointSeries"_s ); // An unregistered layer must not disturb the project list.
      QVERIFY( second->isValid() );
      const QString id = source->id();
      QgsProject::instance()->removeMapLayer( id );
      QCoreApplication::processEvents();
      QVERIFY( plot->samples().isEmpty() );
      QCOMPARE( series->count(), 0 );
    }
    void registrationAndActionLifetime()
    {
      QgsVectorLayer *source = layer( u"MultiPointSeries"_s ).release();
      QgsProject::instance()->addMapLayer( source );
      QgsEdrProviderGuiMetadata metadata;
      auto window = std::make_unique<QMainWindow>();
      metadata.registerGui( window.get() );
      metadata.registerGui( window.get() );
      QCOMPARE( window->findChildren<QgsEdrChartDock *>().size(), 1 );
      QgsEdrChartDock *dock = window->findChild<QgsEdrChartDock *>();
      QVERIFY( dock->isHidden() );
      QVERIFY( dock->data().series.isEmpty() );
      QVERIFY( window->findChild<QAction *>( u"edrChartAction"_s ) );
      QgsMapLayerActionRegistry *registry = QgsGui::mapLayerActionRegistry();
      QPointer<QgsMapLayerAction> chartAction;
      const QList<QgsMapLayerAction *> actions = registry->mapLayerActions( source );
      for ( QgsMapLayerAction *action : actions )
        if ( action->parent() == dock )
          chartAction = action;
      QVERIFY( chartAction );
      const QgsEdrChartData observations = qgsEdrChartData( source, source->fields().indexOf( "temperature"_L1 ) );
      QCOMPARE( observations.series.size(), 2 );
      QVERIFY( !observations.series.at( 1 ).samples.isEmpty() );
      QgsFeature selected;
      const QgsFeatureId featureId = observations.series.at( 1 ).samples.last().feature;
      QVERIFY( source->getFeatures( QgsFeatureRequest( featureId ) ).nextFeature( selected ) );
      window->show();
      QCoreApplication::processEvents();
      chartAction->triggerForFeature( source, selected, QgsMapLayerActionContext() );
      QVERIFY( dock->isVisible() );
      QCOMPARE( dock->data().series.size(), 2 );
      QCOMPARE( dock->findChild<QComboBox *>( u"edrChartSeries"_s )->currentIndex(), 1 );
      window.reset();
      QVERIFY( chartAction.isNull() );
      QCOMPARE( registry->mapLayerActions( source ).size(), actions.size() - 1 );
    }
    void chartInteractionAndEmptyStates()
    {
      QgsEdrChartPlot plot;
      plot.resize( 900, 300 );
      plot.show();
      const QgsEdrChartParameter parameter { 0, u"temperature"_s, u"Temperature"_s, u"K"_s };
      const qint64 t = QDateTime::fromString( u"2026-01-01T00:00:00Z"_s, Qt::ISODate ).toMSecsSinceEpoch();
      plot.setSeries( { u"station"_s, u"Station"_s, { { t, 2, 10 }, { t + 3600000, 4, 11 }, { t + 7200000, 3, 12 } } }, parameter );
      QSignalSpy hover( &plot, &QgsEdrChartPlot::sampleHovered );
      QTest::mouseMove( &plot, plot.samplePosition( 1 ).toPoint() );
      QVERIFY( !hover.isEmpty() );
      QVERIFY( hover.last().first().toString().contains( u"2026-01-01T01:00:00.000Z"_s ) );
      QVERIFY( hover.last().first().toString().contains( u"4 K"_s ) );
      const double x = plot.samplePosition( 0 ).x();
      const QPointF anchor = plot.samplePosition( 1 );
      QWheelEvent wheel( anchor, plot.mapToGlobal( anchor.toPoint() ), QPoint(), QPoint( 0, 120 ), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false );
      QCoreApplication::sendEvent( &plot, &wheel );
      QVERIFY( plot.samplePosition( 0 ).x() < x );
      plot.resetView();
      QCOMPARE( plot.samplePosition( 0 ).x(), x );
      QVERIFY( !plot.grab().isNull() );
      plot.setSeries( { u"constant"_s, u"Constant"_s, { { t, 3, 1 } } }, parameter );
      QVERIFY( std::isfinite( plot.samplePosition( 0 ).y() ) );
      plot.setSeries( { u"missing"_s, u"Missing"_s, { { t, std::numeric_limits<double>::quiet_NaN(), 1 } } }, parameter );
      QVERIFY( !plot.grab().isNull() );
      plot.clear( u"No observations"_s );
      QVERIFY( plot.samples().isEmpty() );
      QVERIFY( !plot.grab().isNull() );
    }
};

QGSTEST_MAIN( TestQgsEdrChart )
#include "testqgsedrchart.moc"
