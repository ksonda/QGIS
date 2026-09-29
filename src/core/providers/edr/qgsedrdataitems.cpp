/***************************************************************************
    qgsedrdataitems.cpp
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

#include "qgsedrdataitems.h"

#include "qgsdatacollectionitem.h"
#include "qgsedrclient.h"
#include "qgslayeritem.h"
#include "qgssettings.h"

#include <QJsonArray>
#include <QString>

using namespace Qt::StringLiterals;

class QgsEdrDataItem final : public QgsDataCollectionItem
{
  public:
    enum Kind
    {
      Root,
      Service,
      Collection,
      Instances,
      Instance
    };
    QgsEdrDataItem( QgsDataItem *parent, const QString &name, const QString &path, Kind kind, QgsEdrQuery query = {} )
      : QgsDataCollectionItem( parent, name, path, u"edr"_s )
      , mKind( kind )
      , mQuery( std::move( query ) )
    {
      setIconName( u"mIconConnect.svg"_s );
      setCapabilities( Qgis::BrowserItemCapability::Fertile );
      if ( kind == Root )
        setToolTip( QObject::tr( "Add or save EDR connections in Data Source Manager > EDR, then refresh this entry." ) );
    }
    QVector<QgsDataItem *> createChildren() override
    {
      QVector<QgsDataItem *> children;
      try
      {
        if ( mKind == Root )
        {
          QgsSettings settings;
          settings.beginGroup( u"edr/connections"_s );
          for ( const QString &name : settings.childGroups() )
          {
            QgsEdrQuery q;
            q.root = QUrl( settings.value( name + u"/url"_s ).toString() );
            q.authcfg = settings.value( name + u"/authcfg"_s ).toString();
            children.append( new QgsEdrDataItem( this, name, path() + '/' + name, Service, q ) );
          }
          return children;
        }
        QgsEdrClient client( mQuery );
        if ( mKind == Service || mKind == Instances )
        {
          QgsEdrRequest request;
          if ( mKind == Service )
            request.url = client.discover( u"collections"_s ).url;
          else
          {
            QgsEdrQuery q = mQuery;
            q.kind = u"instances"_s;
            request = q.request();
          }
          for ( const QgsEdrDocument &doc : client.pages( request ) )
          {
            const QString key = mKind == Service ? u"collections"_s : u"instances"_s;
            for ( const QJsonValue &v : doc.object.value( key ).toArray() )
            {
              const QJsonObject o = v.toObject();
              const QString id = o.value( u"id"_s ).toString();
              if ( id.isEmpty() )
                continue;
              QgsEdrQuery q = mQuery;
              if ( mKind == Service )
              {
                q.collection = id;
                q.kind = u"collection"_s;
              }
              else
              {
                q.instance = id;
                q.kind = u"instance"_s;
              }
              q.href = qgsEdrLinkUrl( o, doc.url, u"self"_s );
              const QString title = o.value( u"title"_s ).toString( id );
              children.append( new QgsEdrDataItem( this, title, path() + '/' + QString::fromLatin1( QUrl::toPercentEncoding( id ) ), mKind == Service ? Collection : Instance, q ) );
            }
          }
          return children;
        }
        QgsEdrQuery q = mQuery;
        q.kind = mKind == Instance ? u"instance"_s : u"collection"_s;
        const QgsEdrDocument doc = client.fetch( q.request() );
        const QJsonObject queries = doc.object.value( u"data_queries"_s ).toObject();
        for ( auto i = queries.begin(); i != queries.end(); ++i )
        {
          if ( i.key() == "instances"_L1 )
            continue;
          if ( i.key() == "items"_L1 || i.key() == "locations"_L1 )
          {
            QgsEdrQuery layerQuery = mQuery;
            layerQuery.kind = i.key();
            const QString href = i.value().toObject().value( u"link"_s ).toObject().value( u"href"_s ).toString();
            layerQuery.href = href.isEmpty() || href.contains( '{' ) ? QUrl() : doc.url.resolved( QUrl( href ) );
            children.append( new QgsLayerItem( this, i.key(), path() + '/' + i.key(), layerQuery.toUri(), Qgis::BrowserLayerType::Vector, u"edr"_s ) );
          }
          else
          {
            QgsDataCollectionItem *item = new QgsDataCollectionItem( this, i.key(), path() + '/' + i.key(), u"edr"_s );
            item->setState( Qgis::BrowserItemState::Populated );
            item->setToolTip( QObject::tr( "Configure this query in Data Source Manager > EDR.\n" ) + qgsEdrCompactJson( i.value() ) );
            children.append( item );
          }
        }
        if ( mKind == Collection && ( queries.contains( u"instances"_s ) || !qgsEdrLinkUrl( doc.object, doc.url, u"instances"_s ).isEmpty() ) )
        {
          QgsEdrQuery instanceQuery = mQuery;
          instanceQuery.href = qgsEdrLinkUrl( doc.object, doc.url, u"instances"_s );
          if ( instanceQuery.href.isEmpty() )
          {
            const QString href = queries.value( u"instances"_s ).toObject().value( u"link"_s ).toObject().value( u"href"_s ).toString();
            if ( !href.isEmpty() )
              instanceQuery.href = doc.url.resolved( QUrl( href ) );
          }
          children.append( new QgsEdrDataItem( this, QObject::tr( "Instances" ), path() + u"/instances"_s, Instances, instanceQuery ) );
        }
      }
      catch ( const std::exception &e )
      {
        children.append( new QgsErrorItem( this, QString::fromUtf8( e.what() ), path() + u"/error"_s ) );
      }
      return children;
    }

  private:
    Kind mKind;
    QgsEdrQuery mQuery;
};

QgsDataItem *QgsEdrDataItemProvider::createDataItem( const QString &path, QgsDataItem *parent )
{
  return path.isEmpty() || path == "edr:"_L1 ? new QgsEdrDataItem( parent, u"OGC EDR"_s, u"edr:"_s, QgsEdrDataItem::Root ) : nullptr;
}
