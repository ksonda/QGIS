/***************************************************************************
    testqgsedrrasterprovider.cpp
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

#include <cmath>

#include "qgsapplication.h"
#include "qgsedrclient.h"
#include "qgsedrcoveragejson.h"
#include "qgsedrdataset.h"
#include "qgsedrquery.h"
#include "qgsgeometry.h"
#include "qgstest.h"

#include <QFile>
#include <QJsonDocument>
#include <QString>
#include <QTemporaryDir>
#include <QUrlQuery>

using namespace Qt::StringLiterals;
#include "qgsedrrasterprovider.h"
#include "qgsrasterlayer.h"
#include "qgsrasterblock.h"
#include "qgsrasteridentifyresult.h"
#include "qgsproject.h"
#include "qgsvectorlayer.h"

class TestQgsEdrRasterProvider : public QObject
{
    Q_OBJECT
    QString path( const QString &name ) const { return QStringLiteral( TEST_DATA_DIR ) + u"/edr/"_s + name + u".covjson"_s; }
    QString localUri( const QString &name ) const { return QgsEdrQuery::encodeUri( { { u"url"_s, QUrl::fromLocalFile( path( name ) ).toString() } } ); }

  private slots:
    void initTestCase()
    {
      QgsApplication::init();
      QgsApplication::initQgis();
    }
    void cleanupTestCase() { QgsApplication::exitQgis(); }
    void rasterSnapshot()
    {
      QgsRasterLayer layer( localUri( u"Grid"_s ), u"grid"_s, u"edr_raster"_s );
      QVERIFY( layer.isValid() );
      QgsRasterDataProvider *provider = layer.dataProvider();
      QCOMPARE( layer.bandCount(), 2 );
      QCOMPARE( provider->xSize(), 3 );
      QCOMPARE( provider->ySize(), 2 );
      std::unique_ptr<QgsRasterBlock> block( provider->block( 1, layer.extent(), 3, 2 ) );
      const QList<double> expected { 10, 11, 12, 0, 1, 2 };
      for ( int index = 0; index < expected.size(); ++index )
        QCOMPARE( block->value( index / 3, index % 3 ), expected[index] );
      block.reset( provider->block( 2, layer.extent(), 3, 2 ) );
      QVERIFY( block->isNoData( 1, 2 ) );
      QCOMPARE( provider->identify( QgsPointXY( 10, 50 ), Qgis::RasterIdentifyFormat::Value ).results().value( 1 ).toDouble(), 10.0 );
      std::unique_ptr<QgsRasterDataProvider> clone( provider->clone() );
      block.reset( clone->block( 1, layer.extent(), 3, 2 ) );
      QCOMPARE( block->value( 0, 0 ), 10.0 );
      block.reset( provider->block( 1, QgsRectangle( -100, -100, -90, -90 ), 2, 2 ) );
      QVERIFY( block->isNoData( 0, 0 ) );
      provider->setUserNoDataValue( 1, { QgsRasterRange( 10, 10 ) } );
      block.reset( provider->block( 1, layer.extent(), 3, 2 ) );
      QVERIFY( block->isNoData( 0, 0 ) );
      provider->reloadData();
      block.reset( provider->block( 1, layer.extent(), 3, 2 ) );
      QVERIFY( block->isNoData( 0, 0 ) );
      QCOMPARE( block->value( 0, 1 ), 11.0 );
    }
    void projectCanMove()
    {
      QTemporaryDir directory;
      QVERIFY( directory.isValid() );
      QDir root( directory.path() );
      QVERIFY( root.mkdir( u"original"_s ) );
      const QString original = directory.path() + u"/original"_s;
      const QString moved = directory.path() + u"/moved"_s;
      QVERIFY( QFile::copy( path( u"Grid"_s ), original + u"/grid.covjson"_s ) );
      const QString uri = QgsEdrQuery::encodeUri( { { u"url"_s, QUrl::fromLocalFile( original + u"/grid.covjson"_s ).toString() } } );
      QgsProject project;
      project.addMapLayer( new QgsVectorLayer( uri, u"vector"_s, u"edr"_s ) );
      project.addMapLayer( new QgsRasterLayer( uri, u"raster"_s, u"edr_raster"_s ) );
      QVERIFY( project.write( original + u"/project.qgs"_s ) );
      QVERIFY( root.rename( u"original"_s, u"moved"_s ) );
      QgsProject restored;
      QVERIFY( restored.read( moved + u"/project.qgs"_s ) );
      QCOMPARE( restored.mapLayers().size(), 2 );
      const QMap<QString, QgsMapLayer *> layers = restored.mapLayers();
      for ( QgsMapLayer *layer : layers )
        QVERIFY( layer->isValid() );
    }
};
QGSTEST_MAIN( TestQgsEdrRasterProvider )
#include "testqgsedrrasterprovider.moc"
