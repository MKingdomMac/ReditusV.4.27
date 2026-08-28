TileMapEditor UE4.27 - v1.2.35 Fixed Gradual Stairs
===================================================

Baseline
--------
This source-only revision is based directly on v1.2.34 Blended Path Paint.
The user-verified v1.2.31 terrain geometry remains the accepted geometry
baseline. Existing ramp and horizontal diagonal generation were not rewritten.

Focused change
--------------
- Adds one stair standard: 100-unit rise over 150 units of stepped run.
- The actual design angle is atan(100 / 150) = 33.690067 degrees.
- A stair run converts exactly two existing 100-unit cells.
- Twelve physical steps are generated, with 25-unit low and high landings.
- All four rise directions are supported.
- Stair angle is fixed; the Ramp angle control is disabled in Stair mode.
- Continuous terrain generates physical treads and risers, exact collision,
  only exposed side/underside faces, and one closure wall beside a higher
  ordinary neighbor. Compatible parallel stair runs share no internal face.
- Stair treads receive the existing painted-path vertex-color mask.
- Modular/HISM fallback stair meshes are generated for non-continuous mode.
- Baking uses the existing procedural/HISM bake path, so stairs are included.

Files intentionally changed
---------------------------
- TileMapEditor.uplugin
- Source/TileMapEditor/Public/TileMapEdMode.h
- Source/TileMapEditor/Private/TileMapEdMode.cpp
- Source/TileMapEditor/Private/TileMapEdModeToolkit.cpp
- Source/TileMapRuntime/Public/TileMapTerrainActor.h
- Source/TileMapRuntime/Private/TileMapTerrainActor.cpp

Replacement and build instructions
----------------------------------
1. Close Unreal Editor and make a backup or source-control checkpoint.
2. Remove the old project Plugins/TileMapEditor folder.
3. Copy this TileMapEditor folder into the project's Plugins folder.
4. Delete only the project's Binaries and Intermediate folders if Unreal asks
   for a rebuild. Do not delete Content/TileMapGenerated; existing generated
   slant assets may be referenced by saved maps.
5. Right-click the .uproject and select Generate Visual Studio project files.
6. Build the project's Development Editor / Win64 target in Visual Studio.
7. Open the project in Unreal Engine 4.27 and accept the plugin rebuild prompt
   if one appears.

Required UE4.27 validation
--------------------------
Do not overwrite a production map for the first test.

1. Create a minimal run containing two adjacent occupied cells at the same Z.
2. Put a lower flat cell behind the first stair cell at Z-1 and an upper flat
   cell beyond the second stair cell at the stair cells' Z.
3. Select Stairs (fixed 33.69 degrees), choose the rise direction from the
   lower cell toward the upper cell, enable Apply Slant, and click the first
   of the two cells.
4. Repeat for +X, +Y, -X, and -Y.
5. Add an ordinary same-level block along each long side. Confirm that the
   neighbor stays flat and that there is no transparent strip between it and
   the stair profile.
6. Build two parallel stair runs. Confirm there is no internal wall or seam.
7. Test a raised unsupported run. Inspect the underside and both exposed sides.
8. Toggle Use Continuous Terrain Prototype off. Confirm the generated modular
   fallback stairs remain closed and collision is present.
9. Toggle it on, paint a path over both stair cells, and confirm the mask reaches
   the treads. The final appearance depends on the terrain material graph.
10. Bake Optimized Static Mesh. Confirm visible stairs, collision, materials,
    and path vertex colors are preserved in the baked asset.
11. Test Undo/Redo for one complete stair placement stroke.

Verification status
-------------------
The source and topology can be checked statically here, but this environment
does not contain an Unreal Engine 4.27 toolchain or viewport. The revision must
be compiled and visually verified in the user's UE4.27 project before it is
accepted as a working baseline.
