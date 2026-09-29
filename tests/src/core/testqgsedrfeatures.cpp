/***************************************************************************
    testqgsedrfeatures.cpp
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

#include "qgsapplication.h"
#include "qgsedrclient.h"
#include "qgsedrcoveragejson.h"
#include "qgsedrdataset.h"
#include "qgsedrquery.h"
#include "qgsgeometry.h"
#include "qgstest.h"

#include <QFile>
#include <QJsonDocument>
#include <QString>
#include <QTemporaryDir>
#include <QUrlQuery>

using namespace Qt::StringLiterals;

class TestQgsEdrFeatures : public QObject
{
    Q_OBJECT
    QString path( const QString &name ) const { return QStringLiteral( TEST_DATA_DIR ) + u"/edr"_s + '/' + name + ".covjson"; }
    QJsonObject fixture( const QString &name ) const
    {
      QFile f( path( name ) );
      if ( !f.open( QIODevice::ReadOnly ) )
        throw QgsEdrError( "Missing test fixture" );
      return QJsonDocument::fromJson( f.readAll() ).object();
    }
    QgsEdrQuery local( const QString &name ) const
    {
      QgsEdrQuery q;
      q.root = QUrl::fromLocalFile( path( name ) );
      return q;
    }
    QVector<QgsEdrCoverage> parse( const QJsonObject &object, const QString &name = "Point" )
    {
      QgsEdrClient client( local( name ) );
      QgsEdrCoverageJsonReader reader( [&client]( const QUrl &url ) { return client.get( url ); } );
      return reader.read( { object, local( name ).root } );
    }

  private slots:
    void initTestCase()
    {
      QgsApplication::init();
      QgsApplication::initQgis();
    }
    void cleanupTestCase() { QgsApplication::exitQgis(); }
    void domains_data()
    {
      QTest::addColumn<QString>( "name" );
      QTest::addColumn<int>( "count" );
      const QMap<QString, int> counts {
        { "Point", 1 },
        { "PointSeries", 2 },
        { "VerticalProfile", 3 },
        { "MultiPoint", 2 },
        { "MultiPointSeries", 4 },
        { "Trajectory", 2 },
        { "Section", 6 },
        { "Polygon", 1 },
        { "PolygonSeries", 2 },
        { "MultiPolygon", 2 },
        { "MultiPolygonSeries", 4 },
        { "Grid", 12 }
      };
      for ( auto i = counts.begin(); i != counts.end(); ++i )
        QTest::newRow( qPrintable( i.key() ) ) << i.key() << i.value();
    }
    void domains()
    {
      QFETCH( QString, name );
      QFETCH( int, count );
      const QgsEdrQuery q = local( name );
      const QgsEdrDataset d = qgsEdrLoadDataset( q );
      const QgsEdrVectorData v = qgsEdrVectorData( d, q, {} );
      QCOMPARE( v.features.size(), count );
      QVERIFY( v.crs.isValid() );
      for ( const QgsFeature &f : v.features )
      {
        QVERIFY( !f.geometry().isNull() );
        QVERIFY( f.attribute( "temperature" ).isValid() );
      }
      if ( name.contains( "Polygon" ) )
        QCOMPARE( v.features.first().geometry().asPolygon().size(), 2 );
      if ( name == "Trajectory" )
        QCOMPARE( v.features[1].attribute( "_edr_time" ).toString(), QString( "2026-01-01T01:00:00Z" ) );
    }
    void tupleAliasesAndScalarRanges()
    {
      QJsonObject object = fixture( "Point" );
      QJsonObject domain = object["domain"].toObject();
      domain["axes"]
        = QJsonObject { { "longitude", QJsonObject { { "values", QJsonArray { -75 } }, { "coordinates", QJsonArray { "x" } } } }, { "latitude", QJsonObject { { "values", QJsonArray { 40 } }, { "coordinates", QJsonArray { "y" } } } } };
      object["domain"] = domain;
      QgsEdrDataset d;
      d.coverages = parse( object );
      QgsEdrVectorData v = qgsEdrVectorData( d, local( "Point" ), {} );
      QCOMPARE( v.features[0].geometry().asPoint(), QgsPointXY( -75, 40 ) );
      QJsonObject ranges = object["ranges"].toObject();
      QJsonObject array = ranges["temperature"].toObject();
      array.remove( "shape" );
      array.remove( "axisNames" );
      ranges["temperature"] = array;
      object["ranges"] = ranges;
      QCOMPARE( parse( object )[0].ranges["temperature"].values.size(), 1 );
    }
    void inheritanceAndCategories()
    {
      const QgsEdrQuery q = local( "collection" );
      const QgsEdrDataset d = qgsEdrLoadDataset( q );
      QCOMPARE( d.coverages.size(), 2 );
      QCOMPARE( d.coverages[0].parameters["temperature"].toObject()["type"].toString(), QString( "Parameter" ) );
      QCOMPARE( qgsEdrVectorData( d, q, {} ).features.size(), 2 );
      for ( const char *name : { "categories", "string-categories" } )
      {
        QgsEdrQuery cq = local( name );
        QgsEdrVectorData v = qgsEdrVectorData( qgsEdrLoadDataset( cq ), cq, {} );
        QCOMPARE( v.features[0].attribute( "temperature_label" ).toString(), QString( "Clear sky" ) );
        QCOMPARE( v.features[1].attribute( "temperature_label" ).toString(), QString( "Cloudy" ) );
      }
    }
    void numericStrings()
    {
      QJsonObject object = fixture( "PointSeries" );
      QJsonObject ranges = object["ranges"].toObject();
      QJsonObject array = ranges["temperature"].toObject();
      array["values"] = QJsonArray { "307", "-1.25e2" };
      ranges["temperature"] = array;
      object["ranges"] = ranges;
      QgsEdrDataset d;
      d.coverages = parse( object );
      const QgsEdrVectorData v = qgsEdrVectorData( d, local( "PointSeries" ), {} );
      QCOMPARE( v.features[0].attribute( "temperature" ).toDouble(), 307.0 );
      QCOMPARE( v.features[1].attribute( "temperature" ).toDouble(), -125.0 );
      array["values"] = QJsonArray { QJsonValue( QJsonValue::Null ), "0" };
      ranges["temperature"] = array;
      object["ranges"] = ranges;
      QVERIFY( parse( object )[0].ranges["temperature"].values[0].isNull() );
      for ( const char *bad : { "", "water", "1,234", "NaN", "inf", "1e400", "1e-400" } )
      {
        array["values"] = QJsonArray { "307", bad };
        ranges["temperature"] = array;
        object["ranges"] = ranges;
        QVERIFY_EXCEPTION_THROWN( parse( object ), QgsEdrError );
      }
      array["dataType"] = "integer";
      for ( const char *bad : { "1.5", "9007199254740993", "-9007199254740993" } )
      {
        array["values"] = QJsonArray { "307", bad };
        ranges["temperature"] = array;
        object["ranges"] = ranges;
        QVERIFY_EXCEPTION_THROWN( parse( object ), QgsEdrError );
      }
      array["dataType"] = "string";
      array["values"] = QJsonArray { "307", "water" };
      ranges["temperature"] = array;
      object["ranges"] = ranges;
      QVERIFY( parse( object )[0].ranges["temperature"].values[0].isString() );
    }
    void paletteMetadataWithoutCategories()
    {
      QJsonObject object = fixture( "Grid" );
      QJsonObject parameters = object["parameters"].toObject();
      QJsonObject parameter = parameters["temperature"].toObject();
      parameter["categoryEncoding"] = QJsonObject { { "#ffffff", 0.5 }, { "#000000", 999999 } };
      parameters["temperature"] = parameter;
      object["parameters"] = parameters;
      QgsEdrDataset d;
      d.coverages = parse( object );
      QgsEdrQuery q = local( "Grid" );
      QCOMPARE( qgsEdrVectorData( d, q, {} ).fields.lookupField( "temperature_label" ), -1 );
      QVERIFY( d.coverages[0].parameters["temperature"].toObject().contains( "categoryEncoding" ) );
    }
    void emptyCustomAndCalendar()
    {
      QgsEdrQuery q = local( "empty" );
      QCOMPARE( qgsEdrVectorData( qgsEdrLoadDataset( q ), q, {} ).features.size(), 0 );
      q = local( "custom" );
      QgsEdrVectorData v = qgsEdrVectorData( qgsEdrLoadDataset( q ), q, {} );
      QCOMPARE( v.features.size(), 2 );
      QVERIFY( v.features[1].attribute( "_edr_axes" ).toString().contains( "member1" ) );
      QJsonObject o = fixture( "PointSeries" );
      QJsonObject domain = o["domain"].toObject();
      QJsonArray refs = domain["referencing"].toArray();
      refs[1] = QJsonObject { { "coordinates", QJsonArray { "t" } }, { "system", QJsonObject { { "type", "TemporalRS" }, { "calendar", "360_day" } } } };
      domain["referencing"] = refs;
      o["domain"] = domain;
      QgsEdrDataset d;
      d.coverages = parse( o );
      v = qgsEdrVectorData( d, local( "PointSeries" ), {} );
      QVERIFY( v.features[0].attribute( "_edr_datetime" ).isNull() );
      QVERIFY( !v.features[0].attribute( "_edr_time" ).toString().isEmpty() );
    }
    void reprojectAndMixedGeometries()
    {
      QgsEdrQuery q = local( "Point" );
      q.targetCrs = "EPSG:3857";
      QgsEdrVectorData v = qgsEdrVectorData( qgsEdrLoadDataset( q ), q, {} );
      QVERIFY( std::abs( v.features[0].geometry().asPoint().x() + 8348961.809495518 ) < 0.001 );
      QgsEdrDataset d = qgsEdrLoadDataset( local( "Point" ) );
      d.coverages += qgsEdrLoadDataset( local( "Polygon" ) ).coverages;
      QVERIFY_EXCEPTION_THROWN( qgsEdrVectorData( d, local( "Point" ), {} ), QgsEdrError );
      q = local( "Point" );
      q.geometry = "polygon";
      v = qgsEdrVectorData( d, q, {} );
      QCOMPARE( v.features.size(), 1 );
      QCOMPARE( v.wkb, Qgis::WkbType::Polygon );
    }
};
QGSTEST_MAIN( TestQgsEdrFeatures )
#include "testqgsedrfeatures.moc"
