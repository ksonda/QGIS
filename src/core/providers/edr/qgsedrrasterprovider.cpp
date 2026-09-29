/***************************************************************************
    qgsedrrasterprovider.cpp
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

#include "qgsedrrasterprovider.h"

#include <cmath>
#include <limits>

#include "qgserror.h"
#include "qgsrasterblock.h"
#include "qgsrasteridentifyresult.h"

#include <QJsonDocument>
#include <QString>

using namespace Qt::StringLiterals;

QgsEdrRasterProvider::QgsEdrRasterProvider( const QString &uri, const ProviderOptions &options, Qgis::DataProviderReadFlags flags )
  : QgsRasterDataProvider( uri, options, flags )
  , mOptions( options )
  , mFlags( flags )
{
  load( false );
}

QgsEdrRasterProvider::QgsEdrRasterProvider( const QString &uri, const ProviderOptions &options, Qgis::DataProviderReadFlags flags, std::shared_ptr<const QgsEdrRasterData> data )
  : QgsRasterDataProvider( uri, options, flags )
  , mOptions( options )
  , mFlags( flags )
  , mData( std::move( data ) )
{
  initNoData();
}

QString QgsEdrRasterProvider::name() const
{
  return u"edr_raster"_s;
}
QString QgsEdrRasterProvider::description() const
{
  return tr( "OGC EDR CoverageJSON grid" );
}
bool QgsEdrRasterProvider::isValid() const
{
  return bool( mData );
}
QgsCoordinateReferenceSystem QgsEdrRasterProvider::crs() const
{
  return mData ? mData->crs : QgsCoordinateReferenceSystem();
}
QgsRectangle QgsEdrRasterProvider::extent() const
{
  return mData ? mData->extent : QgsRectangle();
}
int QgsEdrRasterProvider::bandCount() const
{
  return mData ? mData->bands.size() : 0;
}
int QgsEdrRasterProvider::xSize() const
{
  return mData ? mData->width : 0;
}
int QgsEdrRasterProvider::ySize() const
{
  return mData ? mData->height : 0;
}
int QgsEdrRasterProvider::xBlockSize() const
{
  return std::min( 256, xSize() );
}
int QgsEdrRasterProvider::yBlockSize() const
{
  return std::min( 256, ySize() );
}
Qgis::RasterInterfaceCapabilities QgsEdrRasterProvider::capabilities() const
{
  return Qgis::RasterInterfaceCapability::Size | Qgis::RasterInterfaceCapability::Identify | Qgis::RasterInterfaceCapability::IdentifyValue;
}
Qgis::DataType QgsEdrRasterProvider::dataType( int band ) const
{
  return validBand( band ) ? Qgis::DataType::Float64 : Qgis::DataType::UnknownDataType;
}
Qgis::DataType QgsEdrRasterProvider::sourceDataType( int band ) const
{
  return dataType( band );
}
QString QgsEdrRasterProvider::generateBandName( int band ) const
{
  return validBand( band ) ? mData->bands[band - 1].name : QString();
}
QString QgsEdrRasterProvider::bandDescription( int band )
{
  return generateBandName( band );
}
QString QgsEdrRasterProvider::lastErrorTitle()
{
  return u"EDR"_s;
}
QString QgsEdrRasterProvider::lastError()
{
  return mError;
}
bool QgsEdrRasterProvider::validBand( int band ) const
{
  return mData && band > 0 && band <= mData->bands.size();
}
QString QgsEdrRasterProvider::htmlMetadata() const
{
  return mData ? u"<pre>"_s + QString::fromUtf8( QJsonDocument( mData->metadata ).toJson() ).toHtmlEscaped() + u"</pre>"_s : mError.toHtmlEscaped();
}

QgsEdrRasterProvider *QgsEdrRasterProvider::clone() const
{
  QgsEdrRasterProvider *provider = new QgsEdrRasterProvider( dataSourceUri(), mOptions, mFlags, mData );
  provider->copyBaseSettings( *this );
  provider->mError = mError;
  return provider;
}

void QgsEdrRasterProvider::reloadProviderData()
{
  load( true );
}
bool QgsEdrRasterProvider::reload()
{
  reloadData();
  return mError.isEmpty();
}

QgsRasterBlock *QgsEdrRasterProvider::block( int band, const QgsRectangle &view, int width, int height, QgsRasterBlockFeedback *feedback )
{
  auto result = std::make_unique<QgsRasterBlock>( Qgis::DataType::Float64, width, height );
  result->setNoDataValue( std::numeric_limits<double>::quiet_NaN() );
  if ( !result->isValid() )
    return result.release();
  result->setIsNoData();
  if ( !validBand( band ) || width < 1 || height < 1 || view.isEmpty() )
    return result.release();
  const QVector<double> &values = mData->bands[band - 1].values;
  for ( int row = 0; row < height; ++row )
  {
    if ( feedback && feedback->isCanceled() )
      return result.release();
    const double y = view.yMaximum() - ( row + 0.5 ) * view.height() / height;
    const double sourceRow = ( mData->extent.yMaximum() - y ) * mData->height / mData->extent.height();
    if ( !std::isfinite( sourceRow ) || sourceRow < 0 || sourceRow >= mData->height )
      continue;
    for ( int column = 0; column < width; ++column )
    {
      const double x = view.xMinimum() + ( column + 0.5 ) * view.width() / width;
      const double sourceColumn = ( x - mData->extent.xMinimum() ) * mData->width / mData->extent.width();
      if ( !std::isfinite( sourceColumn ) || sourceColumn < 0 || sourceColumn >= mData->width )
        continue;
      const double value = values[int( std::floor( sourceRow ) ) * mData->width + int( std::floor( sourceColumn ) )];
      if ( !std::isnan( value ) && !userNoDataValuesContains( band, value ) )
        result->setValue( row, column, value );
    }
  }
  return result.release();
}

QgsRasterIdentifyResult QgsEdrRasterProvider::identify( const QgsPointXY &point, Qgis::RasterIdentifyFormat format, const QgsRectangle &, int, int, int )
{
  if ( format != Qgis::RasterIdentifyFormat::Value || !mData )
    return QgsRasterIdentifyResult( QgsError( tr( "Only numeric identify is supported" ), u"EDR"_s ) );
  QMap<int, QVariant> values;
  const double column = ( point.x() - mData->extent.xMinimum() ) * xSize() / mData->extent.width();
  const double row = ( mData->extent.yMaximum() - point.y() ) * ySize() / mData->extent.height();
  for ( int band = 1; band <= bandCount(); ++band )
  {
    const double value = std::isfinite( column ) && std::isfinite( row ) && column >= 0 && row >= 0 && column < xSize() && row < ySize()
                           ? mData->bands[band - 1].values[int( row ) * xSize() + int( column )]
                           : std::numeric_limits<double>::quiet_NaN();
    values.insert( band, std::isnan( value ) || userNoDataValuesContains( band, value ) ? QVariant() : QVariant( value ) );
  }
  return QgsRasterIdentifyResult( format, values );
}

void QgsEdrRasterProvider::initNoData()
{
  mSrcHasNoDataValue.clear();
  mSrcNoDataValue.clear();
  mUseSrcNoDataValue.clear();
  mUserNoDataValue.clear();
  for ( int band = 0; band < bandCount(); ++band )
  {
    mSrcHasNoDataValue.append( true );
    mUseSrcNoDataValue.append( true );
    mSrcNoDataValue.append( std::numeric_limits<double>::quiet_NaN() );
    mUserNoDataValue.append( QgsRasterRangeList() );
  }
}

void QgsEdrRasterProvider::load( bool refresh )
{
  try
  {
    const QgsEdrQuery query = QgsEdrQuery::fromUri( dataSourceUri() );
    auto data = std::make_shared<QgsEdrRasterData>( qgsEdrRasterData( qgsEdrLoadDataset( query, nullptr, refresh ), query ) );
    const QList<QgsRasterRangeList> userNoData = mUserNoDataValue;
    mData = std::move( data );
    initNoData();
    for ( int band = 0; band < std::min( bandCount(), static_cast<int>( userNoData.size() ) ); ++band )
      mUserNoDataValue[band] = userNoData[band];
    mError.clear();
    setError( QgsError() );
    emit dataChanged();
  }
  catch ( const std::exception &exception )
  {
    mError = QString::fromUtf8( exception.what() );
    setError( QgsError( mError, u"EDR"_s ) );
  }
}

QgsEdrRasterProviderMetadata::QgsEdrRasterProviderMetadata()
  : QgsProviderMetadata( u"edr_raster"_s, tr( "OGC EDR CoverageJSON grid" ) )
{}
QgsEdrRasterProvider *QgsEdrRasterProviderMetadata::createProvider( const QString &uri, const QgsDataProvider::ProviderOptions &options, Qgis::DataProviderReadFlags flags )
{
  return new QgsEdrRasterProvider( uri, options, flags );
}
QVariantMap QgsEdrRasterProviderMetadata::decodeUri( const QString &uri ) const
{
  return QgsEdrQuery::decodeUri( uri );
}
QString QgsEdrRasterProviderMetadata::encodeUri( const QVariantMap &parts ) const
{
  return QgsEdrQuery::encodeUri( parts );
}
QString QgsEdrRasterProviderMetadata::absoluteToRelativeUri( const QString &uri, const QgsReadWriteContext &context ) const
{
  return qgsEdrResolveFileUri( uri, context, false );
}
QString QgsEdrRasterProviderMetadata::relativeToAbsoluteUri( const QString &uri, const QgsReadWriteContext &context ) const
{
  return qgsEdrResolveFileUri( uri, context, true );
}
QList<Qgis::LayerType> QgsEdrRasterProviderMetadata::supportedLayerTypes() const
{
  return { Qgis::LayerType::Raster };
}
