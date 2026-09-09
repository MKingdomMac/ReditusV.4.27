TileMapEditor v1.2.67 - Lower support cliff edge beneath horizontal slants

Baseline
--------
This revision is a focused update from v1.2.66. The accepted slant hover
preview, stacked-ramp continuity, hybrid continuous/modular baking, collision,
path masks, cliff-base blending, and existing continuous geometry remain in
place.

Fix
---
When a continuous 45-degree horizontal cut is stacked directly above an
ordinary block, the upper cut occupies only half of the cell. Earlier builds
correctly restored the visible triangular top complement on the lower block,
but treated the upper cell as full coverage when calculating cliff lips. That
left the lower ledge flat and ground-owned, so its cliff-edge strip was absent.

v1.2.67 now:

* evaluates upper coverage separately for each of the lower block's four edges;
* keeps the two edges beneath the upper cut buried and flat;
* restores chamfer height, edge waviness, cliff-top UVs, and cliff-edge vertex
  ownership on the two genuinely exposed lower edges;
* tessellates the visible lower half-cell on the same symmetric sample grid as
  its adjoining wall;
* locks extra diagonal samples to the upper cut's existing piecewise boundary,
  preventing the support fix from reopening a T-junction;
* applies the same result in all four horizontal-cut rotations and in baked
  static meshes because the correction is in the shared continuous generator.

UE4.27 test
-----------
1. Replace the previous TileMapEditor plugin source with this folder.
2. Delete the project's plugin Intermediate/Binaries only if Unreal requests a
   rebuild, then rebuild the UE4.27 editor target.
3. Stack a 45-degree horizontal cut over a normal continuous block.
4. Rotate the cut through X+, X-, Y+, and Y-.
5. Confirm that the lower triangular ledge has a cliff strip only on its two
   exposed outer sides, while the diagonal contact under the upper cut stays
   closed and unchamfered.
6. Bake an optimized static mesh and confirm the same edge ownership remains.
