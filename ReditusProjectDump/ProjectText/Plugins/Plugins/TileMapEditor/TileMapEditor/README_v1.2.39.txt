TileMapEditor UE4.27 v1.2.39 - Dedicated Cliff-Top UVs
=======================================================

Purpose
-------
This is one focused UV-only revision built directly on v1.2.38. It gives the
existing physical upper cliff-edge mask a dedicated UV0 layout so a designed
cliff-top strip texture can follow the terrain boundary. It does not move a
vertex, add or remove a triangle, alter topology, change collision, change
occupied-block data, or change any editor tool.

Only Source/TileMapRuntime/Private/TileMapTerrainActor.cpp changes source code.
TileMapEditor.uplugin changes version metadata only.

Cliff-top UV contract
---------------------
U: Actor-local physical distance along the exposed boundary.
   One U repeat equals one Grid Size (100 Unreal units by default).

V = 0.0: flat-ground side of the lip.
V = 0.5: outer crest where the top turns down.
V = 1.0: lower edge of the narrow upper-wall strip.

The same contract is assigned to:

- ordinary exposed cliff lips;
- straight, convex, and concave rounded lip pieces;
- the fixed-45-degree horizontal diagonal-cut lip; and
- exposed long sides and exposed high/low ends of 45-degree and existing
  26.565-degree vertical ramps.

Shared ramp seams, ramp banks beside occupied neighbors, neighbor closure
walls, internal/buried faces, cliff bodies, supports, undersides, flat ground,
and the lower cliff-base band keep their v1.2.38 UV behavior.

The procedural tangent supplied for the dedicated vertices follows U. This is
required for a tangent-space cliff-top normal map to follow the boundary.

Texture layout
--------------
Recommended first texture: 512 x 128.

- left/right edges must tile;
- top row represents the ground-side transition;
- vertical center represents the outer crest;
- bottom row represents the upper cliff-wall transition;
- use Address X = Wrap and Address Y = Clamp;
- albedo uses sRGB; tangent-space normal map does not use sRGB.

Increasing the image resolution changes sharpness only. It does not change the
physical texture scale. U already repeats once per 100 units and V spans the
complete physical cliff-top strip once.

Required material change from v1.2.38
-------------------------------------
Keep the v1.2.38 CliffEdgeMask decoder and layer order, but stop using a
WorldAlignedTexture for the cliff-top layer.

1. Add a TextureCoordinate node using Coordinate Index 0.
2. Connect it directly to the UVs input of the cliff-top albedo Texture Sample
   Parameter 2D.
3. Connect the same TextureCoordinate output to the UVs input of the cliff-top
   normal Texture Sample Parameter 2D.
4. Use the existing CliffEdgeMask as the Alpha of the cliff-top albedo and
   normal Lerps.
5. Keep the cliff-base blend after the cliff-top layer and the painted-path
   layer last, as established in v1.2.38.

For the first viewport test, use a 512 x 128 debug strip with obvious colors
and arrows. Do not add material tiling controls until the orientation test
passes. A later scalar can multiply only TexCoord0.U if a different along-edge
repeat is desired; TexCoord0.V should normally remain 0-1.

Cliff-base scope
----------------
The v1.2.38 cliff-base mask remains on the lower cliff wall, not on the ground.
This revision intentionally does not give it dedicated UVs. Cliff-base UVs are
the next separate change after the cliff-top layout is verified in UE4.27.

Replacement and build instructions
----------------------------------
1. Close Unreal Editor and Visual Studio.
2. Back up the project's current Plugins/TileMapEditor folder.
3. Replace that complete folder with the TileMapEditor folder from this ZIP.
4. Delete only Plugins/TileMapEditor/Binaries and
   Plugins/TileMapEditor/Intermediate if Unreal does not rebuild automatically.
5. Regenerate Visual Studio project files.
6. Build Development Editor / Win64.
7. Open the terrain material and apply the UV0 material wiring above.
8. Reopen the test map or make one terrain edit so continuous chunks rebuild.

Focused UE4.27 verification
---------------------------
1. Test one isolated ordinary block and confirm the debug texture runs from
   ground at V=0, through the crest at V=0.5, to upper wall at V=1.
2. Place several blocks in a straight line. U must continue/repeat without a
   seam at internal 100-unit cell boundaries.
3. Test one convex corner and a three-block concave corner.
4. Test stacked blocks and an unsupported platform. Cliff bodies, supports,
   undersides, and buried faces must remain unchanged.
5. Test the fixed-45 horizontal cut in all four rotations.
6. Test a 45-degree vertical ramp and the existing two-cell 26.565-degree ramp
   in all four directions, with exposed sides and occupied side neighbors.
7. Confirm no cliff-top texture appears on ramp seams, neighbor closures,
   ordinary flat ground, the main cliff body, or the lower cliff-base band.
8. Inspect collision and Bake Optimized Static Mesh only after the viewport UV
   orientation passes.

Verification status
-------------------
The source and diff were checked for UV ownership and the absence of topology
changes. This revision has not been compiled or visually verified inside the
user's UE4.27 project. Do not accept it as a new baseline until the focused
test above passes in the editor.
