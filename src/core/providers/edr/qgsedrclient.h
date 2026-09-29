/***************************************************************************
    qgsedrclient.h
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

#ifndef QGSEDRCLIENT_H
#define QGSEDRCLIENT_H

#include "qgis_core.h"

#define SIP_NO_FILE

///@cond PRIVATE
#include "qgsedrquery.h"
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include "qgsfeedback.h"

class CORE_EXPORT QgsEdrClient
{
  public:
    explicit QgsEdrClient( const QgsEdrQuery &query, QgsFeedback *feedback = nullptr, bool refresh = false );
    QgsEdrDocument fetch( const QgsEdrRequest &request );
    QgsEdrDocument get( const QUrl &url );
    QgsEdrDocument resolve( const QJsonValue &value, const QUrl &base );
    QVector<QgsEdrDocument> pages( const QgsEdrRequest &request );
    QgsEdrDocument discover( const QString &kind );
    const QgsEdrLimits &limits() const { return mQuery.limits; }
    void checkCanceled() const;

  private:
    QgsEdrQuery mQuery;
    QgsFeedback *mFeedback;
    bool mRefresh;
    int mDocuments = 0;
    qint64 mBytes = 0;
    QHash<QString, QgsEdrDocument> mCache;
};

///@endcond

#endif // QGSEDRCLIENT_H
