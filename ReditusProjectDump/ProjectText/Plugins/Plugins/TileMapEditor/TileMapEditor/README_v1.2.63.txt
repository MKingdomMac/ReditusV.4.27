TileMapEditor UE4.27 v1.2.63 - Per-Block Hybrid Terrain

Baseline
--------
This revision is based on v1.2.62. The accepted continuous geometry,
diagnostics, materials, vertex masks, slants, collision and topology are not
rewritten by this feature.

New editor tools
----------------
Paint Modular Blocks:
Marks occupied cells as explicit continuous-terrain exclusions. Those cells
use their existing palette static mesh through the chunked HISM renderer.

Restore Continuous Blocks:
Removes the exclusion and returns compatible cells to continuous rendering.

Both tools support click-drag painting. One mouse stroke is one Undo/Redo
transaction. Changing a cell rebuilds its neighboring chunks as well.

Hybrid boundary behavior
------------------------
An excluded cell remains occupied for continuous neighbor and occlusion tests.
It is not treated as empty space, so the continuous renderer does not generate
an unnecessary overlapping cliff wall against it. The authored modular mesh
provides that cell's visible geometry and collision.

Paths and baking
----------------
Painted paths on modular overrides use a separate lightweight path-overlay
component, allowing continuous geometry and modular path overlays to coexist
inside one chunk. Static-mesh baking includes continuous components, modular
overrides and their path overlays.

Copy and merge preserves modular overrides. Removing a block removes its
override, moving a block moves it, and old maps default to continuous behavior
because their override array is empty.

Play-in-Editor
--------------
Transient terrain chunks are rebuilt in BeginPlay from the serialized block
arrays, so both continuous and modular terrain are present in PIE and packaged
runtime worlds.

Initial test
------------
1. Leave Use Continuous Terrain Prototype enabled.
2. Select Paint Modular Blocks and click-drag across several flat cells.
3. Confirm those cells change to their authored modular/HISM mesh.
4. Select Restore Continuous Blocks and paint the same cells.
5. Test a chunk boundary, path painting, collision, Undo/Redo, Play and bake.
