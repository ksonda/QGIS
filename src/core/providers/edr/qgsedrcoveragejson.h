/***************************************************************************
    qgsedrcoveragejson.h
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

#ifndef QGSEDRCOVERAGEJSON_H
#define QGSEDRCOVERAGEJSON_H

#include "qgis_core.h"

#define SIP_NO_FILE

///@cond PRIVATE
#include "qgsedrtypes.h"
#include "qgsfeedback.h"
#include <QJsonArray>
#include <QMap>
#include <functional>

CORE_EXPORT bool qgsEdrIsCategoricalParameter( const QJsonObject &parameter );

struct CORE_EXPORT QgsEdrAxis
{
    QString name;
    QString dataType;
    QStringList coordinates;
    QJsonArray values;
    QJsonArray bounds;
};
struct CORE_EXPORT QgsEdrArray
{
    QString dataType;
    QStringList axes;
    QVector<int> shape;
    QJsonArray values;
    QJsonValue at( const QMap<QString, int> &index ) const;
};
struct CORE_EXPORT QgsEdrCoverage
{
    QString id;
    QString domainType;
    QMap<QString, QgsEdrAxis> axes;
    QMap<QString, QgsEdrArray> ranges;
    QJsonObject parameters;
    QJsonArray referencing;
    QJsonObject metadata;
    QJsonObject domain;
    qint64 cellCount = 0;
    void eachCell( const std::function<void( const QMap<QString, int> &, const QJsonObject & )> &callback ) const;
};

class CORE_EXPORT QgsEdrCoverageJsonReader
{
  public:
    /**
     * Resolves linked documents. The caller owns any captured resources.
     */
    using DocumentLoader = std::function<QgsEdrDocument( const QUrl & )>;
    explicit QgsEdrCoverageJsonReader( DocumentLoader loader = {}, const QgsEdrLimits &limits = {}, QgsFeedback *feedback = nullptr )
      : mLoader( std::move( loader ) )
      , mLimits( limits )
      , mFeedback( feedback )
    {}
    QVector<QgsEdrCoverage> read( const QgsEdrDocument &document );

  private:
    DocumentLoader mLoader;
    QgsEdrLimits mLimits;
    QgsFeedback *mFeedback = nullptr;
    QgsEdrDocument resolve( const QJsonValue &value, const QUrl &base );
    void checkCanceled() const;
    qint64 mCells = 0;
    QgsEdrCoverage coverage( const QgsEdrDocument &doc, const QJsonObject &parent );
    QgsEdrArray array( const QgsEdrDocument &doc );
};

///@endcond

#endif // QGSEDRCOVERAGEJSON_H
