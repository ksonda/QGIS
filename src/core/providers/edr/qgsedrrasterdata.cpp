/***************************************************************************
    qgsedrrasterdata.cpp
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

#include "qgsedrrasterdata.h"

#include <cmath>
#include <limits>

#include <QString>

using namespace Qt::StringLiterals;

static double coordinate( const QJsonValue &v )
{
  if ( !v.isDouble() || !std::isfinite( v.toDouble() ) )
    throw QgsEdrError( QObject::tr( "Spatial coordinates must be finite numbers" ) );
  return v.toDouble();
}

struct QgsEdrGridAxis
{
    double minimum;
    double maximum;
    bool ascending;
};

static QgsEdrGridAxis gridAxis( const QgsEdrAxis &axis )
{
  const int n = axis.values.size();
  const double first = coordinate( axis.values.first() ), last = coordinate( axis.values.last() );
  double step = n > 1 ? ( last - first ) / ( n - 1 ) : 0;
  if ( n == 1 )
  {
    if ( axis.bounds.size() != 2 )
      throw QgsEdrError( QObject::tr( "A singleton raster axis needs explicit bounds; use the vector provider for a point" ) );
    const double a = coordinate( axis.bounds[0] ), b = coordinate( axis.bounds[1] );
    if ( !std::isfinite( b - a ) || a == b || first < std::min( a, b ) || first > std::max( a, b ) )
      throw QgsEdrError( QObject::tr( "Invalid singleton raster bounds" ) );
    return { std::min( a, b ), std::max( a, b ), true };
  }
  if ( step == 0 || !std::isfinite( step ) )
    throw QgsEdrError( QObject::tr( "Raster axes must be strictly monotonic" ) );
  double min = std::min( first, last ) - std::abs( step ) / 2, max = std::max( first, last ) + std::abs( step ) / 2;
  if ( !std::isfinite( min ) || !std::isfinite( max ) || !std::isfinite( max - min ) )
    throw QgsEdrError( QObject::tr( "Raster axis extent exceeds the numeric range" ) );
  for ( int i = 0; i < n; ++i )
  {
    const double expected = first + step * i;
    if ( std::abs( coordinate( axis.values[i] ) - expected ) > std::abs( step ) * 1e-7 )
      throw QgsEdrError( QObject::tr( "Raster axes must be regularly spaced; use the vector provider for irregular grids" ) );
    if ( !axis.bounds.isEmpty() )
    {
      const double lo = std::min( coordinate( axis.bounds[2 * i] ), coordinate( axis.bounds[2 * i + 1] ) );
      const double hi = std::max( coordinate( axis.bounds[2 * i] ), coordinate( axis.bounds[2 * i + 1] ) );
      if ( std::abs( lo - ( expected - std::abs( step ) / 2 ) ) > std::abs( step ) * 1e-7 || std::abs( hi - ( expected + std::abs( step ) / 2 ) ) > std::abs( step ) * 1e-7 )
        throw QgsEdrError( QObject::tr( "Raster cell bounds are not a regular grid; use the vector provider" ) );
    }
  }
  return { min, max, step > 0 };
}

QgsEdrRasterData qgsEdrRasterData( const QgsEdrDataset &dataset, const QgsEdrQuery &q )
{
  if ( !dataset.geojson.isEmpty() || dataset.coverages.isEmpty() )
    throw QgsEdrError( QObject::tr( "Raster layers require a CoverageJSON grid" ) );
  if ( !q.targetCrs.isEmpty() )
    throw QgsEdrError( QObject::tr( "Use QGIS on-the-fly reprojection for rasters; targetCrs applies to vector layers" ) );
  QgsEdrRasterData data;
  data.metadata = dataset.metadata;
  qint64 total = 0;
  for ( const QgsEdrCoverage &c : dataset.coverages )
  {
    if ( !c.axes.contains( u"x"_s ) || !c.axes.contains( u"y"_s ) || c.axes.contains( u"composite"_s ) )
      throw QgsEdrError( QObject::tr( "This coverage is not a rectilinear grid; use a vector layer" ) );
    const QgsEdrGridAxis x = gridAxis( c.axes[u"x"_s] ), y = gridAxis( c.axes[u"y"_s] );
    const int nx = c.axes[u"x"_s].values.size(), ny = c.axes[u"y"_s].values.size();
    const QgsRectangle extent( x.minimum, y.minimum, x.maximum, y.maximum );
    const QgsCoordinateReferenceSystem crs = qgsEdrCoverageCrs( c );
    if ( data.width && ( nx != data.width || ny != data.height || extent != data.extent || crs != data.crs ) )
      throw QgsEdrError( QObject::tr( "Coverages have different grids; select one with the coverage URI parameter" ) );
    data.width = nx;
    data.height = ny;
    data.extent = extent;
    data.crs = crs;
    QStringList nonSpatial;
    for ( const QString &key : c.axes.keys() )
      if ( key != "x"_L1 && key != "y"_L1 )
        nonSpatial.append( key );
    for ( auto range = c.ranges.begin(); range != c.ranges.end(); ++range )
    {
      if ( range.value().dataType == "string"_L1 )
        throw QgsEdrError( QObject::tr( "String ranges require a vector layer" ) );
      QMap<QString, int> index;
      bool done = false;
      while ( !done )
      {
        total += qint64( nx ) * ny;
        if ( total > q.limits.maximumCells )
          throw QgsEdrError( QObject::tr( "Raster values across all bands exceed maxCells" ) );
        QgsEdrRasterBand band;
        band.name = range.key();
        if ( !c.id.isEmpty() )
          band.name += u" | "_s + c.id;
        band.metadata = c.parameters.value( range.key() ).toObject();
        const QJsonValue symbol = band.metadata.value( u"unit"_s ).toObject().value( u"symbol"_s );
        band.unit = symbol.isObject() ? symbol.toObject().value( u"value"_s ).toString() : symbol.toString();
        QJsonObject coords;
        for ( const QString &key : std::as_const( nonSpatial ) )
        {
          const QJsonValue value = c.axes[key].values.at( index.value( key ) );
          band.name += u" | "_s + key + '=' + qgsEdrCompactJson( value );
          coords.insert( key, value );
        }
        band.metadata.insert( u"coordinates"_s, coords );
        band.values.resize( nx * ny );
        for ( int row = 0; row < ny; ++row )
          for ( int col = 0; col < nx; ++col )
          {
            index[u"x"_s] = x.ascending ? col : nx - col - 1;
            index[u"y"_s] = y.ascending ? ny - row - 1 : row;
            const QJsonValue v = range.value().at( index );
            band.values[row * nx + col] = v.isNull() ? std::numeric_limits<double>::quiet_NaN() : v.toDouble();
          }
        data.bands.append( std::move( band ) );
        int j = nonSpatial.size() - 1;
        for ( ; j >= 0; --j )
        {
          int &v = index[nonSpatial[j]];
          if ( ++v < c.axes[nonSpatial[j]].values.size() )
            break;
          v = 0;
        }
        done = j < 0;
      }
    }
  }
  return data;
}
