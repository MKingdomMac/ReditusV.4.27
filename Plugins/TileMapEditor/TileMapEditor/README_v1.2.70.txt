TileMapEditor v1.2.70 - Foliage-style terrain surface detail scatter

Baseline
--------
This revision is built directly from v1.2.69. The accepted two-horizontal-slant
fillet, vertical-ramp rules, continuous terrain, hybrid modular blocks, paths,
materials, collision, and optimized terrain mesh baking are unchanged.

Problem
-------
The older Terrain Detail Pass classified each eligible cell as Ground, Cliff
Top, or Cliff Base and used a manual cliff inset. That made the controls hard
to predict and could let a large or off-center mesh extend beyond the actual
terrain surface even when its pivot was inside an occupied cell.

v1.2.70 behavior
----------------
The pass now behaves like a simple landscape-foliage scatter:

* every palette mesh is scattered only across exposed flat top surfaces;
* a random point may use the full area of its occupied surface block;
* eight deterministic attempts relocate a candidate that lands too near an
  edge, hole, painted-path exclusion, or another accepted detail;
* the mesh's scaled bounds plus the terrain's top chamfer width create
  automatic edge clearance, so its entire horizontal footprint stays on the
  flat connected top surface;
* adjacent top blocks at the same height count as one continuous surface;
* the mesh's lowest scaled bound is automatically aligned to the top surface,
  so an asset no longer needs a perfectly authored base pivot;
* Sink Into Surface applies only after automatic base alignment;
* each bake replaces the previous detail actor owned by that terrain, removing
  stale instances left at positions from an older terrain layout;
* details remain separate HISM components and never add triangles to the baked
  terrain static mesh.

Simplified settings
-------------------
The Ground / Cliff Top / Cliff Base selector and manual Cliff Detail Edge
Inset are no longer shown or used. The normal workflow is:

1. Enable Generate Terrain Details During Bake.
2. Add meshes under Surface Detail Meshes.
3. Set Surface Coverage (%), Minimum Spacing, scale variation, sink, and random
   rotation as needed.
4. Bake Optimized Static Mesh.

Seed, Maximum Detail Instances, cull distances, selection weight, and collision
remain available as advanced controls.

UE4.27 test
-----------
1. Rebuild the UE4.27 editor target and confirm VersionName 1.2.70.
2. Add one surface detail mesh and set Surface Coverage to a visible value.
3. Bake a platform containing outer edges, inner holes, paths, and stacked flat
   levels.
4. Confirm every mesh base sits on the exposed top and no mesh bounds extend
   across a cliff edge or hole.
5. Confirm no detail appears on ramps, stairs, horizontal cuts, bridges, or
   painted-path exclusion cells.
6. Confirm SM_TileMapTerrain triangle count and all v1.2.69 terrain geometry
   remain unchanged.
