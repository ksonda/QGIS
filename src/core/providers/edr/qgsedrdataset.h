/***************************************************************************
    qgsedrdataset.h
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

#ifndef QGSEDRDATASET_H
#define QGSEDRDATASET_H

#include "qgis_core.h"

#define SIP_NO_FILE

///@cond PRIVATE
#include "qgsedrcoveragejson.h"
#include "qgsedrquery.h"
#include "qgscoordinatereferencesystem.h"
#include "qgscoordinatetransformcontext.h"
#include "qgsfeature.h"
#include "qgsfields.h"
#include "qgsrectangle.h"

struct CORE_EXPORT QgsEdrDataset
{
    QVector<QgsEdrCoverage> coverages;
    QVector<QgsEdrDocument> geojson;
    QJsonArray metadata;
};

struct CORE_EXPORT QgsEdrVectorData
{
    QgsFields fields;
    QgsFeatureList features;
    QgsCoordinateReferenceSystem crs;
    Qgis::WkbType wkb = Qgis::WkbType::Unknown;
    QJsonArray metadata;
};

CORE_EXPORT QgsEdrDataset qgsEdrLoadDataset( const QgsEdrQuery &query, QgsFeedback *feedback = nullptr, bool refresh = false );
CORE_EXPORT QgsEdrVectorData qgsEdrVectorData( const QgsEdrDataset &dataset, const QgsEdrQuery &query, const QgsCoordinateTransformContext &context );

CORE_EXPORT QgsCoordinateReferenceSystem qgsEdrCoverageCrs( const QgsEdrCoverage &coverage );

///@endcond

#endif // QGSEDRDATASET_H
