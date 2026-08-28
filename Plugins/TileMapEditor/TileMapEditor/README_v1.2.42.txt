TileMapEditor UE4.27 v1.2.42 - Optimized Bake Hash Compile Fix
================================================================

Baseline
--------
- v1.2.40 remains the accepted functional baseline.
- v1.2.41 introduced the focused, bake-only topology optimizer but failed to
  compile because its custom map-key hash called overloaded GetTypeHash
  functions from inside another GetTypeHash overload.

Focused change in v1.2.42
-------------------------
- Replaced nested GetTypeHash calls in FBakePositionKey with a self-contained
  deterministic integer hash.
- Reused that hash for FBakeEdgeKey, so neither custom key depends on overload
  lookup for its member values.
- No runtime continuous-terrain generation, editor tools, materials, path data,
  slants, stairs, collision generation, or modular fallback logic was changed.
- The v1.2.41 bake-only coplanar reduction remains otherwise unchanged.

Important verification status
-----------------------------
- The source and archive were checked statically, including removal of the
  failing nested hash calls.
- This environment does not contain the user's UE4.27 project/toolchain, so the
  user must compile and test the revision inside UE4.27 before accepting it.
- Keep v1.2.40 available until v1.2.42 has been verified in the editor.

Test order
----------
1. Compile the editor target from Visual Studio or UnrealBuildTool.
2. Open the same terrain used for the v1.2.40 baseline.
3. Confirm continuous terrain, paths, stairs, vertical slants, horizontal cuts,
   stacked blocks, and collision still display correctly before baking.
4. Bake the optimized static mesh.
5. Compare its silhouette, materials, vertex colors, normals, collision, and
   triangle/vertex counts against the v1.2.40 bake.
