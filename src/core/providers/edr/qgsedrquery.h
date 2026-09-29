/***************************************************************************
    qgsedrquery.h
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

#ifndef QGSEDRQUERY_H
#define QGSEDRQUERY_H

#include "qgis_core.h"
#include "qgsedrtypes.h"

#include <QString>

#define SIP_NO_FILE

using namespace Qt::StringLiterals;


///@cond PRIVATE

#include <QJsonObject>
#include <QMap>
#include <QUrl>
#include <QVariantMap>
#include <stdexcept>

class QgsReadWriteContext;

struct CORE_EXPORT QgsEdrRequest
{
    QUrl url;
    QByteArray method = "GET";
    QByteArray body;
    QByteArray contentType;
    QByteArray accept = "application/vnd.cov+json, application/prs.coverage+json;q=0.9, application/geo+json;q=0.8, application/json;q=0.7";
};

struct CORE_EXPORT QgsEdrQuery
{
    QUrl root;
    QString collection;
    QString instance;
    QString kind;
    QString id;
    QUrl href;
    QString method = u"GET"_s;
    QString postEncoding = u"form"_s;
    QString authcfg;
    QString geometry;
    QString coverage;
    QString targetCrs;
    QString subset;
    QMap<QString, QString> parameters;
    QgsEdrLimits limits;

    static QgsEdrQuery fromUri( const QString &uri );
    static QString encodeUri( const QVariantMap &parts );
    static QVariantMap decodeUri( const QString &uri );
    QString toUri() const;
    QUrl endpoint() const;
    QgsEdrRequest request() const;
    void validate() const;
    static QStringList queryTypes();
    static QStringList resources();
};

CORE_EXPORT QUrl qgsEdrLinkUrl( const QJsonObject &document, const QUrl &base, const QString &rel );
CORE_EXPORT bool qgsEdrSameOrigin( const QUrl &a, const QUrl &b );
CORE_EXPORT QString qgsEdrResolveFileUri( const QString &uri, const QgsReadWriteContext &context, bool absolute );

///@endcond

#endif // QGSEDRQUERY_H
