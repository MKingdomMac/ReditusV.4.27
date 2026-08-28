TileMapEditor UE4.27 v1.2.41 - Conservative Static-Mesh Bake Optimization
============================================================================

Purpose
-------
This is one focused bake-only revision built directly on the user-accepted
v1.2.40 source. It reduces dense, redundant coplanar triangulation when using
Bake Optimized Static Mesh. It does not reduce the live continuous-terrain
sampling quality and does not change occupied blocks or terrain generation.

Diagnosis
---------
The previous bake copied every triangle from every continuous procedural
chunk. That included the dense sampling grids required while authoring rounded
lips, path masks, horizontal cuts, and ramp transitions, even where many of
those samples ended up on one perfectly flat plane.

The bake then recomputed normals and tangents while every procedural face used
a zero smoothing mask. That could split the built static mesh into still more
render vertices. The reported example contained 35,644 triangles and 47,126
vertices.

Files changed
-------------
- Source/TileMapEditor/Private/TileMapEdModeToolkit.cpp
  Adds a bake-only conservative coplanar-region simplifier and tells the
  static-mesh build to retain the normal/tangent frames already supplied by
  the continuous terrain and fallback meshes.
- TileMapEditor.uplugin
  Updates version metadata to 1.2.41.

What the optimizer may remove
-----------------------------
- interior edges between connected triangles on exactly the same plane;
- redundant collinear boundary samples that no neighboring surface uses;
- duplicated source position records when rebuilding the baked raw mesh.

What is protected
-----------------
A region is left unchanged unless all of these checks pass:
- one grid cell, material slot, smoothing mask, plane, and winding;
- one constant vertex color across each candidate region;
- one consistent authored normal/tangent frame;
- one affine UV0 mapping with no UV seam or scale change;
- one simple closed boundary with no hole or non-manifold edge.

Boundary vertices referenced by any triangle outside the region are locked.
This retains the exact connection to wavy lips, rounded corners, cliff walls,
ramps, cuts, stairs, supports, undersides, and neighboring cells. Variable
path/cliff masks are protected by the vertex-color rule. The optimizer does
not use a generic percentage reduction and does not move any retained vertex.

Unchanged systems
-----------------
- live continuous-terrain geometry and its viewport performance;
- 100-unit occupied-block data, editing, Undo/Redo, and dirty chunks;
- ordinary/stacked blocks, caves, unsupported platforms, and overpasses;
- fixed-45 horizontal cuts and all vertical-ramp angles/rotations;
- fixed 33.69-degree stairs;
- cliff-top UVs, cliff-base height, path masks, and material parameters;
- collision ownership and complex-as-simple collision setting;
- modular/HISM fallback and generated modular mesh source assets;
- copied-terrain snapping/merging.

Build and replacement
---------------------
1. Close Unreal Editor and Visual Studio.
2. Back up the project's current Plugins/TileMapEditor folder.
3. Replace that complete folder with this TileMapEditor folder.
4. Delete only Plugins/TileMapEditor/Binaries and
   Plugins/TileMapEditor/Intermediate if they exist.
5. Regenerate Visual Studio project files.
6. Build the Development Editor / Win64 target and reopen Unreal Editor.

Focused UE4.27 verification
---------------------------
1. Keep the accepted v1.2.40 terrain actor and material unchanged.
2. Make a small reference terrain containing:
   - a large ordinary flat top and straight exposed wall;
   - a convex and concave corner;
   - stacked blocks and an unsupported overpass;
   - a painted path with a soft edge;
   - fixed-45 horizontal cuts in all four rotations;
   - 45-degree and 26.565-degree vertical ramps in all four directions;
   - the fixed 33.69-degree stairs.
3. Save the map, then bake it once with v1.2.40 and record LOD 0 triangle and
   vertex counts in Static Mesh Editor. Keep that asset for comparison.
4. Install v1.2.41, reopen the same unchanged map, and bake a new asset.
5. In Output Log, search for "TileMap bake optimizer". It reports the raw
   continuous triangle count before and after the safe reduction.
6. Compare LOD 0 triangle and vertex counts. They should be lower where the
   terrain contains dense coplanar regions. The exact reduction depends on
   the terrain layout and masks; no fixed percentage is claimed.
7. Toggle wireframe and inspect every top-to-lip, lip-to-wall, chunk, ramp,
   cut, stair, support, and underside junction. There must be no new crack,
   flipped face, missing face, or changed silhouette.
8. Confirm the ground, cliff body, cliff top, cliff base, and path textures
   match the live terrain, including their normals and vertex-color blends.
9. Test player collision on flats, lips, corners, ramps, cuts, stairs, and an
   unsupported platform.
10. Only accept v1.2.41 as the next baseline after these UE4.27 checks pass.

Verification status
-------------------
The source has been checked against v1.2.40 and the runtime terrain generator
is byte-for-byte unchanged. Delimiter/static invariants and the preservation
guards were checked locally. This environment cannot compile or visually run
the user's UE4.27 project, so this revision is not claimed as user-verified.

