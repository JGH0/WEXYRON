# SpatialKit (vendored snapshot)

`include/spatialkit/geometry.hpp` is a copy of the header-only geometry from
the SpatialKit project (`~/Einzelkleinprojekt/spartialkit`). The game only needs
the continuous-collision geometry (ray/segment/swept AABB); the spatial grid and
its tests stay in the SpatialKit repository.

The Linux Makefile and CMake prefer the live checkout at
`../Einzelkleinprojekt/spartialkit` when it exists, so day-to-day changes there
are picked up directly. This snapshot keeps CI and fresh clones building
without the sibling checkout.

To refresh the snapshot after changing SpatialKit:

```sh
cp ../Einzelkleinprojekt/spartialkit/include/spatialkit/geometry.hpp \
   third_party/spatialkit/include/spatialkit/geometry.hpp
```
