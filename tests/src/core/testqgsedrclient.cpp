/***************************************************************************
    testqgsedrclient.cpp
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
#include "qgsauthmanager.h"
#include "qgsedrclient.h"
#include "qgsedrcoveragejson.h"
#include "qgsedrtestserver.h"
#include "qgstest.h"

#include <QFile>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrlQuery>

using namespace Qt::StringLiterals;

class TestQgsEdrClient : public QObject
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
  private slots:
    void initTestCase()
    {
      QVERIFY( mProfile.isValid() );
      qputenv( "QGIS_AUTH_DB_DIR_PATH", mProfile.path().toUtf8() );
      QgsApplication::init();
      QgsApplication::initQgis();
    }
    void cleanupTestCase() { QgsApplication::exitQgis(); }

    void discovery()
    {
      QgsEdrTestServer server;
      QVERIFY( server.isListening() );
      server.respond = []( const QgsEdrTestServer::Request &request ) {
        if ( request.url.path() == "/catalog"_L1 )
          return QgsEdrTestServer::Response { QJsonObject { { u"collections"_s, QJsonArray { QJsonObject { { u"id"_s, u"weather"_s } } } } } };
        return QgsEdrTestServer::Response { QJsonObject { { u"links"_s, QJsonArray { QJsonObject { { u"rel"_s, u"data"_s }, { u"href"_s, u"/catalog"_s }, { u"type"_s, u"application/json"_s } } } } } };
      };
      QgsEdrQuery query;
      query.root = server.url();
      QgsEdrClient client( query );
      QCOMPARE( client.discover( u"collections"_s ).object.value( u"collections"_s ).toArray().first().toObject().value( u"id"_s ).toString(), u"weather"_s );
      QCOMPARE( server.requests.size(), 2 );
    }

    void paginationAndBounds()
    {
      QgsEdrTestServer server;
      QVERIFY( server.isListening() );
      server.respond = [&]( const QgsEdrTestServer::Request &request ) {
        QgsEdrTestServer::Response response { fixture( u"Point"_s ) };
        if ( !QUrlQuery( request.url ).hasQueryItem( u"page"_s ) )
          response.headers.insert( "Link", "<?page=2>; rel=\"next\"" );
        return response;
      };
      QgsEdrQuery query;
      query.root = server.url( u"/page"_s );
      QgsEdrClient client( query );
      QCOMPARE( client.pages( query.request() ).size(), 2 );
      query.limits.maximumPages = 1;
      QgsEdrClient limited( query );
      QVERIFY_EXCEPTION_THROWN( limited.pages( query.request() ), QgsEdrError );
      query.limits.maximumBytes = 16;
      QgsEdrClient oversized( query );
      QVERIFY_EXCEPTION_THROWN( oversized.get( query.root ), QgsEdrError );
      server.respond = [&]( const QgsEdrTestServer::Request & ) {
        QJsonObject object = fixture( u"Point"_s );
        object.insert( u"links"_s, QJsonArray { QJsonObject { { u"rel"_s, u"next"_s }, { u"href"_s, u"/page"_s } } } );
        return QgsEdrTestServer::Response { object };
      };
      query.limits = QgsEdrLimits();
      QgsEdrClient cyclic( query );
      QVERIFY_EXCEPTION_THROWN( cyclic.pages( query.request() ), QgsEdrError );
    }

    void localDocumentLimits()
    {
      QgsEdrQuery query;
      query.root = QUrl::fromLocalFile( path( u"Point"_s ) );
      query.limits.maximumBytes = 10;
      QgsEdrClient small( query );
      QVERIFY_EXCEPTION_THROWN( small.get( query.root ), QgsEdrError );
      query.root = QUrl::fromLocalFile( path( u"tiled"_s ) );
      query.limits = QgsEdrLimits();
      query.limits.maximumDocuments = 2;
      QgsEdrClient limited( query );
      QgsEdrCoverageJsonReader reader( [&limited]( const QUrl &url ) { return limited.get( url ); }, query.limits );
      QVERIFY_EXCEPTION_THROWN( reader.read( limited.get( query.root ) ), QgsEdrError );
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
      QgsEdrClient client( query );
      QCOMPARE( client.fetch( query.request() ).object.value( u"type"_s ).toString(), u"Coverage"_s );
      QCOMPARE( server.requests.size(), 1 );
      const QgsEdrTestServer::Request request = server.requests.first();
      QCOMPARE( request.method, QByteArray( "POST" ) );
      QCOMPARE( request.url.path(), u"/collections/test/instances/cycle 00/position"_s );
      QVERIFY( request.headers.value( "accept" ).contains( "application/vnd.cov+json" ) );
      QVERIFY( !QUrlQuery( request.url ).hasQueryItem( u"f"_s ) );
      QString datetime;
      if ( encoding == "json"_L1 )
        datetime = QJsonDocument::fromJson( request.body ).object().value( u"datetime"_s ).toString();
      else
        datetime = QUrlQuery( encoding == "form"_L1 ? QString::fromUtf8( request.body ) : request.url.query() ).queryItemValue( u"datetime"_s, QUrl::FullyDecoded );
      QCOMPARE( datetime, query.parameters.value( u"datetime"_s ) );
    }

    void redirectsAndAuthentication()
    {
      QgsEdrTestServer origin;
      QgsEdrTestServer other;
      QVERIFY( origin.isListening() );
      QVERIFY( other.isListening() );
      other.respond = [&]( const QgsEdrTestServer::Request & ) { return QgsEdrTestServer::Response { fixture( u"Point"_s ) }; };
      origin.respond = [&]( const QgsEdrTestServer::Request &request ) {
        if ( request.url.path() == "/loop"_L1 )
          return QgsEdrTestServer::Response { {}, 302, { { "Location", "/loop" } } };
        return QgsEdrTestServer::Response { {}, 302, { { "Location", other.url().toEncoded() } } };
      };
      QgsAuthManager *auth = QgsApplication::authManager();
      QVERIFY( auth->setMasterPassword( u"edr-test-only-password"_s, true ) );
      QgsAuthMethodConfig config;
      config.setName( u"EDR fixture"_s );
      config.setMethod( u"Basic"_s );
      config.setConfig( u"username"_s, u"test-user"_s );
      config.setConfig( u"password"_s, u"test-password"_s );
      QVERIFY( auth->storeAuthenticationConfig( config ) );
      QgsEdrQuery query;
      query.root = origin.url();
      query.authcfg = config.id();
      QgsEdrClient client( query );
      QCOMPARE( client.get( query.root ).object.value( u"type"_s ).toString(), u"Coverage"_s );
      QVERIFY( origin.requests.first().headers.contains( "authorization" ) );
      QVERIFY( !other.requests.first().headers.contains( "authorization" ) );
      QVERIFY( auth->removeAuthenticationConfig( config.id() ) );
      query.authcfg.clear();
      QgsEdrClient loop( query );
      QVERIFY_EXCEPTION_THROWN( loop.get( origin.url( u"/loop"_s ) ), QgsEdrError );
      QVERIFY_EXCEPTION_THROWN( loop.resolve( QUrl::fromLocalFile( path( u"Point"_s ) ).toString(), origin.url() ), QgsEdrError );
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

QGSTEST_MAIN( TestQgsEdrClient )
#include "testqgsedrclient.moc"
