/***************************************************************************
    qgsedrrasterprovider.h
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

#ifndef QGSEDRRASTERPROVIDER_H
#define QGSEDRRASTERPROVIDER_H

#include <memory>

#include "qgis_core.h"
#include "qgsedrrasterdata.h"
#include "qgsprovidermetadata.h"
#include "qgsrasterdataprovider.h"

#define SIP_NO_FILE

///@cond PRIVATE

/**
 * Read-only rectilinear CoverageJSON grid with parameter and dimension slices as bands.
 *
 * Clones share an immutable snapshot. Reloading replaces only the requesting
 * provider's snapshot after the entire response has passed validation.
 * \since QGIS 4.4
 */
class CORE_EXPORT QgsEdrRasterProvider final : public QgsRasterDataProvider
{
    Q_OBJECT

  public:
    QgsEdrRasterProvider( const QString &uri, const ProviderOptions &options, Qgis::DataProviderReadFlags flags = {} );
    QString name() const override;
    QString description() const override;
    bool isValid() const override;
    QgsCoordinateReferenceSystem crs() const override;
    QgsRectangle extent() const override;
    int bandCount() const override;
    int xSize() const override;
    int ySize() const override;
    int xBlockSize() const override;
    int yBlockSize() const override;
    Qgis::RasterInterfaceCapabilities capabilities() const override;
    Qgis::DataType dataType( int band ) const override;
    Qgis::DataType sourceDataType( int band ) const override;
    QString generateBandName( int band ) const override;
    QString bandDescription( int band ) override;
    QString lastErrorTitle() override;
    QString lastError() override;
    QString htmlMetadata() const override;
    QgsEdrRasterProvider *clone() const override;
    bool reload() override;
    QgsRasterBlock *block( int band, const QgsRectangle &view, int width, int height, QgsRasterBlockFeedback *feedback = nullptr ) override;
    QgsRasterIdentifyResult identify( const QgsPointXY &point, Qgis::RasterIdentifyFormat format, const QgsRectangle &view = QgsRectangle(), int width = 0, int height = 0, int dpi = 96 ) override;

  private:
    QgsEdrRasterProvider( const QString &uri, const ProviderOptions &options, Qgis::DataProviderReadFlags flags, std::shared_ptr<const QgsEdrRasterData> data );
    void reloadProviderData() override;
    bool validBand( int band ) const;
    void initNoData();
    void load( bool refresh );

    ProviderOptions mOptions;
    Qgis::DataProviderReadFlags mFlags;
    std::shared_ptr<const QgsEdrRasterData> mData;
    QString mError;
};

class QgsEdrRasterProviderMetadata final : public QgsProviderMetadata
{
    Q_OBJECT

  public:
    QgsEdrRasterProviderMetadata();
    QgsEdrRasterProvider *createProvider( const QString &uri, const QgsDataProvider::ProviderOptions &options, Qgis::DataProviderReadFlags flags = {} ) override;
    QVariantMap decodeUri( const QString &uri ) const override;
    QString encodeUri( const QVariantMap &parts ) const override;
    QString absoluteToRelativeUri( const QString &uri, const QgsReadWriteContext &context ) const override;
    QString relativeToAbsoluteUri( const QString &uri, const QgsReadWriteContext &context ) const override;
    QList<Qgis::LayerType> supportedLayerTypes() const override;
};

///@endcond

#endif // QGSEDRRASTERPROVIDER_H
