TileMapEditor UE4.27 v1.2.44 - Affine-Attribute Bake Optimization
==================================================================

Accepted baseline and scope
---------------------------
- Starts from the user-tested v1.2.43 source. The reported bake was about
  30,347 triangles and 24,670 vertices on the user's comparison terrain.
- This revision changes only the continuous-terrain static-mesh bake
  simplifier in TileMapEdModeToolkit.cpp.
- Live continuous-terrain generation, occupied-block data, editor tools,
  collision generation, materials, paths, stairs, ramps, cuts, merging,
  modular/HISM fallback, and dirty-chunk rebuilding are unchanged.
- The minor reported X+ vertical-ramp foot artifact remains deferred.

Focused final reduction
-----------------------
- v1.2.43 simplified only planar regions whose vertex color was constant in
  every source triangle. That retained many center-fan triangles in otherwise
  planar cliff-top and cliff-base mask bands.
- v1.2.44 derives an affine UV0 field and an affine raw vertex-color field for
  each eligible triangle. Connected coplanar triangles may be retriangulated
  when those fields agree, including a one-value tolerance for unavoidable
  8-bit FColor quantization.
- Every vertex in the complete candidate region is checked against the
  reference UV and color fields before replacement. A region is preserved
  unchanged if any UV, mask, tangent, plane, boundary, or winding check fails.
- Component membership is compared to the original seed triangle instead of
  chaining triangle-to-triangle. This prevents gradual normal or attribute
  drift from joining incompatible surfaces.
- A collinear boundary point is removed only if both UV0 and vertex color are
  reproduced by linear interpolation between its two retained neighbors.

What intentionally remains
--------------------------
- Exposed rounded/wavy lip geometry changes the physical silhouette and is
  not coplanar, so it is not flattened or removed by this bake pass.
- Convex/concave corners, ramp transitions, stair steps, supports, undersides,
  overpasses, holes, UV seams, mask breaks, and vertices referenced by geometry
  outside a candidate region remain locked.
- Some perimeter wireframe bands are therefore expected. They are exposed
  terrain geometry, not buried cube faces.

Required UE4.27 verification
----------------------------
1. Keep the accepted v1.2.43 plugin and baked mesh as the rollback.
2. Rebuild v1.2.44 and bake the same unchanged comparison terrain.
3. Record LOD0 triangles and vertices and compare them with the v1.2.43 result.
4. Inspect rendered and wireframe output on broad flat tops, straight walls,
   cliff-top and cliff-base masks, path blends, chunk borders, all ramp/cut
   rotations, stairs, convex/concave corners, stacked supports, and undersides.
5. Confirm collision across every simplified surface.
6. Accept v1.2.44 only after these checks pass inside the user's UE4.27 project.

Verification status
-------------------
- Source structure, package contents, delimiter balance, JSON, and the focused
  file diff were checked in this environment.
- The runtime terrain source is byte-identical to v1.2.43.
- This environment cannot compile or visually test the user's UE4.27 project,
  so no in-editor success claim is made.
