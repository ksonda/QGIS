/***************************************************************************
    qgsedrtypes.cpp
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

#include "qgsedrtypes.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QString>

using namespace Qt::StringLiterals;

QString qgsEdrCompactJson( const QJsonValue &v )
{
  if ( v.isObject() )
    return QString::fromUtf8( QJsonDocument( v.toObject() ).toJson( QJsonDocument::Compact ) );
  if ( v.isArray() )
    return QString::fromUtf8( QJsonDocument( v.toArray() ).toJson( QJsonDocument::Compact ) );
  return v.toVariant().toString();
}

QString qgsEdrTextLabel( const QJsonValue &v )
{
  if ( v.isString() )
    return v.toString();
  const QJsonObject o = v.toObject();
  if ( o.contains( u"en"_s ) )
    return o.value( u"en"_s ).toString();
  return o.isEmpty() ? QString() : o.begin().value().toString();
}
