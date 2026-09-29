/***************************************************************************
    qgsedrclient.cpp
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

#include "qgsedrclient.h"

#include "qgsapplication.h"
#include "qgsauthmanager.h"
#include "qgsnetworkaccessmanager.h"
#include "qgssetrequestinitiator_p.h"

#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QString>
#include <QTimer>

using namespace Qt::StringLiterals;

QgsEdrClient::QgsEdrClient( const QgsEdrQuery &q, QgsFeedback *feedback, bool refresh )
  : mQuery( q )
  , mFeedback( feedback )
  , mRefresh( refresh )
{}

void QgsEdrClient::checkCanceled() const
{
  if ( mFeedback && mFeedback->isCanceled() )
    throw QgsEdrError( QObject::tr( "EDR request canceled" ) );
}

QgsEdrDocument QgsEdrClient::get( const QUrl &url )
{
  QgsEdrRequest r;
  r.url = url;
  return fetch( r );
}

QgsEdrDocument QgsEdrClient::resolve( const QJsonValue &value, const QUrl &base )
{
  if ( value.isObject() )
    return { value.toObject(), base };
  if ( !value.isString() || value.toString().isEmpty() )
    throw QgsEdrError( QObject::tr( "Expected a JSON object or linked document URL" ) );
  const QUrl url = base.resolved( QUrl( value.toString() ) );
  if ( !base.isLocalFile() && url.isLocalFile() )
    throw QgsEdrError( QObject::tr( "A remote document cannot reference a local file" ) );
  return get( url );
}

QgsEdrDocument QgsEdrClient::fetch( const QgsEdrRequest &initial )
{
  checkCanceled();
  const QString key = initial.method + ' ' + initial.url.toString( QUrl::FullyEncoded ) + ' ' + initial.body.toHex();
  if ( mCache.contains( key ) )
    return mCache.value( key );
  if ( ++mDocuments > mQuery.limits.maximumDocuments )
    throw QgsEdrError( QObject::tr( "Document limit exceeded; narrow the query or increase maxDocuments" ) );
  QgsEdrRequest r = initial;
  QByteArray data;
  QUrl finalUrl;
  QSet<QUrl> visited;
  for ( int redirect = 0;; ++redirect )
  {
    checkCanceled();
    if ( redirect > 8 || visited.contains( r.url ) )
      throw QgsEdrError( QObject::tr( "HTTP redirect loop or redirect limit exceeded" ) );
    visited.insert( r.url );
    if ( r.url.isLocalFile() )
    {
      if ( !mQuery.root.isLocalFile() )
        throw QgsEdrError( QObject::tr( "HTTP services cannot access local files" ) );
      QFile file( r.url.toLocalFile() );
      if ( !file.open( QIODevice::ReadOnly ) )
        throw QgsEdrError( QObject::tr( "Cannot open CoverageJSON file: %1" ).arg( file.errorString() ) );
      if ( file.size() > mQuery.limits.maximumBytes - mBytes )
        throw QgsEdrError( QObject::tr( "Response byte limit exceeded" ) );
      data = file.readAll();
      finalUrl = r.url;
      mBytes += data.size();
      break;
    }
    if ( ( r.url.scheme() != "https"_L1 && r.url.scheme() != "http"_L1 ) || r.url.host().isEmpty() )
      throw QgsEdrError( QObject::tr( "Only HTTP(S) links are supported" ) );
    QNetworkRequest request( r.url );
    request.setRawHeader( "Accept", r.accept );
    QgsSetRequestInitiatorClass( request, u"QgsEdrClient"_s );
    request.setAttribute( QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy );
    request.setAttribute( QNetworkRequest::CacheLoadControlAttribute, mRefresh ? QNetworkRequest::AlwaysNetwork : QNetworkRequest::PreferNetwork );
    if ( !r.contentType.isEmpty() )
      request.setHeader( QNetworkRequest::ContentTypeHeader, r.contentType );
    const QString auth = qgsEdrSameOrigin( mQuery.root, r.url ) ? mQuery.authcfg : QString();
    if ( !auth.isEmpty() && !QgsApplication::authManager()->updateNetworkRequest( request, auth ) )
      throw QgsEdrError( QObject::tr( "QGIS authentication configuration failed" ) );
    QgsNetworkAccessManager *manager = QgsNetworkAccessManager::instance();
    QNetworkReply *reply = r.method == "POST" ? manager->post( request, r.body ) : manager->get( request );
    if ( !auth.isEmpty() && !QgsApplication::authManager()->updateNetworkReply( reply, auth ) )
    {
      reply->abort();
      reply->deleteLater();
      throw QgsEdrError( QObject::tr( "QGIS reply authentication failed" ) );
    }
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot( true );
    bool timedOut = false, oversized = false;
    data.clear();
    auto read = [&]() {
      data += reply->readAll();
      if ( data.size() > mQuery.limits.maximumBytes - mBytes )
      {
        oversized = true;
        reply->abort();
      }
    };
    QObject::connect( reply, &QIODevice::readyRead, &loop, read );
    QObject::connect( reply, &QNetworkReply::finished, &loop, &QEventLoop::quit );
    QObject::connect( &timer, &QTimer::timeout, &loop, [&]() {
      timedOut = true;
      reply->abort();
      loop.quit();
    } );
    if ( mFeedback )
      QObject::connect( mFeedback, &QgsFeedback::canceled, reply, &QNetworkReply::abort );
    timer.start( 60000 );
    if ( !reply->isFinished() )
      loop.exec( QEventLoop::ExcludeUserInputEvents );
    timer.stop();
    read();
    const int status = reply->attribute( QNetworkRequest::HttpStatusCodeAttribute ).toInt();
    const QUrl location = r.url.resolved( QUrl( QString::fromUtf8( reply->rawHeader( "Location" ) ) ) );
    const bool hasLocation = !reply->rawHeader( "Location" ).isEmpty();
    const QByteArray linkHeader = reply->rawHeader( "Link" );
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString message = reply->errorString();
    reply->deleteLater();
    checkCanceled();
    if ( timedOut )
      throw QgsEdrError( QObject::tr( "EDR request timed out after 60 seconds" ) );
    if ( oversized )
      throw QgsEdrError( QObject::tr( "Response byte limit exceeded; narrow the query or increase maxBytes" ) );
    mBytes += data.size();
    if ( status == 301 || status == 302 || status == 303 || status == 307 || status == 308 )
    {
      if ( !hasLocation )
        throw QgsEdrError( QObject::tr( "Redirect response has no Location header" ) );
      if ( r.url.scheme() == "https"_L1 && location.scheme() == "http"_L1 )
        throw QgsEdrError( QObject::tr( "Refusing HTTPS to HTTP redirect" ) );
      r.url = location;
      if ( status == 303 || ( ( status == 301 || status == 302 ) && r.method == "POST" ) )
      {
        r.method = "GET";
        r.body.clear();
        r.contentType.clear();
      }
      continue;
    }
    if ( status == 202 )
      throw QgsEdrError(
        QObject::tr( "The service accepted an asynchronous job. Open its status URL to retrieve the completed result: %1" ).arg( ( hasLocation ? location.toString() : r.url.toString() ) )
      );
    if ( status == 204 )
      data = "{\"type\":\"FeatureCollection\",\"features\":[]}";
    if ( status >= 400 || networkError != QNetworkReply::NoError )
    {
      const QJsonObject problem = QJsonDocument::fromJson( data ).object();
      QString detail = problem.value( u"detail"_s ).toString( problem.value( u"description"_s ).toString( problem.value( u"title"_s ).toString( message ) ) );
      throw QgsEdrError( QObject::tr( "HTTP %1: %2" ).arg( status ).arg( detail.left( 1000 ) ) );
    }
    finalUrl = r.url;
    // OGC APIs may put pagination links in either the document or HTTP Link headers.
    if ( !linkHeader.isEmpty() )
    {
      QJsonDocument document = QJsonDocument::fromJson( data );
      if ( document.isObject() )
      {
        QJsonObject object = document.object();
        QJsonArray links = object.value( u"links"_s ).toArray();
        QRegularExpression expression( u"<([^>]+)>\\s*;[^,]*?rel=\"?([^\";, ]+)\"?"_s );
        QRegularExpressionMatchIterator matches = expression.globalMatch( QString::fromUtf8( linkHeader ) );
        while ( matches.hasNext() )
        {
          QRegularExpressionMatch m = matches.next();
          links.append( QJsonObject { { u"href"_s, m.captured( 1 ) }, { u"rel"_s, m.captured( 2 ) } } );
        }
        object.insert( u"links"_s, links );
        data = QJsonDocument( object ).toJson( QJsonDocument::Compact );
      }
    }
    break;
  }
  QJsonParseError error;
  const QJsonDocument json = QJsonDocument::fromJson( data, &error );
  if ( error.error != QJsonParseError::NoError || !json.isObject() )
    throw QgsEdrError( QObject::tr( "Expected a JSON object; the service may have returned HTML or an unsupported format: %1" ).arg( error.errorString() ) );
  QgsEdrDocument result { json.object(), finalUrl };
  mCache.insert( key, result );
  return result;
}

QVector<QgsEdrDocument> QgsEdrClient::pages( const QgsEdrRequest &request )
{
  QVector<QgsEdrDocument> result;
  QgsEdrRequest r = request;
  QSet<QUrl> seen;
  while ( true )
  {
    checkCanceled();
    if ( seen.contains( r.url ) )
      throw QgsEdrError( QObject::tr( "Pagination cycle detected" ) );
    if ( result.size() >= mQuery.limits.maximumPages )
      throw QgsEdrError( QObject::tr( "Page limit exceeded; results have not been silently truncated" ) );
    seen.insert( r.url );
    QgsEdrDocument doc = fetch( r );
    result.append( doc );
    const QUrl next = qgsEdrLinkUrl( doc.object, doc.url, u"next"_s );
    if ( next.isEmpty() )
      break;
    r = QgsEdrRequest();
    r.url = next;
  }
  return result;
}

QgsEdrDocument QgsEdrClient::discover( const QString &kind )
{
  QgsEdrQuery q = mQuery;
  q.kind = u"landing"_s;
  q.parameters.clear();
  q.href = QUrl();
  q.method = "GET";
  if ( kind == "landing"_L1 )
    return fetch( q.request() );
  const QgsEdrDocument landing = fetch( q.request() );
  QString rel = kind == "api"_L1 ? u"service-desc"_s : kind == "collections"_L1 ? u"data"_s : u"conformance"_s;
  const QUrl link = qgsEdrLinkUrl( landing.object, landing.url, rel );
  q.kind = kind;
  if ( !link.isEmpty() )
    q.href = link;
  return fetch( q.request() );
}
