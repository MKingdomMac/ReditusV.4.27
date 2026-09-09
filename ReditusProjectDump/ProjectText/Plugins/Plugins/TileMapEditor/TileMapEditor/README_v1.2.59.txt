TileMapEditor UE4.27 v1.2.59 - Slant Support Diagnostics
=========================================================

Baseline
--------
This diagnostic build is based directly on v1.2.55.

Purpose
-------
This build adds instrumentation for the missing faces that can appear in
Continuous Terrain when a horizontal diagonal cut is stacked directly above
another occupied block near adjacent slanted edges.

No generated positions, indices, masks, topology, collision, or material data
were intentionally changed.

How to capture a diagnostic
---------------------------
1. Select the TileMap terrain actor.
2. Expand Continuous Terrain in Details.
3. Enable the advanced option "Debug Slant Supports".
4. Edit one affected cell, or reopen/rebuild the terrain, to regenerate it.
5. Reproduce the visible hole.
6. In Output Log, filter for: TileMap slant diagnostic
7. Copy every matching line and send it together with one screenshot showing
   the colored debug lines around the hole.
8. Disable "Debug Slant Supports" afterward. It is false by default.

Colors
------
Yellow: source support-polygon boundary.
Cyan: boundary points when all validation tests pass.
Red: boundary points or emitted triangles when validation fails.
Green: emitted triangles that pass degeneracy and containment checks.

The summary reports the source cell, upper slant, rotation, boundary count,
expected and emitted triangle counts, duplicate points, self-intersections,
degenerate/outside triangles, and polygon-versus-triangle area error. Nearby
occupied cells and their rotations are also logged.
