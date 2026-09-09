TileMapEditor UE4.27 v1.2.52

Focused stacked horizontal-cut support fix based on v1.2.51.

- Replaces the concave center-fan triangulation used by the lower support cap
  beneath a stacked horizontal diagonal cut.
- Uses concave-safe ear clipping while preserving the exact rounded diagonal
  boundary shared with the upper cut.
- Covers the two endpoint wedges without restoring a buried full-square top,
  avoiding overlapping faces and z-fighting.
- Applies to all four horizontal-cut rotations.
- Does not change materials, cliff masks, chamfer density, paths, collision,
  bake optimization, ramps, stairs, or unrelated continuous terrain geometry.

Verification required in UE4.27:

1. Stack a horizontal diagonal cut above an occupied normal block.
2. Inspect both diagonal endpoints from below for white triangular holes.
3. Repeat for +X, +Y, -X, and -Y rotations.
4. Confirm the lower support cap has no z-fighting from above.
5. Bake and verify that the repaired cap is retained.
