TileMapEditor UE4.27 v1.2.43 - Cross-Cell Bake Optimization
================================================================

Baseline and scope
------------------
- Starts from user-tested v1.2.42, where terrain appearance and collision were
  reported working and the bake showed a substantial triangle/vertex reduction.
- This revision changes only the bake-time coplanar simplifier.
- The reported minor X+ vertical-ramp foot issue is intentionally deferred and
  is not modified in this revision.

Focused change
--------------
- v1.2.42 allowed a simplification component to remain inside only one
  100-unit grid cell. That retained the dense cell grid on broad flat tops and
  long coplanar wall sections.
- v1.2.43 removes that one-cell component restriction. Connected triangles may
  now be retriangulated across cell and continuous-chunk boundaries when all
  existing safety checks agree.

Still protected
---------------
- exact plane and outward winding;
- material slot and smoothing mask;
- constant vertex-color classification/masks;
- consistent authored normals and tangent frame;
- affine UV0 mapping without a seam or scale change;
- simple closed boundary with no holes or non-manifold edge;
- every boundary vertex referenced by a ramp, lip, wall, corner, stair,
  support, underside, mask transition, or any triangle outside the component.

Unchanged
---------
- runtime continuous-terrain generation and dirty-chunk rebuilding;
- occupied-block data, editor tools, paths, stairs, ramps, horizontal cuts,
  stacked blocks, caves, overpasses, collision policy, materials, and HISM/
  modular fallback;
- the bake source normals/tangents and v1.2.42 hash compile correction.

Required UE4.27 verification
----------------------------
1. Keep the verified v1.2.42 baked mesh for comparison.
2. Rebuild the plugin, open the same unchanged terrain, and bake a new mesh.
3. Record LOD0 triangles and vertices and compare them with v1.2.42.
4. Inspect wireframe and rendered geometry at cell/chunk borders, large flat
   tops, straight walls, convex/concave corners, path-mask edges, cliff masks,
   all ramp/cut rotations, stairs, stacked supports, and undersides.
5. Confirm collision across every newly simplified broad surface.
6. Accept v1.2.43 only after the in-editor comparison passes.

Verification status
-------------------
- The source and archive were checked statically and the runtime terrain source
  was confirmed unchanged from v1.2.42.
- This environment cannot compile or visually test the user's UE4.27 project;
  therefore this revision is not claimed as user-verified.
