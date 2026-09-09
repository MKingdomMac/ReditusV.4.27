TileMapEditor UE4.27 v1.2.64 - Strict Hybrid Bake

Baseline
--------
This revision is based directly on v1.2.63. Live continuous terrain, modular
HISM rendering, paths, masks, slants, collision and per-block selection are
unchanged.

Fixed
-----
Blocks marked with Paint Modular Blocks are no longer silently omitted from an
optimized static-mesh bake when their editable RawMesh source data is absent or
empty.

The bake now uses two geometry sources in this order:

1. Rendered LOD 0 position, index, tangent, UV, vertex-color and material data,
   matching the geometry displayed by the live HISM.
2. Editable LOD 0 RawMesh data as a fallback.

Strict validation
-----------------
Every block excluded from continuous generation must append replacement static
mesh geometry. If any standalone block cannot supply either form of LOD 0
geometry, the complete bake is aborted instead of creating an apparently valid
asset with a missing block. The Output Log identifies its grid position, tile
type and mesh.

A successful hybrid bake logs the total standalone block count and the number
of explicit modular overrides appended.

Test
----
1. Enable Use Continuous Terrain Prototype.
2. Paint several cells with Paint Modular Blocks.
3. Confirm the authored modular meshes appear in the live terrain.
4. Select the terrain actor and click Bake Optimized Static Mesh.
5. Hide the source terrain actor and verify every painted modular block exists
   in the baked static mesh.
6. Inspect collision and the Output Log.
