TileMapEditor UE4.27 v1.2.65
================================

Stacked ramp continuity fix
---------------------------

- Separately painted ramp runs with the same direction, segment count, and
  rotation now form one uninterrupted straight slope when the next run begins
  exactly one grid layer higher (or the previous run ends one layer lower).
- Internal stacked ramp-to-ramp joins no longer receive endpoint Hermite
  curvature or an exposed-edge chamfer.
- The existing rounded transition remains at the true ramp endpoint where the
  chain reaches flat terrain.
- Incompatible ramp directions and slopes are not joined.
- v1.2.64 strict hybrid baking and per-block continuous-terrain exclusions are
  preserved.

Validation note
---------------

This source package was statically checked, but must still be compiled and
tested in Unreal Engine 4.27.
