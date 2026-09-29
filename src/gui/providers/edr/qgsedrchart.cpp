/***************************************************************************
    qgsedrchart.cpp
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

#include "qgsedrchart.h"

#include <algorithm>
#include <cmath>

#include "qgsfeatureiterator.h"
#include "qgsgui.h"
#include "qgslayertreeview.h"
#include "qgsmapcanvas.h"
#include "qgsmaplayeraction.h"
#include "qgsmaplayeractionregistry.h"
#include "qgsmaptoolemitpoint.h"
#include "qgsproject.h"
#include "qgsvectorlayer.h"

#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QString>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWheelEvent>

using namespace Qt::StringLiterals;

namespace
{
  QString number( double value )
  {
    return QLocale().toString( value, 'g', 10 );
  }
  bool eligible( QgsMapLayer *layer )
  {
    return layer && layer->isValid() && layer->providerType() == "edr"_L1 && qobject_cast<QgsVectorLayer *>( layer );
  }
} //namespace

QgsEdrChartPlot::QgsEdrChartPlot( QWidget *parent )
  : QWidget( parent )
{
  setObjectName( u"edrChartPlot"_s );
  setAccessibleName( tr( "EDR time series plot" ) );
  setMinimumSize( 450, 210 );
  setMouseTracking( true );
  setFocusPolicy( Qt::StrongFocus );
  clear( tr( "Choose an EDR vector layer with time observations." ) );
}

void QgsEdrChartPlot::clear( const QString &message )
{
  mSamples.clear();
  mMessage = message;
  mHover = -1;
  setAccessibleDescription( message );
  update();
  emit sampleHovered( QString() );
}

void QgsEdrChartPlot::setSeries( const QgsEdrChartSeries &series, const QgsEdrChartParameter &parameter )
{
  mSamples = series.samples;
  mParameter = parameter;
  mMessage.clear();
  mHover = -1;
  mDragging = false;
  if ( mSamples.isEmpty() )
  {
    clear( tr( "No observations in this series." ) );
    return;
  }
  mOrigin = double( mSamples.first().time );
  mFullStart = 0;
  mFullEnd = xTime( mSamples.last().time );
  if ( mFullEnd == mFullStart )
  {
    mFullStart -= 3600;
    mFullEnd += 3600;
  }
  else
  {
    const double pad = ( mFullEnd - mFullStart ) * 0.025;
    mFullStart -= pad;
    mFullEnd += pad;
  }
  mLow = std::numeric_limits<double>::infinity();
  mHigh = -mLow;
  for ( const QgsEdrChartSample &sample : std::as_const( mSamples ) )
    if ( std::isfinite( sample.value ) )
    {
      mLow = std::min( mLow, sample.value );
      mHigh = std::max( mHigh, sample.value );
    }
  if ( !std::isfinite( mLow ) )
    mMessage = tr( "All values in this series are missing." );
  else
  {
    const double pad = mHigh == mLow ? std::max( std::abs( mLow ) * 0.05, 1.0 ) : ( mHigh - mLow ) * 0.08;
    mLow -= pad;
    mHigh += pad;
    if ( !std::isfinite( mLow ) || !std::isfinite( mHigh ) || !std::isfinite( mHigh - mLow ) )
      mMessage = tr( "Values exceed the chart's numeric range." );
  }
  setAccessibleDescription( tr( "%1. %2 observations. %3. Times are UTC." ).arg( parameter.title() ).arg( mSamples.size() ).arg( series.label ) );
  resetView();
}

void QgsEdrChartPlot::resetView()
{
  mStart = mFullStart;
  mEnd = mFullEnd;
  mHover = -1;
  update();
  emit sampleHovered( QString() );
}

QRectF QgsEdrChartPlot::plotRect() const
{
  const QFontMetrics metrics( font() );
  const int labelWidth = std::max( metrics.horizontalAdvance( number( mLow ) ), metrics.horizontalAdvance( number( mHigh ) ) );
  return QRectF( 30 + labelWidth, 30, std::max( 1, width() - 60 - labelWidth ), std::max( 1, height() - 95 ) );
}

QPointF QgsEdrChartPlot::samplePosition( int index ) const
{
  return samplePosition( index, plotRect() );
}

QPointF QgsEdrChartPlot::samplePosition( int index, const QRectF &area ) const
{
  const QgsEdrChartSample &sample = mSamples.at( index );
  return { area.left() + ( xTime( sample.time ) - mStart ) / ( mEnd - mStart ) * area.width(), area.bottom() - ( sample.value - mLow ) / ( mHigh - mLow ) * area.height() };
}

void QgsEdrChartPlot::paintEvent( QPaintEvent * )
{
  QPainter painter( this );
  painter.setRenderHint( QPainter::Antialiasing );
  painter.fillRect( rect(), palette().base() );
  painter.setPen( palette().text().color() );
  if ( !mMessage.isEmpty() )
  {
    painter.drawText( rect().adjusted( 25, 15, -25, -15 ), Qt::AlignCenter | Qt::TextWordWrap, mMessage );
    return;
  }
  const QRectF area = plotRect();
  painter.drawText( QRectF( area.left(), 2, area.width(), 25 ), Qt::AlignLeft | Qt::AlignVCenter, mParameter.title() );
  const QColor grid = palette().mid().color();
  for ( int i = 0; i <= 4; ++i )
  {
    const double y = area.bottom() - i * area.height() / 4;
    painter.setPen( QPen( grid, 0.5, Qt::DotLine ) );
    painter.drawLine( QPointF( area.left(), y ), QPointF( area.right(), y ) );
    painter.setPen( palette().text().color() );
    painter.drawText( QRectF( 2, y - 10, area.left() - 10, 20 ), Qt::AlignRight | Qt::AlignVCenter, number( mLow + i * ( mHigh - mLow ) / 4 ) );
  }
  const int ticks = std::max( 2, std::min( 6, int( area.width() / 140 ) ) );
  for ( int i = 0; i < ticks; ++i )
  {
    const double fraction = double( i ) / ( ticks - 1 );
    const double x = area.left() + fraction * area.width();
    const QDateTime time = QDateTime::fromMSecsSinceEpoch( qint64( mOrigin + ( mStart + fraction * ( mEnd - mStart ) ) * 1000 ), Qt::UTC );
    const QString format = ( mEnd - mStart ) < 120 ? u"yyyy-MM-dd\nHH:mm:ss"_s : u"yyyy-MM-dd\nHH:mm"_s;
    painter.setPen( QPen( grid, 0.5, Qt::DotLine ) );
    painter.drawLine( QPointF( x, area.top() ), QPointF( x, area.bottom() ) );
    painter.setPen( palette().text().color() );
    const double left = std::clamp( x - 65, 1.0, std::max( 1.0, double( width() - 131 ) ) );
    painter.drawText( QRectF( left, area.bottom() + 5, 130, 38 ), Qt::AlignHCenter | Qt::AlignTop, time.toString( format ) );
  }
  painter.drawText( QRectF( area.left(), height() - 21, area.width(), 20 ), Qt::AlignCenter, tr( "Time (UTC)" ) );
  painter.setPen( QPen( grid ) );
  painter.drawRect( area );
  painter.save();
  painter.setClipRect( area.adjusted( -4, -4, 4, 4 ) );
  const QColor blue( u"#257bb5"_s );
  painter.setPen( QPen( blue, 2 ) );
  QPainterPath path;
  bool connected = false;
  for ( int i = 0; i < mSamples.size(); ++i )
  {
    if ( !std::isfinite( mSamples[i].value ) )
    {
      connected = false;
      continue;
    }
    const QPointF point = samplePosition( i, area );
    const bool duplicate = ( i > 0 && mSamples[i - 1].time == mSamples[i].time ) || ( i + 1 < mSamples.size() && mSamples[i + 1].time == mSamples[i].time );
    if ( connected && !duplicate )
      path.lineTo( point );
    else
      path.moveTo( point );
    connected = !duplicate;
  }
  painter.drawPath( path );
  painter.setBrush( blue );
  for ( int i = 0; i < mSamples.size(); ++i )
  {
    if ( !std::isfinite( mSamples[i].value ) )
      continue;
    const QPointF point = samplePosition( i, area );
    if ( area.contains( point ) )
      painter.drawEllipse( point, 3, 3 );
  }
  if ( mHover >= 0 )
  {
    const QPointF point = samplePosition( mHover, area );
    painter.setPen( QPen( palette().text().color(), 1, Qt::DashLine ) );
    painter.drawLine( QPointF( point.x(), area.top() ), QPointF( point.x(), area.bottom() ) );
    painter.setBrush( QColor( u"#e49332"_s ) );
    painter.setPen( Qt::NoPen );
    painter.drawEllipse( point, 5, 5 );
  }
  painter.restore();
}

int QgsEdrChartPlot::nearest( const QPointF &position ) const
{
  const QRectF area = plotRect();
  if ( !mMessage.isEmpty() || !area.contains( position ) )
    return -1;
  double distance = 144;
  int closest = -1;
  for ( int i = 0; i < mSamples.size(); ++i )
  {
    if ( !std::isfinite( mSamples[i].value ) )
      continue;
    const QPointF delta = samplePosition( i, area ) - position;
    const double squared = delta.x() * delta.x() + delta.y() * delta.y();
    if ( squared < distance )
    {
      distance = squared;
      closest = i;
    }
  }
  return closest;
}

QString QgsEdrChartPlot::observation( int index ) const
{
  const QgsEdrChartSample &sample = mSamples.at( index );
  return QDateTime::fromMSecsSinceEpoch( sample.time, Qt::UTC ).toString( Qt::ISODateWithMs ) + u"   "_s + number( sample.value ) + ( mParameter.unit.isEmpty() ? QString() : ' ' + mParameter.unit );
}

void QgsEdrChartPlot::mouseMoveEvent( QMouseEvent *event )
{
  if ( mDragging )
  {
    if ( ( event->pos() - mPress ).manhattanLength() > 3 )
      mMoved = true;
    const double delta = ( event->pos().x() - mLast.x() ) * ( mEnd - mStart ) / plotRect().width();
    mStart -= delta;
    mEnd -= delta;
    clampView();
    mLast = event->pos();
    mHover = -1;
    update();
    return;
  }
  const int index = nearest( event->pos() );
  if ( index != mHover )
  {
    mHover = index;
    emit sampleHovered( index < 0 ? QString() : observation( index ) );
    update();
  }
}

void QgsEdrChartPlot::mousePressEvent( QMouseEvent *event )
{
  if ( event->button() == Qt::LeftButton && mMessage.isEmpty() && plotRect().contains( event->pos() ) )
  {
    mDragging = true;
    mMoved = false;
    mPress = mLast = event->pos();
  }
}

void QgsEdrChartPlot::mouseReleaseEvent( QMouseEvent *event )
{
  if ( event->button() == Qt::LeftButton && mDragging && !mMoved )
  {
    const int index = nearest( event->pos() );
    if ( index >= 0 )
      emit sampleSelected( mSamples[index].feature );
  }
  mDragging = false;
}

void QgsEdrChartPlot::leaveEvent( QEvent * )
{
  mHover = -1;
  emit sampleHovered( QString() );
  update();
}

void QgsEdrChartPlot::clampView()
{
  if ( mStart < mFullStart )
  {
    mEnd += mFullStart - mStart;
    mStart = mFullStart;
  }
  if ( mEnd > mFullEnd )
  {
    mStart -= mEnd - mFullEnd;
    mEnd = mFullEnd;
  }
}

void QgsEdrChartPlot::wheelEvent( QWheelEvent *event )
{
  if ( !mMessage.isEmpty() || !plotRect().contains( event->position() ) )
  {
    event->ignore();
    return;
  }
  const double fraction = ( event->position().x() - plotRect().left() ) / plotRect().width();
  const double anchor = mStart + fraction * ( mEnd - mStart );
  const double span = std::clamp( ( mEnd - mStart ) * std::pow( 1.2, -event->angleDelta().y() / 120.0 ), std::min( 0.001, mFullEnd - mFullStart ), mFullEnd - mFullStart );
  mStart = anchor - fraction * span;
  mEnd = mStart + span;
  clampView();
  mHover = -1;
  update();
  event->accept();
}

QgsEdrChartDock::QgsEdrChartDock( QMainWindow *window, QgsMapCanvas *canvas )
  : QDockWidget( tr( "EDR time series" ), window )
  , mCanvas( canvas )
{
  setObjectName( u"edrChartDock"_s );
  setAllowedAreas( Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea );
  QWidget *body = new QWidget( this );
  QVBoxLayout *layout = new QVBoxLayout( body );
  layout->setContentsMargins( 8, 5, 8, 5 );
  QHBoxLayout *first = new QHBoxLayout;
  mLayers = new QComboBox;
  mLayers->setObjectName( u"edrChartLayer"_s );
  mParameters = new QComboBox;
  mParameters->setObjectName( u"edrChartParameter"_s );
  for ( QComboBox *combo : { mLayers, mParameters } )
  {
    combo->setSizeAdjustPolicy( QComboBox::AdjustToMinimumContentsLengthWithIcon );
    combo->setMinimumContentsLength( 18 );
  }
  QLabel *layerLabel = new QLabel( tr( "Layer" ) );
  layerLabel->setBuddy( mLayers );
  QLabel *parameterLabel = new QLabel( tr( "Parameter" ) );
  parameterLabel->setBuddy( mParameters );
  first->addWidget( layerLabel );
  first->addWidget( mLayers, 1 );
  first->addWidget( parameterLabel );
  first->addWidget( mParameters, 1 );
  layout->addLayout( first );
  QHBoxLayout *second = new QHBoxLayout;
  mSeries = new QComboBox;
  mSeries->setObjectName( u"edrChartSeries"_s );
  mSeries->setSizeAdjustPolicy( QComboBox::AdjustToMinimumContentsLengthWithIcon );
  mSeries->setMinimumContentsLength( 25 );
  QLabel *seriesLabel = new QLabel( tr( "Station / series" ) );
  seriesLabel->setBuddy( mSeries );
  second->addWidget( seriesLabel );
  second->addWidget( mSeries, 1 );
  mPick = new QPushButton( tr( "Pick on map" ) );
  mPick->setObjectName( u"edrChartPick"_s );
  mPick->setCheckable( true );
  mPick->setToolTip( tr( "Click a station or geometry in the chosen EDR layer to show its full time series." ) );
  second->addWidget( mPick );
  QPushButton *reset = new QPushButton( tr( "Reset view" ) );
  reset->setObjectName( u"edrChartReset"_s );
  second->addWidget( reset );
  layout->addLayout( second );
  mStatus = new QLabel;
  mStatus->setObjectName( u"edrChartStatus"_s );
  mStatus->setTextFormat( Qt::PlainText );
  layout->addWidget( mStatus );
  mPlot = new QgsEdrChartPlot;
  layout->addWidget( mPlot, 1 );
  mDetail = new QLabel;
  mDetail->setObjectName( u"edrChartDetail"_s );
  mDetail->setTextFormat( Qt::PlainText );
  mDetail->setTextInteractionFlags( Qt::TextSelectableByMouse );
  layout->addWidget( mDetail );
  setWidget( body );
  mRefresh = new QTimer( this );
  mRefresh->setSingleShot( true );
  connect( mRefresh, &QTimer::timeout, this, &QgsEdrChartDock::rebuild );
  connect( mLayers, qOverload<int>( &QComboBox::currentIndexChanged ), this, [this] { setLayer( QgsProject::instance()->mapLayer( mLayers->currentData().toString() ) ); } );
  connect( mParameters, qOverload<int>( &QComboBox::currentIndexChanged ), this, [this] { rebuild(); } );
  connect( mSeries, qOverload<int>( &QComboBox::currentIndexChanged ), this, [this] { displaySeries(); } );
  connect( reset, &QPushButton::clicked, mPlot, &QgsEdrChartPlot::resetView );
  connect( mPlot, &QgsEdrChartPlot::sampleHovered, this, [this]( const QString &text ) {
    mDetail->setText( text.isEmpty() ? tr( "Hover for values. Click an observation to select it on the map. Scroll to zoom; drag to pan." ) : text );
  } );
  connect( mPlot, &QgsEdrChartPlot::sampleSelected, this, [this]( qint64 id ) {
    if ( mLayer )
    {
      mSelecting = true;
      mLayer->selectByIds( { id } );
      mSelecting = false;
    }
  } );
  connect( QgsProject::instance(), &QgsProject::layersAdded, this, [this] { refreshLayers(); } );
  connect( QgsProject::instance(), &QgsProject::layersRemoved, this, [this] { refreshLayers(); } );
  connect( this, &QDockWidget::visibilityChanged, this, [this]( bool visible ) {
    if ( !visible )
      stopPicking();
    else
      rebuild();
  } );
  if ( mCanvas )
  {
    mPickTool = new QgsMapToolEmitPoint( mCanvas );
    mPickTool->setParent( this );
    mPickTool->setCursor( Qt::CrossCursor );
    connect( mPickTool, &QgsMapToolEmitPoint::canvasClicked, this, [this]( const QgsPointXY &point, Qt::MouseButton button ) {
      if ( button == Qt::LeftButton )
        pickAt( point );
      else
        stopPicking();
    } );
    connect( mCanvas, &QgsMapCanvas::mapToolSet, this, [this]( QgsMapTool *tool ) {
      if ( tool != mPickTool )
      {
        QSignalBlocker block( mPick );
        mPick->setChecked( false );
        mPreviousTool.clear();
      }
    } );
    connect( mPick, &QPushButton::toggled, this, [this]( bool enabled ) {
      if ( enabled && mCanvas && mLayer )
      {
        mPreviousTool = mCanvas->mapTool();
        mCanvas->setMapTool( mPickTool );
      }
      else
        stopPicking();
    } );
  }
  refreshLayers();
}

QgsEdrChartDock::~QgsEdrChartDock()
{
  stopPicking();
}

void QgsEdrChartDock::stopPicking()
{
  QSignalBlocker block( mPick );
  mPick->setChecked( false );
  if ( mCanvas && mCanvas->mapTool() == mPickTool && mPickTool )
  {
    if ( mPreviousTool )
      mCanvas->setMapTool( mPreviousTool );
    else
      mCanvas->unsetMapTool( mPickTool );
  }
  mPreviousTool.clear();
}

void QgsEdrChartDock::refreshLayers()
{
  const QString previous = mLayer ? mLayer->id() : QString();
  {
    QSignalBlocker block( mLayers );
    mLayers->clear();
    for ( QgsMapLayer *layer : QgsProject::instance()->mapLayers() )
      if ( eligible( layer ) )
        mLayers->addItem( layer->name(), layer->id() );
    const int index = mLayers->findData( previous );
    if ( index >= 0 )
      mLayers->setCurrentIndex( index );
  }
  setLayer( QgsProject::instance()->mapLayer( mLayers->currentData().toString() ) );
}

void QgsEdrChartDock::setLayer( QgsMapLayer *layer )
{
  QgsVectorLayer *vector = eligible( layer ) ? qobject_cast<QgsVectorLayer *>( layer ) : nullptr;
  if ( mLayer == vector && vector )
    return;
  stopPicking();
  mRefresh->stop();
  if ( mLayer )
    disconnect( mLayer, nullptr, this, nullptr );
  mLayer = vector;
  {
    QSignalBlocker block( mLayers );
    mLayers->setCurrentIndex( vector ? mLayers->findData( vector->id() ) : -1 );
  }
  mData = {};
  mParameterDefinitions.clear();
  {
    QSignalBlocker block( mParameters );
    mParameters->clear();
    if ( vector )
    {
      mParameterDefinitions = qgsEdrChartParameters( vector->fields() );
      for ( const QgsEdrChartParameter &parameter : std::as_const( mParameterDefinitions ) )
        mParameters->addItem( parameter.title() + u" ["_s + parameter.name + ']', parameter.field );
    }
  }
  mPick->setEnabled( mCanvas && vector );
  if ( vector )
  {
    connect( vector, &QgsVectorLayer::selectionChanged, this, [this] {
      if ( !mSelecting )
        followSelection();
    } );
    connect( vector, &QgsVectorLayer::dataChanged, this, [this] { mRefresh->start( 0 ); } );
    connect( vector, &QgsVectorLayer::subsetStringChanged, this, [this] { mRefresh->start( 0 ); } );
    connect( vector, &QObject::destroyed, this, [this] {
      mLayer.clear();
      stopPicking();
      rebuild();
    } );
  }
  rebuild();
}

void QgsEdrChartDock::rebuild()
{
  const QString previous = mSeries->currentData().toString();
  const int previousIndex = mParameters->currentIndex();
  const QString previousName = previousIndex >= 0 && previousIndex < mParameterDefinitions.size() ? mParameterDefinitions[previousIndex].name : QString();
  {
    QSignalBlocker block( mParameters );
    mParameters->clear();
    mParameterDefinitions = mLayer ? qgsEdrChartParameters( mLayer->fields() ) : QVector<QgsEdrChartParameter>();
    for ( const QgsEdrChartParameter &parameter : std::as_const( mParameterDefinitions ) )
    {
      mParameters->addItem( parameter.title() + u" ["_s + parameter.name + ']', parameter.field );
      if ( parameter.name == previousName )
        mParameters->setCurrentIndex( mParameters->count() - 1 );
    }
  }
  mData = {};
  const int parameter = mParameters->currentIndex();
  if ( mLayer && parameter >= 0 && isVisible() )
    mData = qgsEdrChartData( mLayer, mParameterDefinitions[parameter].field );
  {
    QSignalBlocker block( mSeries );
    mSeries->clear();
    for ( const QgsEdrChartSeries &series : std::as_const( mData.series ) )
    {
      mSeries->addItem( series.label.left( 160 ), series.key );
      mSeries->setItemData( mSeries->count() - 1, series.label, Qt::ToolTipRole );
    }
    const int index = mSeries->findData( previous );
    if ( index >= 0 )
      mSeries->setCurrentIndex( index );
  }
  displaySeries();
  followSelection();
}

void QgsEdrChartDock::displaySeries()
{
  const int index = mSeries->currentIndex(), parameter = mParameters->currentIndex();
  mStatus->clear();
  if ( index < 0 || index >= mData.series.size() || parameter < 0 )
  {
    const QString message = !mData.error.isEmpty() ? mData.error : !mLayer ? tr( "Choose an EDR vector layer with time observations." ) : tr( "No numeric, non-categorical time series available." );
    mPlot->clear( message );
    return;
  }
  const QgsEdrChartSeries &series = mData.series[index];
  int valid = 0;
  for ( const QgsEdrChartSample &sample : series.samples )
    if ( std::isfinite( sample.value ) )
      ++valid;
  QString status = tr( "%1 observations; %2 missing values. Series %3 of %4. Times in UTC." ).arg( series.samples.size() ).arg( series.samples.size() - valid ).arg( index + 1 ).arg( mData.series.size() );
  if ( mData.missingTimes )
    status += ' ' + tr( "%1 rows without a Gregorian timestamp were omitted." ).arg( mData.missingTimes );
  mStatus->setText( status );
  mPlot->setSeries( series, mParameterDefinitions[parameter] );
}

void QgsEdrChartDock::followSelection()
{
  if ( !mLayer )
    return;
  QList<QgsFeatureId> ids = mLayer->selectedFeatureIds().values();
  std::sort( ids.begin(), ids.end() );
  // Selection chooses a series, not a single observation. Preserve the current
  // series when several selected features belong to different series.
  for ( QgsFeatureId id : std::as_const( ids ) )
    if ( mData.featureSeries.value( id, -1 ) == mSeries->currentIndex() )
      return;
  for ( QgsFeatureId id : std::as_const( ids ) )
  {
    const int index = mData.featureSeries.value( id, -1 );
    if ( index >= 0 )
    {
      mSeries->setCurrentIndex( index );
      return;
    }
  }
}

void QgsEdrChartDock::showFeature( QgsMapLayer *layer, QgsFeatureId feature )
{
  if ( !eligible( layer ) )
    return;
  setLayer( layer );
  show();
  raise();
  const int index = mData.featureSeries.value( feature, -1 );
  if ( index >= 0 )
    mSeries->setCurrentIndex( index );
}

void QgsEdrChartDock::pickAt( const QgsPointXY &point )
{
  if ( !mCanvas || !mLayer )
    return;
  const double radius = std::max( QgsMapTool::searchRadiusMU( mCanvas ), mCanvas->mapUnitsPerPixel() * 6 );
  QgsFeatureRequest request;
  request.setDestinationCrs( mCanvas->mapSettings().destinationCrs(), QgsProject::instance()->transformContext() );
  request.setFilterRect( QgsRectangle( point.x() - radius, point.y() - radius, point.x() + radius, point.y() + radius ) );
  request.setLimit( 100001 );
  const QgsGeometry target = QgsGeometry::fromPointXY( point );
  double closest = radius;
  QgsFeatureId id = FID_NULL;
  QgsFeatureIterator features = mLayer->getFeatures( request );
  QgsFeature feature;
  while ( features.nextFeature( feature ) )
  {
    if ( !mData.featureSeries.contains( feature.id() ) || feature.geometry().isNull() )
      continue;
    const double distance = feature.geometry().distance( target );
    if ( distance >= 0 && ( distance < closest || ( distance == closest && id == FID_NULL ) ) )
    {
      closest = distance;
      id = feature.id();
    }
  }
  if ( id == FID_NULL )
  {
    mDetail->setText( tr( "No time series at that position in the chosen layer." ) );
    return;
  }
  mSeries->setCurrentIndex( mData.featureSeries.value( id ) );
  mSelecting = true;
  mLayer->selectByIds( { id } );
  mSelecting = false;
}

namespace
{
  class QgsEdrChartAction final : public QgsMapLayerAction
  {
    public:
      explicit QgsEdrChartAction( QgsEdrChartDock *dock )
        : QgsMapLayerAction( QObject::tr( "Show EDR time series" ), dock, Qgis::MapLayerActionTarget::Layer | Qgis::MapLayerActionTarget::SingleFeature )
        , mDock( dock )
      {}
      bool canRunUsingLayer( QgsMapLayer *layer, const QgsMapLayerActionContext & ) const override { return eligible( layer ); }
      void triggerForLayer( QgsMapLayer *layer, const QgsMapLayerActionContext & ) override { mDock->showFeature( layer ); }
      void triggerForFeature( QgsMapLayer *layer, const QgsFeature &feature, const QgsMapLayerActionContext & ) override { mDock->showFeature( layer, feature.id() ); }

    private:
      QgsEdrChartDock *mDock;
  };
} //namespace

void qgsEdrInstallChartPanel( QMainWindow *window )
{
  if ( !window || window->findChild<QgsEdrChartDock *>( u"edrChartDock"_s ) )
    return;
  QgsMapCanvas *canvas = window->findChild<QgsMapCanvas *>( u"theMapCanvas"_s );
  if ( !canvas )
    canvas = window->findChild<QgsMapCanvas *>();
  QgsEdrChartDock *dock = new QgsEdrChartDock( window, canvas );
  window->addDockWidget( Qt::BottomDockWidgetArea, dock );
  dock->hide();
  window->resizeDocks( { dock }, { 360 }, Qt::Vertical );
  QToolBar *toolbar = window->addToolBar( QObject::tr( "EDR time series" ) );
  toolbar->setObjectName( u"edrChartToolbar"_s );
  QAction *toggle = dock->toggleViewAction();
  toggle->setObjectName( u"edrChartAction"_s );
  toolbar->addAction( toggle );
  if ( QgsLayerTreeView *tree = window->findChild<QgsLayerTreeView *>() )
  {
    QObject::connect( tree, &QgsLayerTreeView::currentLayerChanged, dock, [dock]( QgsMapLayer *layer ) {
      if ( eligible( layer ) )
        dock->setLayer( layer );
    } );
    if ( eligible( tree->currentLayer() ) )
      dock->setLayer( tree->currentLayer() );
  }
  QgsEdrChartAction *action = new QgsEdrChartAction( dock );
  QgsMapLayerActionRegistry *registry = QgsGui::mapLayerActionRegistry();
  registry->addMapLayerAction( action );
  QObject::connect( action, &QObject::destroyed, registry, [registry, action] { registry->removeMapLayerAction( action ); } );
}
