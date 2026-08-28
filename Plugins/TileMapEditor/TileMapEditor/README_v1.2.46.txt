TileMapEditor UE4.27 v1.2.46 - Optional Terrain Detail Pass
================================================================

Baseline and scope
------------------
- Starts from the exact packaged v1.2.45 source, so modular/HISM path painting
  remains included.
- Adds only an optional structural terrain-detail pass to Bake Optimized Static
  Mesh. It is disabled by default.
- Does not change continuous-terrain generation, the accepted bake optimizer,
  terrain collision, slants, stairs, cuts, paths, chunk ownership, or foliage.

What the pass creates
---------------------
When enabled, the bake can place user-supplied low-poly meshes representing:
- low ground bumps or mounds;
- embedded rocks and boulders;
- cliff-top or cliff-corner protrusions;
- rocks at the base of a higher cliff;
- short layered or stepped terrain pieces.

Vegetation is deliberately excluded. Continue using Unreal's Foliage tool for
grass, ferns, shrubs, trees, stumps, and fallen branches.

Performance and ownership
-------------------------
- Detail meshes are grouped by palette entry into HISM components on a separate
  actor named TileMap_Terrain_Details_Baked.
- The detail actor receives the terrain actor's complete location, rotation,
  and scale after its instance root is registered.
- They never enter the optimized terrain FRawMesh. Enabling the pass therefore
  must not change the triangle or vertex count of SM_TileMapTerrain.
- Detail collision is disabled per entry by default and can be enabled only for
  entries that genuinely require it.
- HISM start/end cull distances, density, maximum instances, minimum spacing,
  deterministic seed, and cliff-edge inset are editable on the terrain actor.
- Editing these bake-only settings does not rebuild the live terrain chunks.

Placement rules
---------------
- Only exposed, flat occupied surfaces are candidates in this first pass.
- Ramps, horizontal diagonal cuts, stairs, and pass/bridge cells are excluded.
- Painted paths and their immediate neighboring cells are kept clear.
- Ground entries use cell-interior jitter. Cliff Top entries sit inside an
  exposed boundary. Cliff Base entries sit beside a neighboring wall one level
  higher.
- The same seed and terrain data produce the same placements.
- Minimum spacing applies between nearby details on similar elevations while
  independently stacked platforms remain eligible.

Mesh authoring assumptions
--------------------------
- Author around the existing 100-unit terrain cell scale.
- Put the mesh origin at the intended ground/contact point.
- Mesh Scale and Uniform Scale Range provide per-entry adjustment.
- Sink Depth embeds the detail below the supporting top surface.
- If Random Yaw is disabled, mesh +X faces outward at a cliff top and toward
  the higher wall at a cliff base. Ground entries use zero yaw.
- Material Override is optional; otherwise the mesh's own material is used.

Files and functions changed
---------------------------
- Source/TileMapRuntime/Public/TileMapTerrainActor.h
  Adds the placement enum, palette-entry struct, disabled-by-default actor
  settings, and flat-surface eligibility query.
- Source/TileMapRuntime/Private/TileMapTerrainActor.cpp
  Sets defaults, excludes special terrain shapes, and prevents detail-only
  property edits from rebuilding the live terrain.
- Source/TileMapEditor/Private/TileMapEdModeToolkit.cpp
  Deterministically classifies eligible cells, spawns the separate HISM detail
  actor after a successful terrain bake, and reports the instance count.

Verification status
-------------------
The source was inspected and statically checked in this environment. It cannot
be compiled or visually verified here in the user's UE4.27 project. v1.2.45 is
the direct source parent; keep the user-tested v1.2.43 package as the confirmed
rollback until the focused tests in INSTALL_AND_TEST.txt pass.
