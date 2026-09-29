/***************************************************************************
    qgsedrchart.h
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

#ifndef QGSEDRCHART_H
#define QGSEDRCHART_H

#include "qgis_gui.h"
#include "qgsedrchartmodel.h"

#include <QDockWidget>
#include <QPointer>

#define SIP_NO_FILE

class QComboBox;
class QLabel;
class QPushButton;
class QTimer;
class QMainWindow;
class QgsMapCanvas;
class QgsMapLayer;
class QgsMapTool;
class QgsMapToolEmitPoint;
class QgsPointXY;


///@cond PRIVATE

class GUI_EXPORT QgsEdrChartPlot final : public QWidget
{
    Q_OBJECT
  public:
    explicit QgsEdrChartPlot( QWidget *parent = nullptr );
    void setSeries( const QgsEdrChartSeries &series, const QgsEdrChartParameter &parameter );
    void clear( const QString &message );
    void resetView();
    const QVector<QgsEdrChartSample> &samples() const { return mSamples; }
    QRectF plotRect() const;
    QPointF samplePosition( int index ) const;
  signals:
    void sampleSelected( qint64 feature );
    void sampleHovered( const QString &text );

  protected:
    void paintEvent( QPaintEvent * ) override;
    void mouseMoveEvent( QMouseEvent *event ) override;
    void mousePressEvent( QMouseEvent *event ) override;
    void mouseReleaseEvent( QMouseEvent *event ) override;
    void leaveEvent( QEvent * ) override;
    void wheelEvent( QWheelEvent *event ) override;

  private:
    QVector<QgsEdrChartSample> mSamples;
    QgsEdrChartParameter mParameter;
    QString mMessage;
    double mOrigin = 0, mFullStart = 0, mFullEnd = 1, mStart = 0, mEnd = 1, mLow = 0, mHigh = 1;
    int mHover = -1;
    bool mDragging = false, mMoved = false;
    QPoint mPress, mLast;
    double xTime( qint64 time ) const { return ( double( time ) - mOrigin ) / 1000.0; }
    int nearest( const QPointF &position ) const;
    QPointF samplePosition( int index, const QRectF &area ) const;
    QString observation( int index ) const;
    void clampView();
};

class GUI_EXPORT QgsEdrChartDock final : public QDockWidget
{
    Q_OBJECT
  public:
    explicit QgsEdrChartDock( QMainWindow *window, QgsMapCanvas *canvas = nullptr );
    ~QgsEdrChartDock() override;
    void setLayer( QgsMapLayer *layer );
    void showFeature( QgsMapLayer *layer, QgsFeatureId feature = FID_NULL );
    void pickAt( const QgsPointXY &point );
    const QgsEdrChartData &data() const { return mData; }

  private:
    QPointer<QgsVectorLayer> mLayer;
    QPointer<QgsMapCanvas> mCanvas;
    QPointer<QgsMapToolEmitPoint> mPickTool;
    QPointer<QgsMapTool> mPreviousTool;
    QComboBox *mLayers, *mParameters, *mSeries;
    QPushButton *mPick;
    QLabel *mStatus, *mDetail;
    QgsEdrChartPlot *mPlot;
    QTimer *mRefresh;
    QVector<QgsEdrChartParameter> mParameterDefinitions;
    QgsEdrChartData mData;
    bool mSelecting = false;
    void refreshLayers();
    void rebuild();
    void displaySeries();
    void followSelection();
    void stopPicking();
};

GUI_EXPORT void qgsEdrInstallChartPanel( QMainWindow *window );

///@endcond

#endif // QGSEDRCHART_H
