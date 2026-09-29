/***************************************************************************
    qgsedrchartmodel.h
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

#ifndef QGSEDRCHARTMODEL_H
#define QGSEDRCHARTMODEL_H

#include <limits>

#include "qgis_gui.h"
#include "qgsfeature.h"

#include <QHash>
#include <QString>
#include <QVector>

#define SIP_NO_FILE

class QgsVectorLayer;


///@cond PRIVATE

struct GUI_EXPORT QgsEdrChartParameter
{
    int field = -1;
    QString name;
    QString label;
    QString unit;
    QString title() const;
};

struct GUI_EXPORT QgsEdrChartSample
{
    qint64 time = 0; // UTC milliseconds since the Unix epoch.
    double value = std::numeric_limits<double>::quiet_NaN();
    QgsFeatureId feature = FID_NULL;
};

struct GUI_EXPORT QgsEdrChartSeries
{
    QString key;
    QString label;
    QVector<QgsEdrChartSample> samples; // Missing values remain in the series as gaps.
};

struct GUI_EXPORT QgsEdrChartData
{
    QVector<QgsEdrChartSeries> series;
    QHash<QgsFeatureId, int> featureSeries;
    int missingTimes = 0;
    QString error;
};

GUI_EXPORT QVector<QgsEdrChartParameter> qgsEdrChartParameters( const QgsFields &fields );
// Groups stationary samples by coverage and non-temporal coordinates. Moving
// trajectories stay together; sections retain separate vertical levels.
GUI_EXPORT QgsEdrChartData qgsEdrChartData( QgsVectorLayer *layer, int field, int maximumRows = 100000 );

///@endcond

#endif // QGSEDRCHARTMODEL_H
