/***************************************************************************
    testqgsedrcoveragejson.cpp
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

#include "qgsapplication.h"
#include "qgsedrcoveragejson.h"
#include "qgstest.h"

#include <QFile>
#include <QJsonDocument>
#include <QString>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;

class TestQgsEdrCoverageJson : public QObject
{
    Q_OBJECT

    QString path( const QString &name ) const { return QStringLiteral( TEST_DATA_DIR ) + u"/edr/"_s + name + u".covjson"_s; }
    static QgsEdrDocument readFile( const QUrl &url )
    {
      QFile file( url.toLocalFile() );
      if ( !url.isLocalFile() || !file.open( QIODevice::ReadOnly ) )
        throw QgsEdrError( u"Missing local test fixture"_s );
      return { QJsonDocument::fromJson( file.readAll() ).object(), url };
    }
    QJsonObject fixture( const QString &name ) const { return readFile( QUrl::fromLocalFile( path( name ) ) ).object; }
    QVector<QgsEdrCoverage> parse( const QJsonObject &object, const QString &name = u"Point"_s ) const
    {
      QgsEdrCoverageJsonReader reader( readFile );
      return reader.read( { object, QUrl::fromLocalFile( path( name ) ) } );
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
      const QVector<QgsEdrCoverage> coverages = parse( fixture( name ), name );
      QCOMPARE( coverages.size(), 1 );
      QCOMPARE( coverages.first().cellCount, count );
      QCOMPARE( coverages.first().domainType, name );
      QVERIFY( coverages.first().ranges.contains( u"temperature"_s ) );
    }
    void linkedTiles()
    {
      const QVector<QgsEdrCoverage> coverages = parse( fixture( u"tiled"_s ), u"tiled"_s );
      QCOMPARE( coverages.first().ranges.value( u"temperature"_s ).values, QJsonArray( { 0, 1, 2, 10, 11, 12 } ) );
    }
    void numericStrings()
    {
      QJsonObject object = fixture( u"PointSeries"_s );
      QJsonObject ranges = object.value( u"ranges"_s ).toObject();
      QJsonObject array = ranges.value( u"temperature"_s ).toObject();
      array.insert( u"values"_s, QJsonArray { u"307"_s, u"-1.25e2"_s } );
      ranges.insert( u"temperature"_s, array );
      object.insert( u"ranges"_s, ranges );
      QCOMPARE( parse( object ).first().ranges.value( u"temperature"_s ).values, QJsonArray( { 307, -125 } ) );
      for ( const QString &bad : { u"NaN"_s, u"inf"_s, u"1e400"_s, u"1e-400"_s, u"water"_s } )
      {
        array.insert( u"values"_s, QJsonArray { 0, bad } );
        ranges.insert( u"temperature"_s, array );
        object.insert( u"ranges"_s, ranges );
        QVERIFY_EXCEPTION_THROWN( parse( object ), QgsEdrError );
      }
    }
    void inheritance()
    {
      const QVector<QgsEdrCoverage> coverages = parse( fixture( u"collection"_s ), u"collection"_s );
      QCOMPARE( coverages.size(), 2 );
      QCOMPARE( coverages.first().parameters.value( u"temperature"_s ).toObject().value( u"type"_s ).toString(), u"Parameter"_s );
      QVERIFY( parse( fixture( u"empty"_s ) ).isEmpty() );
    }
    void limitsAndCancellation()
    {
      QgsEdrLimits limits;
      limits.maximumCells = 2;
      QgsEdrCoverageJsonReader limited( {}, limits );
      QVERIFY_EXCEPTION_THROWN( limited.read( { fixture( u"Grid"_s ), QUrl() } ), QgsEdrError );
      QgsFeedback feedback;
      feedback.cancel();
      QgsEdrCoverageJsonReader canceled( {}, {}, &feedback );
      QVERIFY_EXCEPTION_THROWN( canceled.read( { fixture( u"Point"_s ), QUrl() } ), QgsEdrError );
    }
    void linkedDocumentsNeedLoader()
    {
      QgsEdrCoverageJsonReader reader;
      QVERIFY_EXCEPTION_THROWN( reader.read( { fixture( u"tiled"_s ), QUrl::fromLocalFile( path( u"tiled"_s ) ) } ), QgsEdrError );
    }
    void malformedRanges()
    {
      QJsonObject o = fixture( "Grid" );
      QJsonObject ranges = o["ranges"].toObject();
      QJsonObject r = ranges["temperature"].toObject();
      r["shape"] = QJsonArray { 12 };
      ranges["temperature"] = r;
      o["ranges"] = ranges;
      QVERIFY_EXCEPTION_THROWN( parse( o ), QgsEdrError );
      o = fixture( "PointSeries" );
      ranges = o["ranges"].toObject();
      r = ranges["temperature"].toObject();
      r["axisNames"] = QJsonArray();
      r["shape"] = QJsonArray();
      r["values"] = QJsonArray { 4 };
      ranges["temperature"] = r;
      o["ranges"] = ranges;
      QVERIFY_EXCEPTION_THROWN( parse( o ), QgsEdrError );
      o = fixture( "categories" );
      ranges = o["ranges"].toObject();
      r = ranges["temperature"].toObject();
      r["values"] = QJsonArray { 99, 99 };
      ranges["temperature"] = r;
      o["ranges"] = ranges;
      QVERIFY_EXCEPTION_THROWN( parse( o ), QgsEdrError );
    }
    void uriTemplatesAndUntiledDimensions()
    {
      QTemporaryDir dir;
      auto write = [&]( const QString &name, const QJsonObject &object ) {
        QFile f( dir.path() + '/' + name );
        QVERIFY( f.open( QIODevice::WriteOnly ) );
        f.write( QJsonDocument( object ).toJson() );
      };
      QJsonObject object = fixture( "Grid" );
      QJsonObject domain = object["domain"].toObject();
      QJsonObject axes = domain["axes"].toObject();
      axes.remove( "t" );
      axes.remove( "z" );
      domain["axes"] = axes;
      object["domain"] = domain;
      object["ranges"] = QJsonObject {
        { "temperature",
          QJsonObject {
            { "type", "TiledNdArray" },
            { "dataType", "float" },
            { "axisNames", QJsonArray { "y", "x" } },
            { "shape", QJsonArray { 2, 3 } },
            { "tileSets", QJsonArray { QJsonObject { { "tileShape", QJsonArray { QJsonValue( QJsonValue::Null ), 2 } }, { "urlTemplate", "{y,x}.json" } } } }
          } }
      };
      write( "coverage.covjson", object );
      write( "0,0.json", QJsonObject { { "type", "NdArray" }, { "dataType", "float" }, { "axisNames", QJsonArray { "y", "x" } }, { "shape", QJsonArray { 2, 2 } }, { "values", QJsonArray { 0, 1, 10, 11 } } } );
      write( "0,1.json", QJsonObject { { "type", "NdArray" }, { "dataType", "float" }, { "axisNames", QJsonArray { "y", "x" } }, { "shape", QJsonArray { 2, 1 } }, { "values", QJsonArray { 2, 12 } } } );
      QgsEdrCoverageJsonReader reader( readFile );
      QCOMPARE( reader.read( readFile( QUrl::fromLocalFile( dir.path() + u"/coverage.covjson"_s ) ) ).first().ranges.value( u"temperature"_s ).values, QJsonArray( { 0, 1, 2, 10, 11, 12 } ) );
    }
};
QGSTEST_MAIN( TestQgsEdrCoverageJson )
#include "testqgsedrcoveragejson.moc"
