TileMapEditor UE4.27 v1.2.54

Focused continuous stacked-horizontal-cut completion based on the accepted
v1.2.51 code paths and the user-tested v1.2.53 boundary correction.

- Combines the corrected shared rounded outer boundary with concave-safe ear
  clipping for the support complement. Both are required: the old raw boundary
  caused the large wedges, while its center fan causes the remaining small
  triangular tears after the boundary is rounded.
- Applies the support repair to all four horizontal-cut rotations.
- Adds the missing cliff-foot/base vertex-color band to a horizontal diagonal
  wall when that cut is supported by ordinary continuous terrain below it.
- Uses the existing Cliff Foot Blend Height and existing wavy height function.
- Does not restore a buried full-square face or create overlapping geometry.
- Does not change modular/HISM terrain, materials, paths, ramps, stairs,
  collision, chamfer density, or bake optimization.

UE4.27 verification:

1. Enable Continuous Terrain.
2. Stack each horizontal-cut rotation above ordinary occupied blocks.
3. Check every endpoint and lower-lip junction from above and below.
4. Confirm the cliff-base blend appears along the bottom of the diagonal wall.
5. Bake and repeat the visual and collision checks.
