/***************************************************************************
    testqgsedrquery.cpp
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
#include "qgsedrquery.h"
#include "qgsgeometry.h"
#include "qgstest.h"

#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QUrlQuery>

using namespace Qt::StringLiterals;

class TestQgsEdrQuery : public QObject
{
    Q_OBJECT
    QgsEdrQuery query( const QString &kind ) const
    {
      QgsEdrQuery q;
      q.root = QUrl( "https://example.test/edr?api_key=a%2Bb" );
      q.collection = "weather/surface";
      q.kind = kind;
      if ( kind == "item" || kind == "location" )
        q.id = "station/one";
      if ( kind == "instance" )
        q.instance = "2026-01-01T00:00:00Z";
      if ( kind == "position" || kind == "radius" )
        q.parameters["coords"] = "MULTIPOINT((1 2),(3 4))";
      if ( kind == "area" )
        q.parameters["coords"] = "MULTIPOLYGON(((0 0,1 0,1 1,0 0)))";
      if ( kind == "cube" )
      {
        q.parameters["bbox"] = "170,-20,-170,20";
        q.parameters["z"] = "1000/500";
      }
      if ( kind == "trajectory" || kind == "corridor" )
        q.parameters["coords"] = "LINESTRINGZM(1 2 3 2026-01-01T00:00:00Z,4 5 6 2026-01-01T01:00:00Z)";
      if ( kind == "radius" )
      {
        q.parameters["within"] = "5";
        q.parameters["within-units"] = "km";
      }
      if ( kind == "corridor" )
      {
        q.parameters["corridor-width"] = "5";
        q.parameters["width-units"] = "km";
        q.parameters["corridor-height"] = "100";
        q.parameters["height-units"] = "m";
      }
      return q;
    }

  private slots:
    void initTestCase()
    {
      QgsApplication::init();
      QgsApplication::initQgis();
    }
    void cleanupTestCase() { QgsApplication::exitQgis(); }
    void endpoints_data()
    {
      QTest::addColumn<QString>( "kind" );
      for ( const QString &k : QgsEdrQuery::resources() + QgsEdrQuery::queryTypes() )
        QTest::newRow( qPrintable( k ) ) << k;
    }
    void endpoints()
    {
      QFETCH( QString, kind );
      QgsEdrQuery q = query( kind );
      QgsEdrRequest r = q.request();
      QCOMPARE( r.method, QByteArray( "GET" ) );
      QVERIFY( r.url.toString( QUrl::FullyEncoded ).contains( "api_key=a%2Bb" ) );
      if ( !QStringList { "landing", "api", "conformance", "collections" }.contains( kind ) )
        QVERIFY( r.url.path( QUrl::FullyEncoded ).contains( "weather%2Fsurface" ) );
      if ( kind == "item" || kind == "location" )
        QVERIFY( r.url.path( QUrl::FullyEncoded ).endsWith( "station%2Fone" ) );
      if ( QgsEdrQuery::queryTypes().contains( kind ) )
      {
        QVERIFY( !QUrlQuery( r.url ).hasQueryItem( "f" ) );
        q.instance = "cycle 00";
        QVERIFY( q.request().url.path( QUrl::FullyEncoded ).contains( "/instances/cycle%2000/" ) );
      }
    }
    void uriRoundTrip()
    {
      QgsEdrQuery q = query( "position" );
      q.parameters["datetime"] = "2026-01-01T01:00:00+01:00/..";
      q.parameters["model:member"] = "R10/0/1";
      q.authcfg = "abc1234";
      q.subset = "temperature > 3";
      QgsEdrQuery other = QgsEdrQuery::fromUri( q.toUri() );
      QCOMPARE( other.toUri(), q.toUri() );
      QCOMPARE( other.parameters, q.parameters );
      QCOMPARE( other.subset, q.subset );
      QVERIFY( other.request().url.toEncoded().contains( "%2B01:00" ) );
      QCOMPARE( QUrlQuery( other.request().url ).queryItemValue( "datetime", QUrl::FullyDecoded ), q.parameters["datetime"] );
    }
    void postEncodings()
    {
      QgsEdrQuery q = query( "position" );
      q.method = "POST";
      QgsEdrRequest r = q.request();
      QVERIFY( !r.body.isEmpty() );
      QVERIFY( !QUrlQuery( r.url ).hasQueryItem( "coords" ) );
      QCOMPARE( r.contentType, QByteArray( "application/x-www-form-urlencoded" ) );
      QCOMPARE( QUrlQuery( QString::fromUtf8( r.body ) ).queryItemValue( "coords", QUrl::FullyDecoded ), q.parameters["coords"] );
      q.postEncoding = "json";
      r = q.request();
      QCOMPARE( QJsonDocument::fromJson( r.body ).object()["coords"].toString(), q.parameters["coords"] );
      q.postEncoding = "query";
      r = q.request();
      QVERIFY( r.body.isEmpty() );
      QVERIFY( QUrlQuery( r.url ).hasQueryItem( "coords" ) );
    }
    void requiredParameters_data()
    {
      QTest::addColumn<QString>( "kind" );
      QTest::addColumn<QString>( "missing" );
      for ( const char *k : { "position", "radius", "area", "trajectory", "corridor" } )
        QTest::newRow( k ) << QString( k ) << QString( "coords" );
      QTest::newRow( "radius-size" ) << QString( "radius" ) << QString( "within" );
      QTest::newRow( "radius-unit" ) << QString( "radius" ) << QString( "within-units" );
      QTest::newRow( "cube-bbox" ) << QString( "cube" ) << QString( "bbox" );
      for ( const char *key : { "corridor-width", "width-units", "corridor-height", "height-units" } )
        QTest::newRow( key ) << QString( "corridor" ) << QString( key );
    }
    void requiredParameters()
    {
      QFETCH( QString, kind );
      QFETCH( QString, missing );
      QgsEdrQuery q = query( kind );
      q.parameters.remove( missing );
      QVERIFY_EXCEPTION_THROWN( q.validate(), QgsEdrError );
    }
    void invalidQueries()
    {
      QgsEdrQuery q = query( "cube" );
      q.parameters["bbox"] = "1,2,3";
      QVERIFY_EXCEPTION_THROWN( q.validate(), QgsEdrError );
      q = query( "radius" );
      q.parameters["within"] = "-1";
      QVERIFY_EXCEPTION_THROWN( q.validate(), QgsEdrError );
      q = query( "position" );
      q.parameters["coords"] = "POLYGON((0 0,1 0,1 1,0 0))";
      QVERIFY_EXCEPTION_THROWN( q.validate(), QgsEdrError );
      q = query( "position" );
      q.parameters["resolution-x"] = "0";
      q.validate();
      q.parameters["limit"] = "2.2";
      QVERIFY_EXCEPTION_THROWN( q.validate(), QgsEdrError );
      QVERIFY_EXCEPTION_THROWN( QgsEdrQuery::fromUri( "url=file%3A%2F%2F%2Ftmp%2Ftest&maxCells=-1" ), QgsEdrError );
    }
    void cubeDefaultLevels()
    {
      QgsEdrQuery q = query( "cube" );
      q.parameters.remove( "z" );
      q.validate();
      QVERIFY( !QUrlQuery( q.request().url ).hasQueryItem( "z" ) );
    }
};
QGSTEST_MAIN( TestQgsEdrQuery )
#include "testqgsedrquery.moc"
