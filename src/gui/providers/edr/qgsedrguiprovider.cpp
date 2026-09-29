/***************************************************************************
    qgsedrguiprovider.cpp
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

#include "qgsedrguiprovider.h"

#include "qgsabstractdatasourcewidget.h"
#include "qgsapplication.h"
#include "qgsauthconfigselect.h"
#include "qgsedrclient.h"
#include "qgsproviderguimetadata.h"
#include "qgssettings.h"
#include "qgssourceselectprovider.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

class QgsEdrSourceSelect final : public QgsAbstractDataSourceWidget
{
    Q_OBJECT
  public:
    QgsEdrSourceSelect( QWidget *parent, Qt::WindowFlags flags, QgsProviderRegistry::WidgetMode mode )
      : QgsAbstractDataSourceWidget( parent, flags, mode )
    {
      setObjectName( u"edrSourceWidget"_s );
      QVBoxLayout *layout = new QVBoxLayout( this );
      QScrollArea *scroll = new QScrollArea( this );
      scroll->setWidgetResizable( true );
      QWidget *body = new QWidget;
      QFormLayout *form = new QFormLayout( body );
      scroll->setWidget( body );
      layout->addWidget( scroll );
      mSaved = new QComboBox;
      mSaved->setEditable( true );
      mSaved->setObjectName( u"edrConnection"_s );
      QgsSettings settings;
      settings.beginGroup( u"edr/connections"_s );
      mSaved->addItems( settings.childGroups() );
      form->addRow( tr( "Saved connection" ), mSaved );
      mUrl = new QLineEdit;
      mUrl->setObjectName( u"edrUrl"_s );
      mUrl->setPlaceholderText( u"https://example.org/edr"_s );
      form->addRow( tr( "Service URL or local file URL" ), mUrl );
      mAuth = new QgsAuthConfigSelect( this, u"edr"_s );
      form->addRow( tr( "Authentication" ), mAuth );
      QHBoxLayout *actions = new QHBoxLayout;
      QPushButton *discover = new QPushButton( tr( "Connect" ) );
      discover->setObjectName( u"edrConnect"_s );
      actions->addWidget( discover );
      QPushButton *save = new QPushButton( tr( "Save connection" ) );
      actions->addWidget( save );
      QPushButton *remove = new QPushButton( tr( "Remove connection" ) );
      actions->addWidget( remove );
      form->addRow( actions );
      mCollection = combo( u"edrCollection"_s );
      form->addRow( tr( "Collection" ), mCollection );
      mInstance = combo( u"edrInstance"_s );
      mInstance->setPlaceholderText( tr( "Collection default" ) );
      form->addRow( tr( "Instance (optional)" ), mInstance );
      QPushButton *instanceButton = new QPushButton( tr( "List instances" ) );
      form->addRow( instanceButton );
      QPushButton *detailsButton = new QPushButton( tr( "Read collection / instance metadata" ) );
      form->addRow( detailsButton );
      mKind = combo( u"edrQuery"_s );
      mKind->setEditable( false );
      mKind->addItems( QgsEdrQuery::queryTypes() );
      form->addRow( tr( "Query" ), mKind );
      mId = new QLineEdit;
      mId->setObjectName( u"edrId"_s );
      form->addRow( tr( "Item or location identifier" ), mId );
      mMode = new QComboBox;
      mMode->addItems( { tr( "Vector samples / polygons" ), tr( "Raster grid" ) } );
      mMode->setObjectName( u"edrMode"_s );
      form->addRow( tr( "Layer type" ), mMode );
      mMethod = new QComboBox;
      mMethod->addItems( { u"GET"_s, tr( "POST (form body)" ), tr( "POST (JSON body)" ), tr( "POST (query parameters)" ) } );
      mMethod->setToolTip( tr( "Choose the request encoding defined by this service's OpenAPI document." ) );
      form->addRow( tr( "HTTP method" ), mMethod );
      mParameterForm = new QFormLayout;
      const QList<QPair<QString, QString>> parameters = {
        { u"coords"_s, tr( "Coordinates (EDR WKT)" ) },
        { u"bbox"_s, tr( "Bounding box" ) },
        { u"z"_s, tr( "Vertical value / interval / list" ) },
        { u"datetime"_s, tr( "Time / interval" ) },
        { u"parameter-name"_s, tr( "Parameters (comma-separated)" ) },
        { u"crs"_s, tr( "Query coordinate reference system" ) },
        { u"f"_s, tr( "Response format" ) },
        { u"within"_s, tr( "Radius" ) },
        { u"within-units"_s, tr( "Radius units" ) },
        { u"corridor-width"_s, tr( "Corridor width" ) },
        { u"width-units"_s, tr( "Width units" ) },
        { u"corridor-height"_s, tr( "Corridor height" ) },
        { u"height-units"_s, tr( "Height units" ) },
        { u"resolution-x"_s, tr( "X resolution" ) },
        { u"resolution-y"_s, tr( "Y resolution" ) },
        { u"resolution-z"_s, tr( "Z resolution" ) },
        { u"limit"_s, tr( "Page size" ) }
      };
      for ( const QPair<QString, QString> &entry : parameters )
      {
        QComboBox *field = combo( u"edr_"_s + entry.first );
        mParameters.insert( entry.first, field );
        mParameterForm->addRow( entry.second, field );
      }
      mParameters[u"coords"_s]->setPlaceholderText( u"POINT(-75 40)"_s );
      mParameters[u"datetime"_s]->setPlaceholderText( u"2026-01-01T00:00:00Z/.."_s );
      mParameters[u"f"_s]->addItems( { u""_s, u"json"_s, u"CoverageJSON"_s, u"application/vnd.cov+json"_s, u"application/prs.coverage+json"_s, u"GeoJSON"_s } );
      mParameters[u"f"_s]->setToolTip( tr( "Leave blank to negotiate CoverageJSON or GeoJSON with the HTTP Accept header, or use an advertised format alias." ) );
      form->addRow( mParameterForm );
      mCustom = new QPlainTextEdit;
      mCustom->setObjectName( u"edrCustom"_s );
      mCustom->setPlaceholderText( u"{\"ensemble\": \"0/9\"}"_s );
      mCustom->setMaximumHeight( 75 );
      form->addRow( tr( "Custom dimensions (JSON object)" ), mCustom );
      mGeometry = new QComboBox;
      mGeometry->addItems( { tr( "All compatible geometries" ), u"point"_s, u"polygon"_s } );
      form->addRow( tr( "Geometry filter" ), mGeometry );
      mCoverage = new QLineEdit;
      form->addRow( tr( "Coverage ID (optional)" ), mCoverage );
      mMaxCells = new QLineEdit( u"1000000"_s );
      form->addRow( tr( "Maximum cells / raster values" ), mMaxCells );
      QHBoxLayout *inspectRow = new QHBoxLayout;
      mInspect = new QComboBox;
      mInspect->addItems( QgsEdrQuery::resources() );
      inspectRow->addWidget( mInspect );
      QPushButton *inspectButton = new QPushButton( tr( "Inspect resource" ) );
      inspectRow->addWidget( inspectButton );
      form->addRow( inspectRow );
      mPreview = new QPlainTextEdit;
      mPreview->setReadOnly( true );
      mPreview->setObjectName( u"edrPreview"_s );
      mPreview->setMinimumHeight( 130 );
      form->addRow( tr( "Metadata / request preview" ), mPreview );
      QHBoxLayout *buttons = new QHBoxLayout;
      QPushButton *previewButton = new QPushButton( tr( "Preview request" ) );
      buttons->addWidget( previewButton );
      QPushButton *add = new QPushButton( tr( "Add layer" ) );
      add->setObjectName( u"edrAdd"_s );
      buttons->addWidget( add );
      layout->addLayout( buttons );
      connect( discover, &QPushButton::clicked, this, [this] { guarded( [this] { discoverCollections(); } ); } );
      connect( save, &QPushButton::clicked, this, [this] {
        guarded( [this] {
          if ( mSaved->currentText().trimmed().isEmpty() || mSaved->currentText().contains( '/' ) )
            throw QgsEdrError( tr( "Enter a connection name without slashes" ) );
          QgsEdrQuery q = baseQuery();
          q.validate();
          QgsSettings s;
          const QString key = u"edr/connections/"_s + mSaved->currentText();
          s.setValue( key + u"/url"_s, mUrl->text() );
          s.setValue( key + u"/authcfg"_s, mAuth->configId() );
          if ( mSaved->findText( mSaved->currentText() ) < 0 )
            mSaved->addItem( mSaved->currentText() );
          mPreview->setPlainText( tr( "Connection saved. Refresh OGC EDR in the Browser to see it." ) );
          emit connectionsChanged();
        } );
      } );
      connect( remove, &QPushButton::clicked, this, [this] {
        QgsSettings s;
        s.remove( u"edr/connections/"_s + mSaved->currentText() );
        const int i = mSaved->findText( mSaved->currentText() );
        if ( i >= 0 )
          mSaved->removeItem( i );
        emit connectionsChanged();
      } );
      connect( mSaved, &QComboBox::currentTextChanged, this, [this]( const QString &name ) {
        QgsSettings s;
        const QString key = u"edr/connections/"_s + name;
        if ( s.contains( key + u"/url"_s ) )
        {
          mUrl->setText( s.value( key + u"/url"_s ).toString() );
          mAuth->setConfigId( s.value( key + u"/authcfg"_s ).toString() );
        }
      } );
      connect( mUrl, &QLineEdit::textChanged, this, [this] {
        mMetadata = {};
        mMetadataUrl = QUrl();
        mCollectionLinks.clear();
        mInstanceLinks.clear();
      } );
      connect( mCollection, &QComboBox::currentTextChanged, this, [this] {
        mMetadata = {};
        mMetadataUrl = QUrl();
        mInstanceLinks.clear();
        mInstance->clear();
      } );
      connect( mInstance, &QComboBox::currentTextChanged, this, [this] {
        mMetadata = {};
        mMetadataUrl = QUrl();
      } );
      connect( instanceButton, &QPushButton::clicked, this, [this] {
        guarded( [this] {
          QgsEdrQuery q = baseQuery();
          q.kind = u"instances"_s;
          q.instance.clear();
          QgsEdrClient client( q );
          q.href = qgsEdrLinkUrl( mMetadata, mMetadataUrl, u"instances"_s );
          if ( q.href.isEmpty() )
          {
            const QString href = mMetadata.value( u"data_queries"_s ).toObject().value( u"instances"_s ).toObject().value( u"link"_s ).toObject().value( u"href"_s ).toString();
            if ( !href.isEmpty() && !href.contains( '{' ) )
              q.href = mMetadataUrl.resolved( QUrl( href ) );
          }
          mInstanceLinks.clear();
          mInstance->clear();
          mInstance->addItem( u""_s );
          for ( const QgsEdrDocument &doc : client.pages( q.request() ) )
            for ( const QJsonValue &value : doc.object.value( u"instances"_s ).toArray() )
            {
              const QJsonObject object = value.toObject();
              const QString id = object.value( u"id"_s ).toString();
              mInstanceLinks.insert( id, qgsEdrLinkUrl( object, doc.url, u"self"_s ) );
              mInstance->addItem( id );
            }
        } );
      } );
      connect( detailsButton, &QPushButton::clicked, this, [this] { guarded( [this] { readMetadata(); } ); } );
      connect( mKind, &QComboBox::currentTextChanged, this, [this] { updateParameters(); } );
      connect( inspectButton, &QPushButton::clicked, this, [this] {
        guarded( [this] {
          QgsEdrQuery q = baseQuery();
          q.kind = mInspect->currentText();
          QgsEdrClient client( q );
          const QgsEdrDocument doc = QStringList { u"landing"_s, u"api"_s, u"conformance"_s, u"collections"_s }.contains( q.kind ) ? client.discover( q.kind ) : client.fetch( q.request() );
          mPreview->setPlainText( QString::fromUtf8( QJsonDocument( doc.object ).toJson() ) );
        } );
      } );
      connect( previewButton, &QPushButton::clicked, this, [this] {
        guarded( [this] {
          const QgsEdrRequest request = layerQuery().request();
          mPreview->setPlainText( QString::fromLatin1( request.method ) + ' ' + request.url.toString( QUrl::FullyEncoded ) + '\n' + request.contentType + '\n' + request.body );
        } );
      } );
      connect( add, &QPushButton::clicked, this, [this] {
        guarded( [this] {
          const QgsEdrQuery q = layerQuery();
          q.validate();
          const QString name = q.collection.isEmpty() ? u"CoverageJSON"_s : q.collection + u" / "_s + q.kind;
          emit addLayer( mMode->currentIndex() == 1 ? Qgis::LayerType::Raster : Qgis::LayerType::Vector, q.toUri(), name, mMode->currentIndex() == 1 ? u"edr_raster"_s : u"edr"_s );
        } );
      } );
      if ( mSaved->count() )
      {
        const QString name = mSaved->currentText();
        mUrl->setText( settings.value( name + u"/url"_s ).toString() );
        mAuth->setConfigId( settings.value( name + u"/authcfg"_s ).toString() );
      }
      updateParameters();
    }

  private:
    QComboBox *mSaved, *mCollection, *mInstance, *mKind, *mMode, *mMethod, *mInspect, *mGeometry;
    QLineEdit *mUrl, *mId, *mCoverage, *mMaxCells;
    QgsAuthConfigSelect *mAuth;
    QPlainTextEdit *mCustom, *mPreview;
    QFormLayout *mParameterForm;
    QMap<QString, QComboBox *> mParameters;
    QJsonObject mMetadata;
    QUrl mMetadataUrl;
    QMap<QString, QUrl> mCollectionLinks, mInstanceLinks;
    QComboBox *combo( const QString &name )
    {
      QComboBox *c = new QComboBox;
      c->setEditable( true );
      c->setObjectName( name );
      c->setSizeAdjustPolicy( QComboBox::AdjustToMinimumContentsLengthWithIcon );
      c->setMinimumContentsLength( 20 );
      return c;
    }
    void guarded( const std::function<void()> &action )
    {
      try
      {
        action();
      }
      catch ( const std::exception &e )
      {
        mPreview->setPlainText( tr( "Error: %1" ).arg( QString::fromUtf8( e.what() ) ) );
      }
    }
    QgsEdrQuery baseQuery() const
    {
      QgsEdrQuery q;
      q.root = QUrl::fromUserInput( mUrl->text().trimmed() );
      q.authcfg = mAuth->configId();
      q.collection = mCollection->currentText();
      q.instance = mInstance->currentText();
      return q;
    }
    QgsEdrQuery layerQuery() const
    {
      QgsEdrQuery q = baseQuery();
      q.kind = q.root.isLocalFile() ? QString() : mKind->currentText();
      q.id = mId->text();
      q.coverage = mCoverage->text();
      if ( mGeometry->currentIndex() )
        q.geometry = mGeometry->currentText();
      bool ok;
      q.limits.maximumCells = mMaxCells->text().toLongLong( &ok );
      if ( !ok || q.limits.maximumCells < 1 || q.limits.maximumCells > 2147483647 )
        throw QgsEdrError( tr( "Maximum cells must be a positive integer up to 2147483647" ) );
      if ( mMethod->currentIndex() )
      {
        q.method = u"POST"_s;
        q.postEncoding = mMethod->currentIndex() == 2 ? u"json"_s : mMethod->currentIndex() == 3 ? u"query"_s : u"form"_s;
      }
      for ( auto i = mParameters.begin(); i != mParameters.end(); ++i )
        if ( !i.value()->isHidden() && !i.value()->currentText().trimmed().isEmpty() )
          q.parameters.insert( i.key(), i.value()->currentText().trimmed() );
      if ( !mCustom->toPlainText().trimmed().isEmpty() )
      {
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson( mCustom->toPlainText().toUtf8(), &error );
        if ( error.error != QJsonParseError::NoError || !doc.isObject() )
          throw QgsEdrError( tr( "Custom dimensions must be a JSON object" ) );
        const QJsonObject object = doc.object();
        for ( auto i = object.begin(); i != object.end(); ++i )
          q.parameters.insert( i.key(), qgsEdrCompactJson( i.value() ) );
      }
      QString key = q.kind == "item"_L1 ? u"items"_s : q.kind == "location"_L1 ? u"locations"_s : q.kind;
      QString href = mMetadata.value( u"data_queries"_s ).toObject().value( key ).toObject().value( u"link"_s ).toObject().value( u"href"_s ).toString();
      if ( q.kind == "item"_L1 || q.kind == "location"_L1 )
      {
        const QString placeholder = q.kind == "item"_L1 ? u"{itemId}"_s : u"{locationId}"_s;
        if ( href.contains( placeholder ) )
          href.replace( placeholder, QString::fromLatin1( QUrl::toPercentEncoding( q.id ) ) );
        else
          href.clear();
      }
      if ( !href.isEmpty() && !href.contains( '{' ) )
        q.href = mMetadataUrl.resolved( QUrl( href ) );
      return q;
    }
    void discoverCollections()
    {
      QgsEdrQuery q = baseQuery();
      QgsEdrClient client( q );
      const QgsEdrDocument doc = client.discover( u"collections"_s );
      QgsEdrRequest request;
      request.url = doc.url;
      mCollectionLinks.clear();
      mInstanceLinks.clear();
      mCollection->clear();
      mInstance->clear();
      for ( const QgsEdrDocument &page : client.pages( request ) )
        for ( const QJsonValue &value : page.object.value( u"collections"_s ).toArray() )
        {
          const QJsonObject object = value.toObject();
          const QString id = object.value( u"id"_s ).toString();
          mCollectionLinks.insert( id, qgsEdrLinkUrl( object, page.url, u"self"_s ) );
          mCollection->addItem( id );
        }
      if ( mCollection->count() )
        readMetadata();
      else
        mPreview->setPlainText( tr( "The service returned no collections." ) );
    }
    void readMetadata()
    {
      QgsEdrQuery q = baseQuery();
      q.kind = q.instance.isEmpty() ? u"collection"_s : u"instance"_s;
      QgsEdrClient client( q );
      q.href = q.instance.isEmpty() ? mCollectionLinks.value( q.collection ) : mInstanceLinks.value( q.instance );
      const QgsEdrDocument doc = client.fetch( q.request() );
      mMetadata = doc.object;
      mMetadataUrl = doc.url;
      mPreview->setPlainText( QString::fromUtf8( QJsonDocument( doc.object ).toJson() ) );
      const QJsonObject queries = mMetadata.value( u"data_queries"_s ).toObject();
      mKind->clear();
      for ( const QString &kind : QgsEdrQuery::queryTypes() )
        if ( queries.contains( kind ) || ( kind == "item"_L1 && queries.contains( u"items"_s ) ) || ( kind == "location"_L1 && queries.contains( u"locations"_s ) ) )
          mKind->addItem( kind );
      if ( !mKind->count() )
        mKind->addItems( QgsEdrQuery::queryTypes() );
      mParameters[u"parameter-name"_s]->clear();
      mParameters[u"parameter-name"_s]->addItem( u""_s );
      mParameters[u"parameter-name"_s]->addItems( mMetadata.value( u"parameter_names"_s ).toObject().keys() );
      updateParameters();
    }
    void updateParameters()
    {
      const QString kind = mKind->currentText();
      QSet<QString> visible { u"datetime"_s, u"parameter-name"_s, u"crs"_s, u"f"_s, u"z"_s, u"limit"_s };
      if ( QStringList { u"position"_s, u"radius"_s, u"area"_s, u"trajectory"_s, u"corridor"_s }.contains( kind ) )
        visible.insert( u"coords"_s );
      if ( QStringList { u"cube"_s, u"items"_s, u"locations"_s }.contains( kind ) )
        visible.insert( u"bbox"_s );
      if ( kind == "radius"_L1 )
        visible.unite( { u"within"_s, tr( "within-units" ) } );
      if ( kind == "corridor"_L1 )
        visible.unite( { u"corridor-width"_s, u"width-units"_s, u"corridor-height"_s, u"height-units"_s } );
      if ( kind == "cube"_L1 || kind == "corridor"_L1 )
        visible.unite( { u"resolution-x"_s, u"resolution-y"_s, u"resolution-z"_s } );
      for ( auto i = mParameters.begin(); i != mParameters.end(); ++i )
      {
        i.value()->setVisible( visible.contains( i.key() ) );
        mParameterForm->labelForField( i.value() )->setVisible( visible.contains( i.key() ) );
      }
      const QString key = kind == "item"_L1 ? u"items"_s : kind == "location"_L1 ? u"locations"_s : kind;
      const QJsonObject link = mMetadata.value( u"data_queries"_s ).toObject().value( key ).toObject().value( u"link"_s ).toObject();
      const QJsonObject vars = link.value( u"variables"_s ).toObject();
      for ( const QString &name : { u"within-units"_s, u"width-units"_s, u"height-units"_s } )
      {
        const QJsonArray values = vars.value( QString( name ).replace( '-', '_' ) ).toArray();
        if ( !values.isEmpty() )
        {
          mParameters[name]->clear();
          for ( const QJsonValue &value : values )
            mParameters[name]->addItem( value.toString() );
        }
      }
      const QJsonArray crs = vars.value( u"crs_details"_s ).toArray();
      if ( !crs.isEmpty() )
      {
        mParameters[u"crs"_s]->clear();
        mParameters[u"crs"_s]->addItem( u""_s );
        for ( const QJsonValue &c : crs )
          mParameters[u"crs"_s]->addItem( c.toObject().value( u"crs"_s ).toString() );
      }
      const QJsonArray formats = vars.value( u"output_formats"_s ).toArray();
      mParameters[u"f"_s]->clear();
      if ( !formats.isEmpty() )
        for ( const QJsonValue &v : formats )
          mParameters[u"f"_s]->addItem( v.toString() );
      else
        mParameters[u"f"_s]->addItems( { u""_s, u"json"_s, u"CoverageJSON"_s, u"application/vnd.cov+json"_s, u"GeoJSON"_s } );
      const bool list = kind == "items"_L1 || kind == "locations"_L1;
      for ( int i = 0; !formats.isEmpty() && i < mParameters[u"f"_s]->count(); ++i )
      {
        const QString text = mParameters[u"f"_s]->itemText( i ).toLower();
        if ( list ? text.contains( u"geo"_s ) : text.contains( u"cov"_s ) )
        {
          mParameters[u"f"_s]->setCurrentIndex( i );
          break;
        }
      }
    }
};

class QgsEdrSourceSelectProvider final : public QgsSourceSelectProvider
{
  public:
    QString providerKey() const override { return u"edr"_s; }
    QString text() const override { return u"EDR"_s; }
    QString toolTip() const override { return QObject::tr( "OGC API Environmental Data Retrieval" ); }
    QIcon icon() const override { return QgsApplication::getThemeIcon( u"/mIconWcs.svg"_s ); }
    int ordering() const override { return OrderRemoteProvider + 80; }
    QgsAbstractDataSourceWidget *createDataSourceWidget( QWidget *parent, Qt::WindowFlags flags, QgsProviderRegistry::WidgetMode mode ) const override
    {
      return new QgsEdrSourceSelect( parent, flags, mode );
    }
};

QgsEdrProviderGuiMetadata::QgsEdrProviderGuiMetadata()
  : QgsProviderGuiMetadata( u"edr"_s )
{}
QList<QgsSourceSelectProvider *> QgsEdrProviderGuiMetadata::sourceSelectProviders()
{
  return { new QgsEdrSourceSelectProvider };
}

#include "qgsedrguiprovider.moc"
