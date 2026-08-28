TileMapEditor UE4.27 v1.2.56

Deterministic continuous horizontal-cut support triangulation.

- Retains the working v1.2.55 slant cliff-base blend.
- Retains the corrected shared rounded boundary from v1.2.53.
- Replaces the support complement's early-exit ear clipping with a guaranteed
  corner fan from the known square corner opposite the occupied triangle.
- Rotation mapping: 0 uses C01, 1 uses C00, 2 uses C10, and 3 uses C11.
- Emits the entire complement in one deterministic pass; collinear rounded
  samples may create zero-area spans, which are safely skipped without leaving
  any nonzero polygon region unbuilt.
- Continuous terrain only. Modular/HISM terrain is unchanged.

Verify all four stacked horizontal-cut rotations from above and below, then
bake and inspect the same junctions.
