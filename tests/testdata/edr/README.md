# CoverageJSON fixtures

These are synthetic fixtures for the native EDR provider tests. They do not contain downloaded observations or require a public service.

The files named after the 12 standard CoverageJSON domain types exercise point, grid, profile, polygon, trajectory, and section coordinates. Temperature values and times are chosen so tests can check the mapping between domain axes, flattened ranges, feature attributes, raster rows, and chart series.

`collection.covjson` exercises collection-level parameter inheritance. `categories.covjson` and `string-categories.covjson` exercise categorical ranges. `custom.covjson` retains an additional dimension, and `empty.covjson` is an empty collection.

`tiled.covjson` references the domain and range documents under `linked/`. Its range references four edge tiles. The resulting row-major values are `0, 1, 2, 10, 11, 12`.

The C++ tests also construct malformed and boundary cases in memory or temporary directories. HTTP tests serve these objects on an ephemeral loopback port.
