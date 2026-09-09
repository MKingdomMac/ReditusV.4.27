TileMapEditor UE4.27 v1.2.57

Continuous boundary T-junction repair.

- Every boundary-affected top surface now uses RoundedSurfaceAlphas (or the
  corresponding rounded cliff-foot superset) instead of selecting rounded or
  straight samples independently per cell.
- Every visible continuous wall uses the same RoundedSurfaceAlphas superset.
- Stacked horizontal-cut support outer edges use that same superset.
- Completely flat interior cells retain FlatSurfaceAlphas, so dense sampling is
  not restored across broad interior terrain.
- The v1.2.55 slant cliff-base blend and v1.2.56 deterministic support cap are
  retained.
- Vertex welding is intentionally not used: welding coincident endpoints does
  not repair an intermediate vertex terminating on a neighboring triangle.
- Modular/HISM terrain remains unchanged.

Verify long straight runs, rounded corners, step junctions, all four stacked
horizontal cuts, and the baked static mesh.
