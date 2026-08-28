TileMapEditor UE4.27 v1.2.38 - Dedicated Cliff-Edge Material Mask
=================================================================

Purpose
-------
This is one focused material-data revision built on v1.2.37. It adds a
separate mask for the narrow physical strip where ground meets an exposed
cliff. It does not change terrain topology, triangle winding, outer terrain
boundaries, ramps, cut angles, stairs, merge behavior, editor tools, or
static-mesh baking.

The new mask covers:

- ordinary straight outer lips;
- convex and concave rounded lips;
- the two exposed long sides of a vertical ramp; and
- exposed ramp high/low ends when those ends border a cliff; and
- the exposed upper lip of the fixed-45-degree horizontal diagonal cut.

Shared ramp seams, ramp banks beside occupied neighbors, internal closure
walls, flat ground, cliff bodies, and cliff-foot blending are not tagged as
the upper cliff edge.

The existing cliff-base mask is also corrected in this revision. It no longer
spreads across adjacent flat ground. It now fades upward on the lower portion
of the cliff wall, with a deterministic wavy boundary driven by the existing
edge irregularity, edge wavelength, cliff-foot width, and cliff-foot height
settings. The wall remains planar and its outer silhouette is unchanged.

Files and functions changed
---------------------------
Source/TileMapRuntime/Private/TileMapTerrainActor.cpp

- Added ContinuousCliffEdgeSurfaceColor and
  MakeCliffEdgeSurfaceVertexColor.
- BuildContinuousChunk now writes the edge mask onto existing ordinary lip,
  concave-corner lip, exposed vertical-ramp edge, and fixed-45 horizontal-cut
  lip vertices.
- The cliff-base mask is removed from horizontal ground/cutout patches and is
  retained on the lower cliff wall only.
- No vertices or triangles are added or removed. Only the existing coplanar
  cliff-base material row varies vertically to make its blend boundary wavy;
  outer boundaries and silhouettes do not move.

TileMapEditor.uplugin

- Version metadata only: 1.2.38.

Packed vertex-color contract
----------------------------
R: Inverse cliff-base mask on the lower cliff wall. Material mask =
   OneMinus(VertexColor.R). Flat ground remains R=1.
G: Inverse painted-path mask. Material mask = OneMinus(VertexColor.G).
B: Explicit ground ownership. 1 = ground, 0 = cliff/support.
A: Packed data marker and upper-edge mask:

   0.0 = authored continuous terrain, not upper edge
   0.0 through approximately 0.5 = blended upper cliff-edge mask
   1.0 = ordinary modular fallback; continuous ownership is not valid

Important material update from v1.2.37
--------------------------------------
The old AuthoredMaskWeight = OneMinus(VertexColor.A) is no longer sufficient,
because A now carries the edge mask in its lower half.

Create these nodes:

    AuthoredMaskWeight = Saturate(OneMinus(VertexColor.A) * 100)
    CliffEdgeMask = Saturate(VertexColor.A * 2) * AuthoredMaskWeight
    GroundMask = Lerp(NormalGroundMask, VertexColor.B, AuthoredMaskWeight)

Replace the Alpha connection on the existing ownership Lerp with the new
AuthoredMaskWeight. Keep GroundMask connected to both existing Cliff/Ground
albedo and normal Lerps.

Add two texture parameters:

    CliffEdge_Albedo
    CliffEdge_Normal

Use the same world-aligned texture setup as the other terrain layers. Insert
the new layer after the main Cliff/Ground blend and before the existing
CliffBase blend:

    EdgeAlbedo = Lerp(CurrentCliffGroundAlbedo, CliffEdgeAlbedo, CliffEdgeMask)
    FinalBaseAlbedo = Lerp(EdgeAlbedo, CliffBaseAlbedo, CliffBaseMask)

    EdgeNormal = Lerp(CurrentCliffGroundNormal, CliffEdgeNormal, CliffEdgeMask)
    FinalBaseNormal = Lerp(EdgeNormal, CliffBaseNormal, CliffBaseMask)

Keep the painted-path layer after these base layers. The main cliff texture
remains the cliff-body texture. CliffBase remains the bottom contact blend.
CliffEdge is only the narrow upper strip circled in the reference images.

Replacement and build instructions
----------------------------------
1. Close Unreal Editor and Visual Studio.
2. Back up the project's current Plugins/TileMapEditor folder.
3. Replace that folder with the TileMapEditor folder from this ZIP.
4. If Unreal does not rebuild automatically, delete only the project's
   Binaries and Intermediate folders.
5. Regenerate Visual Studio project files.
6. Build the project's Editor target as Development Editor / Win64.
7. Open the terrain material and apply the wiring above.
8. Reopen the map or make one terrain edit to rebuild continuous chunks.

Focused UE4.27 test matrix
--------------------------
1. Ordinary terrain: straight outer edge, convex corner, three-block concave
   corner, stacked block, and unsupported platform.
2. Vertical ramps: 45 degrees and the existing two-cell 26.565-degree ramp in
   all four directions.
3. For each ramp, test both exposed long sides, one occupied side neighbor,
   exposed low/high ends, and exact lower/upper ground seams.
4. Test the fixed-45-degree horizontal cut in all four rotations and confirm
   its exposed upper diagonal lip receives the new edge texture.
5. Confirm the new texture appears only on the physical upper edge strip.
6. Confirm the cliff-base texture has a gently wavy upper boundary on the
   lower cliff wall and does not spread onto adjacent flat ground.
7. Confirm flat ground, neighboring cubes, cliff bodies, ramp
   seams, support walls, and undersides keep their existing classifications.
8. Toggle continuous terrain off to confirm modular fallback still uses the
   normal-derived mask.
9. Bake an optimized static mesh and confirm the same vertex-color material
   classification is retained.

Verification status
-------------------
The source and diff were checked for material-channel ownership and for the
absence of topology changes. This revision has not been compiled or visually
verified inside the user's UE4.27 project; in-editor results require the test
matrix above.
