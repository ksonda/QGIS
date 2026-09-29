/***************************************************************************
    testqgsedrsourceselect.cpp
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

#include <memory>

#include "../core/qgsedrtestserver.h"
#include "qgsabstractdatasourcewidget.h"
#include "qgsapplication.h"
#include "qgsdataitem.h"
#include "qgsedrdataitems.h"
#include "qgsedrquery.h"
#include "qgsgui.h"
#include "qgslayeritem.h"
#include "qgsproviderguiregistry.h"
#include "qgssettings.h"
#include "qgssourceselectprovider.h"
#include "qgstest.h"

#include <QComboBox>
#include <QJsonArray>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;

class TestQgsEdrSourceSelect : public QObject
{
    Q_OBJECT
    QTemporaryDir mProfile;

    std::unique_ptr<QgsAbstractDataSourceWidget> widget()
    {
      QList<QgsSourceSelectProvider *> providers = QgsGui::providerGuiRegistry()->sourceSelectProviders( u"edr"_s );
      if ( providers.isEmpty() )
        return nullptr;
      std::unique_ptr<QgsAbstractDataSourceWidget> result( providers.first()->createDataSourceWidget( nullptr, Qt::Window, QgsProviderRegistry::WidgetMode::Standalone ) );
      qDeleteAll( providers );
      return result;
    }

    static QgsEdrTestServer::Response response( const QgsEdrTestServer::Request &request )
    {
      if ( request.url.path() == "/catalog"_L1 )
        return { QJsonObject {
          { u"collections"_s, QJsonArray { QJsonObject { { u"id"_s, u"weather"_s }, { u"links"_s, QJsonArray { QJsonObject { { u"rel"_s, u"self"_s }, { u"href"_s, u"/weather"_s } } } } } } }
        } };
      if ( request.url.path() == "/weather"_L1 )
        return { QJsonObject {
          { u"id"_s, u"weather"_s },
          { u"parameter_names"_s, QJsonObject { { u"temperature"_s, QJsonObject() } } },
          { u"data_queries"_s,
            QJsonObject {
              { u"position"_s,
                QJsonObject { { u"link"_s, QJsonObject { { u"href"_s, u"/weather/position"_s }, { u"variables"_s, QJsonObject { { u"output_formats"_s, QJsonArray { u"CoverageJSON"_s } } } } } } } },
              { u"locations"_s, QJsonObject { { u"link"_s, QJsonObject { { u"href"_s, u"/weather/locations"_s } } } } }
            } }
        } };
      return { QJsonObject { { u"links"_s, QJsonArray { QJsonObject { { u"rel"_s, u"data"_s }, { u"href"_s, u"/catalog"_s } } } } } };
    }

  private slots:
    void initTestCase()
    {
      QVERIFY( mProfile.isValid() );
      QSettings::setDefaultFormat( QSettings::IniFormat );
      QSettings::setPath( QSettings::IniFormat, QSettings::UserScope, mProfile.path() );
      qputenv( "QGIS_AUTH_DB_DIR_PATH", mProfile.path().toUtf8() );
      QgsApplication::init();
      QgsApplication::initQgis();
    }
    void cleanupTestCase() { QgsApplication::exitQgis(); }

    void discoveryAndLayerRequest()
    {
      QgsEdrTestServer server;
      QVERIFY( server.isListening() );
      server.respond = response;
      auto select = widget();
      QVERIFY( select );
      select->findChild<QLineEdit *>( u"edrUrl"_s )->setText( server.url().toString() );
      select->findChild<QPushButton *>( u"edrConnect"_s )->click();
      QCOMPARE( select->findChild<QComboBox *>( u"edrCollection"_s )->currentText(), u"weather"_s );
      QCOMPARE( select->findChild<QComboBox *>( u"edrQuery"_s )->count(), 3 );
      QVERIFY( select->findChild<QComboBox *>( u"edr_parameter-name"_s )->findText( u"temperature"_s ) >= 0 );
      select->findChild<QComboBox *>( u"edrQuery"_s )->setCurrentText( u"position"_s );
      select->findChild<QComboBox *>( u"edr_coords"_s )->setCurrentText( u"POINT(-75 40)"_s );
      select->findChild<QComboBox *>( u"edr_datetime"_s )->setCurrentText( u"2026-01-01T00:00:00Z/.."_s );
      QSignalSpy added( select.get(), &QgsAbstractDataSourceWidget::addLayer );
      select->findChild<QPushButton *>( u"edrAdd"_s )->click();
      QCOMPARE( added.size(), 1 );
      const QList<QVariant> arguments = added.takeFirst();
      QCOMPARE( qvariant_cast<Qgis::LayerType>( arguments.at( 0 ) ), Qgis::LayerType::Vector );
      const QgsEdrQuery query = QgsEdrQuery::fromUri( arguments.at( 1 ).toString() );
      QCOMPARE( query.endpoint(), server.url( u"/weather/position"_s ) );
      QCOMPARE( query.parameters.value( u"coords"_s ), u"POINT(-75 40)"_s );
      QCOMPARE( query.parameters.value( u"datetime"_s ), u"2026-01-01T00:00:00Z/.."_s );
      QCOMPARE( arguments.at( 3 ).toString(), u"edr"_s );
      select->findChild<QComboBox *>( u"edrMode"_s )->setCurrentIndex( 1 );
      select->findChild<QPushButton *>( u"edrAdd"_s )->click();
      QCOMPARE( added.size(), 1 );
      QCOMPARE( added.first().at( 3 ).toString(), u"edr_raster"_s );
      QCOMPARE( qvariant_cast<Qgis::LayerType>( added.first().at( 0 ) ), Qgis::LayerType::Raster );
    }

    void invalidQueryStaysInDialog()
    {
      auto select = widget();
      QVERIFY( select );
      select->findChild<QLineEdit *>( u"edrUrl"_s )->setText( u"https://example.invalid/edr"_s );
      select->findChild<QComboBox *>( u"edrCollection"_s )->setCurrentText( u"weather"_s );
      select->findChild<QComboBox *>( u"edrQuery"_s )->setCurrentText( u"position"_s );
      QSignalSpy added( select.get(), &QgsAbstractDataSourceWidget::addLayer );
      select->findChild<QPushButton *>( u"edrAdd"_s )->click();
      QVERIFY( added.isEmpty() );
      QVERIFY( select->findChild<QPlainTextEdit *>( u"edrPreview"_s )->toPlainText().contains( u"coords"_s ) );
    }

    void browserUsesAdvertisedLinks()
    {
      QgsEdrTestServer server;
      QVERIFY( server.isListening() );
      server.respond = response;
      QgsSettings settings;
      settings.setValue( u"edr/connections/test-weather/url"_s, server.url().toString() );
      QgsEdrDataItemProvider provider;
      std::unique_ptr<QgsDataItem> root( provider.createDataItem( QString(), nullptr ) );
      QVERIFY( root );
      root->populate( true );
      QCOMPARE( root->children().size(), 1 );
      QgsDataItem *service = root->children().first();
      service->populate( true );
      QCOMPARE( service->children().size(), 1 );
      QgsDataItem *collection = service->children().first();
      collection->populate( true );
      QCOMPARE( collection->children().size(), 2 );
      bool foundLocations = false;
      const QVector<QgsDataItem *> children = collection->children();
      for ( QgsDataItem *child : children )
      {
        if ( QgsLayerItem *layer = qobject_cast<QgsLayerItem *>( child ) )
        {
          QCOMPARE( QgsEdrQuery::fromUri( layer->uri() ).endpoint(), server.url( u"/weather/locations"_s ) );
          foundLocations = true;
        }
      }
      QVERIFY( foundLocations );
      settings.remove( u"edr/connections/test-weather"_s );
    }
};
QGSTEST_MAIN( TestQgsEdrSourceSelect )
#include "testqgsedrsourceselect.moc"
