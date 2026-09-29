/***************************************************************************
    qgsedrprovider.cpp
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

#include "qgsedrprovider.h"

#include "qgsedrdataset.h"
#include "qgserror.h"
#include "qgsfeatureiterator.h"
#include "qgsmemoryprovider.h"

#include <QJsonDocument>
#include <QString>
#include <QUrlQuery>

using namespace Qt::StringLiterals;

QgsEdrProvider::QgsEdrProvider( const QString &uri, const ProviderOptions &options, Qgis::DataProviderReadFlags flags )
  : QgsVectorDataProvider( uri, options, flags )
  , mMemory( std::make_unique<QgsMemoryProvider>( u"None"_s, options ) )
  , mOptions( options )
{
  load( false );
}

QString QgsEdrProvider::name() const
{
  return u"edr"_s;
}
QString QgsEdrProvider::description() const
{
  return tr( "OGC API Environmental Data Retrieval" );
}
QString QgsEdrProvider::storageType() const
{
  return u"EDR / CoverageJSON"_s;
}
bool QgsEdrProvider::isValid() const
{
  QReadLocker locker( &mLock );
  return mValid;
}
QgsCoordinateReferenceSystem QgsEdrProvider::crs() const
{
  QReadLocker locker( &mLock );
  return mMemory->crs();
}
QgsRectangle QgsEdrProvider::extent() const
{
  QReadLocker locker( &mLock );
  return mMemory->extent();
}
QgsFields QgsEdrProvider::fields() const
{
  QReadLocker locker( &mLock );
  return mMemory->fields();
}
Qgis::WkbType QgsEdrProvider::wkbType() const
{
  QReadLocker locker( &mLock );
  return mMemory->wkbType();
}
long long QgsEdrProvider::featureCount() const
{
  QReadLocker locker( &mLock );
  return mMemory->featureCount();
}
QgsFeatureIterator QgsEdrProvider::getFeatures( const QgsFeatureRequest &request ) const
{
  QReadLocker locker( &mLock );
  return mMemory->getFeatures( request );
}
QgsAbstractFeatureSource *QgsEdrProvider::featureSource() const
{
  QReadLocker locker( &mLock );
  return mMemory->featureSource();
}
Qgis::VectorProviderCapabilities QgsEdrProvider::capabilities() const
{
  return Qgis::VectorProviderCapability::SelectAtId;
}
QString QgsEdrProvider::dataComment() const
{
  QReadLocker locker( &mLock );
  return QString::fromUtf8( QJsonDocument( mMetadata ).toJson() );
}
bool QgsEdrProvider::supportsSubsetString() const
{
  return true;
}
QString QgsEdrProvider::subsetStringDialect() const
{
  return tr( "QGIS expression" );
}
QString QgsEdrProvider::subsetString() const
{
  QReadLocker locker( &mLock );
  return mMemory->subsetString();
}

bool QgsEdrProvider::setSubsetString( const QString &subset, bool updateFeatureCount )
{
  {
    QWriteLocker locker( &mLock );
    if ( !mMemory->setSubsetString( subset, updateFeatureCount ) )
      return false;
    QVariantMap parts = QgsEdrQuery::decodeUri( dataSourceUri() );
    parts.insert( u"subset"_s, subset );
    setDataSourceUri( QgsEdrQuery::encodeUri( parts ) );
  }
  emit dataChanged();
  return true;
}

void QgsEdrProvider::reloadProviderData()
{
  load( true );
}

void QgsEdrProvider::load( bool refresh )
{
  try
  {
    const QgsEdrQuery query = QgsEdrQuery::fromUri( dataSourceUri() );
    const QgsEdrDataset dataset = qgsEdrLoadDataset( query, nullptr, refresh );
    QgsEdrVectorData data = qgsEdrVectorData( dataset, query, mOptions.transformContext );
    const QString crsDefinition = data.crs.authid().isEmpty() ? data.crs.toWkt() : data.crs.authid();
    QUrlQuery memoryQuery;
    memoryQuery.addQueryItem( u"crs"_s, crsDefinition );
    memoryQuery.addQueryItem( u"index"_s, u"yes"_s );
    const QString uri = QgsWkbTypes::displayString( data.wkb ) + '?' + memoryQuery.toString();
    auto memory = std::make_unique<QgsMemoryProvider>( uri, mOptions );
    if ( !memory->isValid() || !memory->crs().isValid() )
      throw QgsEdrError( tr( "Cannot create the feature store with the coverage CRS" ) );
    if ( !memory->addAttributes( data.fields.toList() ) || !memory->addFeatures( data.features ) )
      throw QgsEdrError( tr( "Cannot populate the feature store" ) );
    if ( !memory->setSubsetString( query.subset ) )
      throw QgsEdrError( tr( "Invalid subset expression" ) );
    {
      QWriteLocker locker( &mLock );
      mMemory = std::move( memory );
      mMetadata = data.metadata;
      mValid = true;
      clearErrors();
      setError( QgsError() );
    }
    emit dataChanged();
  }
  catch ( const std::exception &exception )
  {
    const QString message = QString::fromUtf8( exception.what() );
    setError( QgsError( message, u"EDR"_s ) );
    pushError( message );
  }
}

QgsEdrProviderMetadata::QgsEdrProviderMetadata()
  : QgsProviderMetadata( u"edr"_s, tr( "OGC API Environmental Data Retrieval" ) )
{}

QgsEdrProvider *QgsEdrProviderMetadata::createProvider( const QString &uri, const QgsDataProvider::ProviderOptions &options, Qgis::DataProviderReadFlags flags )
{
  return new QgsEdrProvider( uri, options, flags );
}

QVariantMap QgsEdrProviderMetadata::decodeUri( const QString &uri ) const
{
  return QgsEdrQuery::decodeUri( uri );
}
QString QgsEdrProviderMetadata::encodeUri( const QVariantMap &parts ) const
{
  return QgsEdrQuery::encodeUri( parts );
}
QString QgsEdrProviderMetadata::absoluteToRelativeUri( const QString &uri, const QgsReadWriteContext &context ) const
{
  return qgsEdrResolveFileUri( uri, context, false );
}
QString QgsEdrProviderMetadata::relativeToAbsoluteUri( const QString &uri, const QgsReadWriteContext &context ) const
{
  return qgsEdrResolveFileUri( uri, context, true );
}
QList<Qgis::LayerType> QgsEdrProviderMetadata::supportedLayerTypes() const
{
  return { Qgis::LayerType::Vector };
}
