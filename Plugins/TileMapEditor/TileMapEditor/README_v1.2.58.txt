TileMapEditor UE4.27 v1.2.58

Deterministic stacked horizontal-cut support grid.

Evidence from v1.2.57:

- Two-sided rendering did not hide the gaps, proving triangles were absent.
- The underside view showed isolated sliver triangles outside the intended
  support area, proving that the rounded complement is not reliably star-shaped
  when it is close to neighboring slanted edges.

Fix:

- Returns to the compile-correct v1.2.55 geometry and working slant cliff-base
  blend; the ineffective v1.2.57 sampling experiment is excluded.
- Replaces polygon triangulation of the lower complementary half-cell with a
  fixed triangular grid using DetailedEdgeSubdivisions.
- The grid's diagonal row uses RoundDiagonalTangentPoint with the upper slant's
  layer, exactly matching the upper diagonal wall bottom.
- Remaining grid vertices use RoundConvexPoint from the lower supporting cell.
- Connectivity is fixed before rounding, so nearby slants may move vertices but
  cannot create missing triangles, early exits, or outside fan slivers.
- Supports all four fixed horizontal-cut rotations.
- Does not change modular/HISM terrain, materials, paths, collision, ramps,
  stairs, bake optimization, or the accepted slant cliff-base blend.

Verify stacked horizontal cuts both isolated and adjacent to other slanted
edges, from above and underneath, before baking.
