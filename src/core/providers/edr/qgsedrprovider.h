/***************************************************************************
    qgsedrprovider.h
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

#ifndef QGSEDRPROVIDER_H
#define QGSEDRPROVIDER_H

#include <memory>

#include "qgis_core.h"
#include "qgsprovidermetadata.h"
#include "qgsvectordataprovider.h"

#include <QJsonArray>
#include <QReadWriteLock>

#define SIP_NO_FILE

///@cond PRIVATE

/**
 * Read-only snapshot of an EDR query, with one feature per CoverageJSON cell.
 *
 * The memory provider supplies detached feature sources and expression filtering.
 * A failed reload retains the last complete snapshot and sets the provider error.
 * \since QGIS 4.4
 */
class CORE_EXPORT QgsEdrProvider final : public QgsVectorDataProvider
{
    Q_OBJECT

  public:
    QgsEdrProvider( const QString &uri, const ProviderOptions &options, Qgis::DataProviderReadFlags flags = {} );
    QString name() const override;
    QString description() const override;
    QString storageType() const override;
    bool isValid() const override;
    QgsCoordinateReferenceSystem crs() const override;
    QgsRectangle extent() const override;
    QgsFields fields() const override;
    Qgis::WkbType wkbType() const override;
    long long featureCount() const override;
    QgsFeatureIterator getFeatures( const QgsFeatureRequest &request = QgsFeatureRequest() ) const override;
    QgsAbstractFeatureSource *featureSource() const override;
    Qgis::VectorProviderCapabilities capabilities() const override;
    QString dataComment() const override;
    bool supportsSubsetString() const override;
    QString subsetStringDialect() const override;
    QString subsetString() const override;
    bool setSubsetString( const QString &subset, bool updateFeatureCount = true ) override;

  private:
    void reloadProviderData() override;
    void load( bool refresh );

    mutable QReadWriteLock mLock;
    std::unique_ptr<QgsVectorDataProvider> mMemory;
    ProviderOptions mOptions;
    QJsonArray mMetadata;
    bool mValid = false;
};

class QgsEdrProviderMetadata final : public QgsProviderMetadata
{
    Q_OBJECT

  public:
    QgsEdrProviderMetadata();

    QgsEdrProvider *createProvider( const QString &uri, const QgsDataProvider::ProviderOptions &options, Qgis::DataProviderReadFlags flags = {} ) override;
    QVariantMap decodeUri( const QString &uri ) const override;
    QString encodeUri( const QVariantMap &parts ) const override;
    QString absoluteToRelativeUri( const QString &uri, const QgsReadWriteContext &context ) const override;
    QString relativeToAbsoluteUri( const QString &uri, const QgsReadWriteContext &context ) const override;
    QList<Qgis::LayerType> supportedLayerTypes() const override;
};

///@endcond

#endif // QGSEDRPROVIDER_H
