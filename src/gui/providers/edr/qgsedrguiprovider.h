/***************************************************************************
    qgsedrguiprovider.h
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

#ifndef QGSEDRGUIPROVIDER_H
#define QGSEDRGUIPROVIDER_H

#include "qgis_gui.h"
#include "qgsproviderguimetadata.h"

#define SIP_NO_FILE

///@cond PRIVATE

class GUI_EXPORT QgsEdrProviderGuiMetadata final : public QgsProviderGuiMetadata
{
  public:
    QgsEdrProviderGuiMetadata();
    void registerGui( QMainWindow *window ) override;
    QList<QgsSourceSelectProvider *> sourceSelectProviders() override;
};

///@endcond

#endif // QGSEDRGUIPROVIDER_H
