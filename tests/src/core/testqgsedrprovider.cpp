/***************************************************************************
    testqgsedrprovider.cpp
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

#include <functional>

#include "qgsapplication.h"
#include "qgsauthmanager.h"
#include "qgsedrclient.h"
#include "qgsedrprovider.h"
#include "qgsedrquery.h"
#include "qgsfeatureiterator.h"
#include "qgsproject.h"
#include "qgsproviderregistry.h"
#include "qgsrasterblock.h"
#include "qgsrasteridentifyresult.h"
#include "qgstest.h"
#include "qgsvectorlayer.h"

#include <QFile>
#include <QJsonDocument>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrlQuery>

using namespace Qt::StringLiterals;

#include "qgsedrtestserver.h"

class TestQgsEdrProvider : public QObject
{
    Q_OBJECT

    QTemporaryDir mProfile;

    QString path( const QString &name ) const { return QStringLiteral( TEST_DATA_DIR ) + u"/edr/"_s + name + u".covjson"_s; }
    QJsonObject fixture( const QString &name ) const
    {
      QFile file( path( name ) );
      if ( !file.open( QIODevice::ReadOnly ) )
        throw QgsEdrError( u"Missing fixture"_s );
      return QJsonDocument::fromJson( file.readAll() ).object();
    }
    QString localUri( const QString &name ) const { return QgsEdrQuery::encodeUri( { { u"url"_s, QUrl::fromLocalFile( path( name ) ).toString() } } ); }
    int featureCount( QgsFeatureIterator iterator ) const
    {
      QgsFeature feature;
      int count = 0;
      while ( iterator.nextFeature( feature ) )
        ++count;
      return count;
    }

  private slots:
    void initTestCase()
    {
      QVERIFY( mProfile.isValid() );
      qputenv( "QGIS_AUTH_DB_DIR_PATH", mProfile.path().toUtf8() );
      QgsApplication::init();
      QgsApplication::initQgis();
    }
    void cleanupTestCase() { QgsApplication::exitQgis(); }

    void registrationAndFiltering()
    {
      QVERIFY( QgsProviderRegistry::instance()->providerList().contains( u"edr"_s ) );
      QgsVectorLayer layer( localUri( u"Grid"_s ), u"grid"_s, u"edr"_s );
      QVERIFY( layer.isValid() );
      QCOMPARE( layer.featureCount(), 12 );
      QVERIFY( !layer.startEditing() );
      QCOMPARE( featureCount( layer.getFeatures( QgsFeatureRequest().setFilterRect( QgsRectangle( 9, 39, 11, 41 ) ) ) ), 2 );
      QgsFeature feature;
      QVERIFY( layer.getFeatures().nextFeature( feature ) );
      QCOMPARE( featureCount( layer.getFeatures( QgsFeatureRequest( feature.id() ) ) ), 1 );
      QCOMPARE( featureCount( layer.getFeatures( QgsFeatureRequest().setFilterExpression( u"\"temperature\" > 100"_s ) ) ), 4 );
      QVERIFY( layer.setSubsetString( u"\"temperature\" > 100"_s ) );
      QCOMPARE( layer.featureCount(), 4 );
      QgsVectorLayer restored( layer.source(), u"subset"_s, u"edr"_s );
      QVERIFY( restored.isValid() );
      QCOMPARE( restored.featureCount(), 4 );
      QVERIFY( !layer.setSubsetString( u"temperature >"_s ) );
      QCOMPARE( layer.featureCount(), 4 );
    }

    void iteratorLifetimeAndReprojection()
    {
      auto layer = std::make_unique<QgsVectorLayer>( localUri( u"PointSeries"_s ), u"time series"_s, u"edr"_s );
      QVERIFY( layer->isValid() );
      QgsFeatureRequest request;
      request.setDestinationCrs( QgsCoordinateReferenceSystem( u"EPSG:3857"_s ), QgsCoordinateTransformContext() );
      QgsFeature feature;
      QVERIFY( layer->getFeatures( request ).nextFeature( feature ) );
      QGSCOMPARENEAR( feature.geometry().asPoint().x(), -8348961.809495518, 0.001 );
      QgsFeatureIterator iterator = layer->dataProvider()->getFeatures();
      layer.reset();
      QCOMPARE( featureCount( iterator ), 2 );
    }


    void paginationAndLimits()
    {
      QgsEdrTestServer server;
      QVERIFY( server.isListening() );
      server.respond = [&]( const QgsEdrTestServer::Request &request ) {
        const bool second = QUrlQuery( request.url ).hasQueryItem( u"page"_s );
        QgsEdrTestServer::Response response { fixture( u"Point"_s ) };
        if ( !second )
          response.headers.insert( "Link", "<?page=2>; rel=\"next\"" );
        return response;
      };
      QgsEdrQuery query;
      query.root = server.url( u"/page"_s );
      QgsVectorLayer layer( query.toUri(), u"paged"_s, u"edr"_s );
      QVERIFY( layer.isValid() );
      QCOMPARE( layer.featureCount(), 2 );
      QCOMPARE( server.requests.size(), 2 );
      query.limits.maximumPages = 1;
      QgsVectorLayer limited( query.toUri(), u"limited"_s, u"edr"_s );
      QVERIFY( !limited.isValid() );
      query.limits.maximumPages = 100;
      query.limits.maximumBytes = 16;
      QgsVectorLayer oversized( query.toUri(), u"oversized"_s, u"edr"_s );
      QVERIFY( !oversized.isValid() );
    }

    void failuresAndReload()
    {
      QgsEdrTestServer server;
      QVERIFY( server.isListening() );
      int status = 200;
      server.respond = [&]( const QgsEdrTestServer::Request & ) {
        return QgsEdrTestServer::Response { status == 200 ? fixture( u"Point"_s ) : QJsonObject { { u"detail"_s, u"Synthetic failure"_s } }, status };
      };
      QgsVectorLayer layer( server.url().toString(), u"snapshot"_s, u"edr"_s );
      QVERIFY( layer.isValid() );
      for ( int failure : { 400, 500, 202 } )
      {
        status = failure;
        layer.dataProvider()->reloadData();
        QVERIFY( layer.isValid() );
        QCOMPARE( layer.featureCount(), 1 );
        QVERIFY( !layer.dataProvider()->error().isEmpty() );
        QgsVectorLayer invalid( server.url().toString(), u"failed"_s, u"edr"_s );
        QVERIFY( !invalid.isValid() );
      }
      status = 204;
      layer.dataProvider()->reloadData();
      QCOMPARE( layer.featureCount(), 0 );
      QVERIFY( layer.dataProvider()->error().isEmpty() );
    }

    void postEncoding_data()
    {
      QTest::addColumn<QString>( "encoding" );
      QTest::newRow( "form" ) << u"form"_s;
      QTest::newRow( "json" ) << u"json"_s;
      QTest::newRow( "query" ) << u"query"_s;
    }
    void postEncoding()
    {
      QFETCH( QString, encoding );
      QgsEdrTestServer server;
      QVERIFY( server.isListening() );
      server.respond = [&]( const QgsEdrTestServer::Request & ) { return QgsEdrTestServer::Response { fixture( u"Point"_s ) }; };
      QgsEdrQuery query;
      query.root = server.url();
      query.collection = u"test"_s;
      query.instance = u"cycle 00"_s;
      query.kind = u"position"_s;
      query.method = u"POST"_s;
      query.postEncoding = encoding;
      query.parameters = { { u"coords"_s, u"POINT(-75 40)"_s }, { u"datetime"_s, u"2026-01-01T01:00:00+01:00/.."_s } };
      QgsVectorLayer layer( query.toUri(), u"post"_s, u"edr"_s );
      QVERIFY( layer.isValid() );
      QCOMPARE( server.requests.size(), 1 );
      const QgsEdrTestServer::Request request = server.requests.first();
      QCOMPARE( request.method, QByteArray( "POST" ) );
      QCOMPARE( request.url.path(), u"/collections/test/instances/cycle 00/position"_s );
      QVERIFY( request.headers.value( "accept" ).contains( "application/vnd.cov+json" ) );
      QString datetime;
      if ( encoding == "json"_L1 )
        datetime = QJsonDocument::fromJson( request.body ).object().value( u"datetime"_s ).toString();
      else
        datetime = QUrlQuery( encoding == "form"_L1 ? QString::fromUtf8( request.body ) : request.url.query() ).queryItemValue( u"datetime"_s, QUrl::FullyDecoded );
      QCOMPARE( datetime, query.parameters.value( u"datetime"_s ) );
    }

    void redirectsAndLocalFileGuard()
    {
      QgsEdrTestServer server;
      QVERIFY( server.isListening() );
      server.respond = [&]( const QgsEdrTestServer::Request &request ) {
        if ( request.url.path() == "/loop"_L1 )
          return QgsEdrTestServer::Response { {}, 302, { { "Location", "/loop" } } };
        if ( request.url.path() == "/redirect"_L1 )
          return QgsEdrTestServer::Response { {}, 302, { { "Location", "/data" } } };
        QJsonObject object = fixture( u"Point"_s );
        if ( request.url.path() == "/local"_L1 )
          object.insert( u"domain"_s, QUrl::fromLocalFile( path( u"Point"_s ) ).toString() );
        return QgsEdrTestServer::Response { object };
      };
      QgsVectorLayer redirected( server.url( u"/redirect"_s ).toString(), u"redirected"_s, u"edr"_s );
      QVERIFY( redirected.isValid() );
      for ( const QString &path : { u"/loop"_s, u"/local"_s } )
      {
        QgsVectorLayer invalid( server.url( path ).toString(), u"invalid"_s, u"edr"_s );
        QVERIFY( !invalid.isValid() );
      }
    }

    void cancellation()
    {
      QgsEdrTestServer server;
      QVERIFY( server.isListening() );
      server.respond = []( const QgsEdrTestServer::Request & ) { return QgsEdrTestServer::Response { {}, 200, {}, true }; };
      QgsEdrQuery query;
      query.root = server.url();
      QgsFeedback feedback;
      QgsEdrClient client( query, &feedback );
      QTimer::singleShot( 50, &feedback, &QgsFeedback::cancel );
      QVERIFY_EXCEPTION_THROWN( client.get( query.root ), QgsEdrError );
      QVERIFY( feedback.isCanceled() );
    }
};

QGSTEST_MAIN( TestQgsEdrProvider )
#include "testqgsedrprovider.moc"
