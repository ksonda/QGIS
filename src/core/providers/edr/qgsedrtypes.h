/***************************************************************************
    qgsedrtypes.h
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

#ifndef QGSEDRTYPES_H
#define QGSEDRTYPES_H
#include <stdexcept>

#include "qgis_core.h"

#include <QJsonObject>
#include <QUrl>

#define SIP_NO_FILE

///@cond PRIVATE
class CORE_EXPORT QgsEdrError : public std::runtime_error
{
  public:
    explicit QgsEdrError( const QString &message )
      : std::runtime_error( message.toStdString() )
    {}
};

struct CORE_EXPORT QgsEdrLimits
{
    qint64 maximumCells = 1000000;
    qint64 maximumBytes = 128 * 1024 * 1024;
    int maximumPages = 100;
    int maximumDocuments = 2048;
};

struct CORE_EXPORT QgsEdrDocument
{
    QJsonObject object;
    QUrl url;
};


CORE_EXPORT QString qgsEdrCompactJson( const QJsonValue &value );
CORE_EXPORT QString qgsEdrTextLabel( const QJsonValue &value );
///@endcond
#endif
