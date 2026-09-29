/***************************************************************************
    testqgsedrrasterdata.cpp
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
#include "qgsedrrasterdata.h"

class TestQgsEdrRasterData : public QObject
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
    void rasterAxisOrder()
    {
      QgsEdrQuery q = local( "Grid" );
      QgsEdrRasterData data = qgsEdrRasterData( qgsEdrLoadDataset( q ), q );
      QCOMPARE( data.width, 3 );
      QCOMPARE( data.height, 2 );
      QCOMPARE( data.bands.size(), 2 );
      QCOMPARE( data.extent, QgsRectangle( 5, 35, 35, 55 ) );
      QCOMPARE( data.bands[0].values, QVector<double>( { 10, 11, 12, 0, 1, 2 } ) );
      QCOMPARE( data.bands[1].values[0], 110.0 );
      QVERIFY( std::isnan( data.bands[1].values[5] ) );
      QVERIFY( data.bands[1].name.contains( "t=2026-01-01T01:00:00Z" ) );
      QJsonObject object = fixture( "Grid" );
      QJsonObject domain = object["domain"].toObject();
      QJsonObject axes = domain["axes"].toObject();
      axes["x"] = QJsonObject { { "values", QJsonArray { 30, 20, 10 } } };
      domain["axes"] = axes;
      object["domain"] = domain;
      QgsEdrDataset d;
      d.coverages = parse( object );
      data = qgsEdrRasterData( d, q );
      QCOMPARE( data.bands[0].values, QVector<double>( { 12, 11, 10, 2, 1, 0 } ) );
    }
    void regularAndIrregularAxes()
    {
      QJsonObject o = fixture( "Grid" );
      QJsonObject domain = o["domain"].toObject();
      QJsonObject axes = domain["axes"].toObject();
      axes["x"] = QJsonObject { { "start", 10 }, { "stop", 30 }, { "num", 3 } };
      domain["axes"] = axes;
      o["domain"] = domain;
      QVector<QgsEdrCoverage> c = parse( o );
      QCOMPARE( c[0].axes["x"].values, QJsonArray( { 10, 20, 30 } ) );
      axes["x"] = QJsonObject { { "values", QJsonArray { 10, 15, 30 } } };
      domain["axes"] = axes;
      o["domain"] = domain;
      QgsEdrDataset d;
      d.coverages = parse( o );
      QgsEdrQuery q = local( "Grid" );
      QCOMPARE( qgsEdrVectorData( d, q, {} ).features.size(), 12 );
      QVERIFY_EXCEPTION_THROWN( qgsEdrRasterData( d, q ), QgsEdrError );
    }
    void singletonBoundsOverflow()
    {
      QgsEdrCoverage coverage;
      QgsEdrAxis axis;
      axis.values = QJsonArray { 0 };
      axis.bounds = QJsonArray { -1e308, 1e308 };
      coverage.axes.insert( u"x"_s, axis );
      coverage.axes.insert( u"y"_s, axis );
      QgsEdrDataset dataset;
      dataset.coverages.append( coverage );
      QVERIFY_EXCEPTION_THROWN( qgsEdrRasterData( dataset, QgsEdrQuery() ), QgsEdrError );
    }
    void extremeCoordinates()
    {
      QJsonObject object = fixture( u"Grid"_s );
      QJsonObject domain = object.value( u"domain"_s ).toObject();
      QJsonObject axes = domain.value( u"axes"_s ).toObject();
      axes.insert( u"x"_s, QJsonObject { { u"values"_s, QJsonArray { -1e308, 0, 1e308 } } } );
      domain.insert( u"axes"_s, axes );
      object.insert( u"domain"_s, domain );
      QgsEdrDataset dataset;
      dataset.coverages = parse( object );
      QVERIFY_EXCEPTION_THROWN( qgsEdrRasterData( dataset, local( u"Grid"_s ) ), QgsEdrError );
    }
};
QGSTEST_MAIN( TestQgsEdrRasterData )
#include "testqgsedrrasterdata.moc"
