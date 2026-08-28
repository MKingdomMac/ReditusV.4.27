TileMapEditor UE4.27 v1.2.40 - Taller Lower Cliff-Base Mask
===========================================================

Purpose
-------
This is one focused default-value revision built directly on the user-accepted
v1.2.39 Dedicated Cliff-Top UV package. It doubles the default height of the
separate lower cliff-base vertex-color band from 10 to 20 Unreal units.

Only Source/TileMapRuntime/Private/TileMapTerrainActor.cpp changes runtime
source code. TileMapEditor.uplugin changes version metadata. INSTALL_AND_TEST
and this README document the replacement and verification steps.

Exact source change
-------------------
ATileMapTerrainActor::ATileMapTerrainActor():

    ContinuousCliffFootBlendHeight = 10.0f;

becomes:

    ContinuousCliffFootBlendHeight = 20.0f;

What this changes
-----------------
- The physical lower-wall split used by MakeCliffFootVertexColor is 20 units
  high by default instead of 10 units.
- The inverse VertexColor.R cliff-base mask therefore covers a visibly taller
  band on genuinely supported exposed cliff walls.
- The existing wavy height variation is retained and scales from the same
  Cliff Foot Blend Height value.

What this does not change
-------------------------
- the accepted dedicated cliff-top alpha mask or UV0 layout;
- cliff-top geometry, chamfer size, edge waviness, or corner rounding;
- cliff-body vertex colors;
- flat-ground, path, ramp, stair, support, or underside masks;
- occupied-block data, boundary ownership, triangle winding, collision, mesh
  baking, editor tools, copied-terrain merging, or modular/HISM fallback.

Existing placed terrain actors
------------------------------
UE4 serializes editable actor properties in the level. Replacing the plugin
does not overwrite a saved 10-unit value on an actor that is already placed.

After installing v1.2.40:

1. Select the existing TileMap_Terrain actor.
2. Open Continuous Terrain in Details.
3. Set Cliff Foot Blend Height to 20.0, or click its yellow reset arrow to use
   the new 20-unit default.
4. Reopen the map or make one terrain edit to rebuild continuous chunks.

New terrain actors use 20.0 automatically.

Replacement and build instructions
----------------------------------
1. Close Unreal Editor and Visual Studio.
2. Back up the project's current Plugins/TileMapEditor folder.
3. Replace that complete folder with the TileMapEditor folder from this ZIP.
4. Delete only Plugins/TileMapEditor/Binaries and
   Plugins/TileMapEditor/Intermediate if Unreal does not rebuild automatically.
5. Regenerate Visual Studio project files.
6. Build Development Editor / Win64.
7. Reopen the map, set the existing actor's Cliff Foot Blend Height to 20.0,
   and force one continuous-chunk rebuild.

Focused UE4.27 verification
---------------------------
1. Keep the current cliff-top test texture and material graph unchanged.
2. Assign a visibly different debug texture to the existing CliffBase_Albedo
   layer so the two masks cannot be confused.
3. Confirm the cliff-top test texture remains confined to the accepted wavy
   upper lip.
4. Confirm the cliff-base texture now occupies approximately the lowest 20
   units of a supported one-cell cliff wall, rather than approximately 10.
5. Test a straight wall, convex corner, concave corner, stacked block, fixed-45
   horizontal cut, 45-degree vertical ramp, and existing 26.565-degree ramp.
6. Confirm the taller lower band does not appear on flat ground, unsupported
   undersides, internal faces, or the upper cliff-top strip.
7. Confirm collision and Bake Optimized Static Mesh remain unchanged.

Verification status
-------------------
The v1.2.39 package was matched byte-for-byte against the local source tree
before this one-line runtime change. The source diff and value flow were
checked statically. This revision has not been compiled or visually verified
inside the user's UE4.27 project, so accept it as a baseline only after the
focused editor test passes.
