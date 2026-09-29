/***************************************************************************
    qgsedrdataitems.h
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

#ifndef QGSEDRDATAITEMS_H
#define QGSEDRDATAITEMS_H

#include "qgis_core.h"
#include "qgsdataitemprovider.h"
#include "qgsdataprovider.h"

#include <QString>

#define SIP_NO_FILE

using namespace Qt::StringLiterals;


///@cond PRIVATE

class CORE_EXPORT QgsEdrDataItemProvider final : public QgsDataItemProvider
{
  public:
    QString name() override { return u"EDR"_s; }
    Qgis::DataItemProviderCapabilities capabilities() const override { return Qgis::DataItemProviderCapability::NetworkSources; }
    QgsDataItem *createDataItem( const QString &path, QgsDataItem *parent ) override;
};

///@endcond

#endif // QGSEDRDATAITEMS_H
