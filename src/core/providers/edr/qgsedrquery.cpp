/***************************************************************************
    qgsedrquery.cpp
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

#include "qgsedrquery.h"

#include <cmath>

#include "qgspathresolver.h"
#include "qgsreadwritecontext.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QUrlQuery>

using namespace Qt::StringLiterals;

QString qgsEdrResolveFileUri( const QString &uri, const QgsReadWriteContext &context, bool absolute )
{
  QVariantMap parts = QgsEdrQuery::decodeUri( uri );
  const QUrl url( parts.value( u"url"_s ).toString() );
  if ( !url.isLocalFile() )
    return uri;
  const QString path = absolute ? context.pathResolver().readPath( url.toLocalFile() ) : context.pathResolver().writePath( url.toLocalFile() );
  parts[u"url"_s] = QUrl::fromLocalFile( path ).toString();
  return QgsEdrQuery::encodeUri( parts );
}

bool qgsEdrSameOrigin( const QUrl &a, const QUrl &b )
{
  const auto port = []( const QUrl &u ) { return u.port( u.scheme() == "https"_L1 ? 443 : 80 ); };
  return a.scheme() == b.scheme() && a.host() == b.host() && port( a ) == port( b );
}

QUrl qgsEdrLinkUrl( const QJsonObject &doc, const QUrl &base, const QString &rel )
{
  QUrl fallback;
  for ( const QJsonValue &v : doc.value( u"links"_s ).toArray() )
  {
    const QJsonObject link = v.toObject();
    const QString relation = link.value( u"rel"_s ).toString();
    if ( relation != rel && !relation.endsWith( '/' + rel ) )
      continue;
    const QUrl url = base.resolved( QUrl( link.value( u"href"_s ).toString() ) );
    if ( link.value( u"type"_s ).toString().contains( u"json"_s ) )
      return url;
    if ( fallback.isEmpty() )
      fallback = url;
  }
  return fallback;
}

QStringList QgsEdrQuery::queryTypes()
{
  return { u"position"_s, u"radius"_s, u"area"_s, u"cube"_s, u"trajectory"_s, u"corridor"_s, u"items"_s, u"item"_s, u"locations"_s, u"location"_s };
}

QStringList QgsEdrQuery::resources()
{
  return { u"landing"_s, u"api"_s, u"conformance"_s, u"collections"_s, u"collection"_s, u"queryables"_s, u"instances"_s, u"instance"_s };
}

QVariantMap QgsEdrQuery::decodeUri( const QString &uri )
{
  if ( uri.startsWith( "http://"_L1 ) || uri.startsWith( "https://"_L1 ) || uri.startsWith( "file://"_L1 ) )
    return { { u"url"_s, uri } };
  if ( QFileInfo( uri ).exists() )
    return { { u"url"_s, QUrl::fromLocalFile( QFileInfo( uri ).absoluteFilePath() ).toString() } };
  QVariantMap result;
  for ( const QPair<QString, QString> &pair : QUrlQuery( uri ).queryItems( QUrl::FullyDecoded ) )
    result.insert( pair.first, pair.second );
  return result;
}

QString QgsEdrQuery::encodeUri( const QVariantMap &parts )
{
  QStringList pairs;
  for ( auto i = parts.begin(); i != parts.end(); ++i )
    if ( !i.value().toString().isEmpty() )
      pairs << QString::fromLatin1( QUrl::toPercentEncoding( i.key() ) ) + '=' + QString::fromLatin1( QUrl::toPercentEncoding( i.value().toString() ) );
  return pairs.join( '&' );
}

QgsEdrQuery QgsEdrQuery::fromUri( const QString &uri )
{
  QgsEdrQuery q;
  QVariantMap p = decodeUri( uri );
  q.root = QUrl( p.take( u"url"_s ).toString() );
  q.collection = p.take( u"collection"_s ).toString();
  q.instance = p.take( u"instance"_s ).toString();
  q.kind = p.take( u"query"_s ).toString();
  q.id = p.take( u"id"_s ).toString();
  q.href = QUrl( p.take( u"href"_s ).toString() );
  q.authcfg = p.take( u"authcfg"_s ).toString();
  q.geometry = p.take( u"geometry"_s ).toString();
  q.coverage = p.take( u"coverage"_s ).toString();
  q.targetCrs = p.take( u"targetCrs"_s ).toString();
  q.subset = p.take( u"subset"_s ).toString();
  if ( p.contains( u"method"_s ) )
    q.method = p.take( u"method"_s ).toString().toUpper();
  if ( p.contains( u"postEncoding"_s ) )
    q.postEncoding = p.take( u"postEncoding"_s ).toString();
  auto limit = [&p]( const QString &name, qint64 def ) {
    if ( !p.contains( name ) )
      return def;
    bool ok = false;
    const qint64 v = p.take( name ).toString().toLongLong( &ok );
    if ( !ok || v < 1 || v > 2147483647 )
      throw QgsEdrError( QObject::tr( "Invalid positive limit: %1" ).arg( name ) );
    return v;
  };
  q.limits.maximumCells = limit( u"maxCells"_s, q.limits.maximumCells );
  q.limits.maximumBytes = limit( u"maxBytes"_s, q.limits.maximumBytes );
  q.limits.maximumPages = int( limit( u"maxPages"_s, q.limits.maximumPages ) );
  q.limits.maximumDocuments = int( limit( u"maxDocuments"_s, q.limits.maximumDocuments ) );
  for ( auto i = p.begin(); i != p.end(); ++i )
    q.parameters.insert( i.key().startsWith( "p."_L1 ) ? i.key().mid( 2 ) : i.key(), i.value().toString() );
  q.validate();
  return q;
}

QString QgsEdrQuery::toUri() const
{
  QVariantMap p {
    { u"url"_s, root.toString() },
    { u"collection"_s, collection },
    { u"instance"_s, instance },
    { u"query"_s, kind },
    { u"id"_s, id },
    { u"href"_s, href.toString() },
    { u"authcfg"_s, authcfg },
    { u"geometry"_s, geometry },
    { u"coverage"_s, coverage },
    { u"targetCrs"_s, targetCrs },
    { u"subset"_s, subset },
    { u"method"_s, method },
    { u"postEncoding"_s, postEncoding },
    { u"maxCells"_s, limits.maximumCells },
    { u"maxPages"_s, limits.maximumPages },
    { u"maxBytes"_s, limits.maximumBytes },
    { u"maxDocuments"_s, limits.maximumDocuments }
  };
  for ( auto i = parameters.begin(); i != parameters.end(); ++i )
    p.insert( u"p."_s + i.key(), i.value() );
  return encodeUri( p );
}

void QgsEdrQuery::validate() const
{
  if ( !root.isValid() || !QStringList { u"http"_s, u"https"_s, u"file"_s }.contains( root.scheme() ) )
    throw QgsEdrError( QObject::tr( "url must be an absolute HTTP(S) URL or file URL" ) );
  if ( root.scheme() != "file"_L1 && root.host().isEmpty() )
    throw QgsEdrError( QObject::tr( "The service URL has no host" ) );
  if ( method != "GET" && method != "POST" )
    throw QgsEdrError( QObject::tr( "Only GET and POST are supported" ) );
  if ( postEncoding != "form"_L1 && postEncoding != "json"_L1 && postEncoding != "query"_L1 )
    throw QgsEdrError( QObject::tr( "postEncoding must be form, json or query, as specified by the service OpenAPI definition" ) );
  if ( !kind.isEmpty() && !resources().contains( kind ) && !queryTypes().contains( kind ) )
    throw QgsEdrError( QObject::tr( "Unknown EDR query: %1" ).arg( kind ) );
  if ( ( !kind.isEmpty() && !QStringList { u"landing"_s, u"api"_s, u"conformance"_s, u"collections"_s }.contains( kind ) ) && collection.isEmpty() )
    throw QgsEdrError( QObject::tr( "This resource requires a collection identifier" ) );
  if ( ( kind == "item"_L1 || kind == "location"_L1 ) && id.isEmpty() )
    throw QgsEdrError( QObject::tr( "An item or location identifier is required" ) );
  if ( kind == "instance"_L1 && instance.isEmpty() )
    throw QgsEdrError( QObject::tr( "An instance identifier is required" ) );
  if ( !geometry.isEmpty() && geometry != "point"_L1 && geometry != "polygon"_L1 )
    throw QgsEdrError( QObject::tr( "geometry must be point or polygon" ) );
  for ( const QString &s : { collection, instance, id } )
    if ( s == "."_L1 || s == ".."_L1 )
      throw QgsEdrError( QObject::tr( "Dot path segments are not valid resource identifiers" ) );
  auto require = [this]( const QString &key ) {
    if ( parameters.value( key ).trimmed().isEmpty() )
      throw QgsEdrError( QObject::tr( "%1 requires %2" ).arg( kind, key ) );
  };
  const bool spatial = QStringList { u"position"_s, u"radius"_s, u"area"_s, u"trajectory"_s, u"corridor"_s }.contains( kind );
  if ( spatial )
  {
    require( u"coords"_s );
    const QString wkt = parameters.value( u"coords"_s ).trimmed().toUpper();
    QString allowed = kind == "area"_L1 ? u"(?:POLYGON|MULTIPOLYGON)"_s : ( kind == "trajectory"_L1 || kind == "corridor"_L1 ) ? u"LINESTRING"_s : u"(?:POINT|MULTIPOINT)"_s;
    if ( !QRegularExpression( u"^"_s + allowed + u"\\s*(?:ZM|Z|M)?\\s*\\("_s ).match( wkt ).hasMatch() )
      throw QgsEdrError( QObject::tr( "Invalid geometry type for %1" ).arg( kind ) );
    // EDR's M ordinates may be RFC3339 timestamps, so a Simple Features WKT parser is not used here.
    if ( wkt.count( '(' ) != wkt.count( ')' ) || !wkt.endsWith( ')' ) )
      throw QgsEdrError( QObject::tr( "Unbalanced coords geometry" ) );
  }
  if ( kind == "radius"_L1 )
  {
    require( u"within"_s );
    require( u"within-units"_s );
  }
  if ( kind == "corridor"_L1 )
    for ( const QString &key : { u"corridor-width"_s, u"width-units"_s, u"corridor-height"_s, u"height-units"_s } )
      require( key );
  // Services differ on cube's vertical default (including 2-D collections).
  // Preserve an explicit z filter, and let the service decide when it is required.
  if ( kind == "cube"_L1 )
    require( u"bbox"_s );
  if ( parameters.contains( u"bbox"_s ) )
  {
    const QStringList values = parameters.value( u"bbox"_s ).split( ',' );
    if ( values.size() != 4 && values.size() != 6 )
      throw QgsEdrError( QObject::tr( "bbox requires four or six numbers" ) );
    for ( const QString &v : values )
    {
      bool ok;
      const double d = v.toDouble( &ok );
      if ( !ok || !std::isfinite( d ) )
        throw QgsEdrError( QObject::tr( "Invalid bbox coordinate" ) );
    }
    const int half = values.size() / 2;
    for ( int i = 1; i < half; ++i )
      if ( values[i].toDouble() > values[i + half].toDouble() )
        throw QgsEdrError( QObject::tr( "bbox minimum exceeds maximum" ) );
    // A longitude interval may cross the antimeridian.
  }
  for ( const QString &key : { u"within"_s, u"corridor-width"_s, u"corridor-height"_s, u"limit"_s, u"resolution-x"_s, u"resolution-y"_s, u"resolution-z"_s } )
  {
    if ( !parameters.contains( key ) )
      continue;
    bool ok;
    const double v = parameters.value( key ).toDouble( &ok );
    const bool resolution = QString( key ).startsWith( "resolution-"_L1 );
    if ( !ok || !std::isfinite( v ) || ( resolution ? v < 0 || std::floor( v ) != v : v <= 0 ) )
      throw QgsEdrError( QObject::tr( "Invalid positive value for %1" ).arg( QString( key ) ) );
    if ( QString( key ) == "limit"_L1 && std::floor( v ) != v )
      throw QgsEdrError( QObject::tr( "limit must be an integer" ) );
  }
}

QUrl QgsEdrQuery::endpoint() const
{
  if ( !href.isEmpty() )
    return root.resolved( href );
  if ( kind.isEmpty() || kind == "landing"_L1 )
    return root;
  QString path = root.path( QUrl::FullyEncoded );
  while ( path.endsWith( '/' ) )
    path.chop( 1 );
  auto add = [&path]( const QString &s ) { path += '/' + QString::fromLatin1( QUrl::toPercentEncoding( s ) ); };
  if ( QStringList { u"api"_s, u"conformance"_s, u"collections"_s }.contains( kind ) )
    add( kind );
  else
  {
    add( u"collections"_s );
    add( collection );
    if ( !instance.isEmpty() )
    {
      add( u"instances"_s );
      add( instance );
    }
    if ( kind == "instances"_L1 )
      add( u"instances"_s );
    else if ( kind == "item"_L1 || kind == "location"_L1 )
    {
      add( kind == "item"_L1 ? u"items"_s : u"locations"_s );
      add( id );
    }
    else if ( kind != "collection"_L1 && kind != "instance"_L1 )
      add( kind );
  }
  QUrl url = root;
  url.setPath( path, QUrl::StrictMode );
  url.setFragment( QString() );
  return url;
}

QgsEdrRequest QgsEdrQuery::request() const
{
  validate();
  QgsEdrRequest r;
  r.url = endpoint();
  r.method = method.toLatin1();
  QMap<QString, QString> p = parameters;
  // Format aliases are service-specific. Data requests negotiate with Accept unless
  // the caller selected an advertised f value; discovery documents request JSON.
  if ( resources().contains( kind ) && !p.contains( u"f"_s ) )
    p.insert( u"f"_s, u"json"_s );
  QVariantMap values;
  for ( auto i = p.begin(); i != p.end(); ++i )
    values.insert( i.key(), i.value() );
  const QByteArray encoded = encodeUri( values ).toUtf8();
  if ( method == "POST" && postEncoding != "query"_L1 )
  {
    if ( postEncoding == "json"_L1 )
    {
      r.body = QJsonDocument( QJsonObject::fromVariantMap( values ) ).toJson( QJsonDocument::Compact );
      r.contentType = "application/json";
    }
    else
    {
      r.body = encoded;
      r.contentType = "application/x-www-form-urlencoded";
    }
  }
  else
  {
    if ( method == "POST" )
      r.contentType = "application/x-www-form-urlencoded";
    QUrlQuery query( r.url );
    for ( auto i = p.begin(); i != p.end(); ++i )
    {
      query.removeAllQueryItems( i.key() );
      query.addQueryItem( i.key(), i.value() );
    }
    // QUrlQuery keeps literal '+', but many HTTP servers decode it as a space.
    r.url.setQuery( query.query( QUrl::FullyEncoded ).replace( '+', "%2B"_L1 ), QUrl::StrictMode );
  }
  return r;
}
