/***************************************************************************
    qgsedrrasterdata.h
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

#ifndef QGSEDRRASTERDATA_H
#define QGSEDRRASTERDATA_H
#include "qgsedrdataset.h"

#define SIP_NO_FILE

///@cond PRIVATE
struct CORE_EXPORT QgsEdrRasterBand
{
    QString name;
    QString unit;
    QVector<double> values;
    QJsonObject metadata;
};

struct CORE_EXPORT QgsEdrRasterData
{
    int width = 0;
    int height = 0;
    QgsRectangle extent;
    QgsCoordinateReferenceSystem crs;
    QVector<QgsEdrRasterBand> bands;
    QJsonArray metadata;
};

CORE_EXPORT QgsEdrRasterData qgsEdrRasterData( const QgsEdrDataset &dataset, const QgsEdrQuery &query );
///@endcond
#endif
