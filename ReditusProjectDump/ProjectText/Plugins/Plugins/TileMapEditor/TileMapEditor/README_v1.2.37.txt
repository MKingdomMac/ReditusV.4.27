TileMapEditor UE4.27 v1.2.37 - Explicit Surface Ownership Mask
================================================================

Purpose
-------
This is one focused material-data revision built on the cumulative v1.2.36
source. It does not change terrain topology, vertex positions, triangle
winding, collision, stairs, slants, merge behavior, editor tools, or baking.

Continuous-terrain vertex-color contract
----------------------------------------
R: Inverse cliff-foot mask. The material reads OneMinus(VertexColor.R).
G: Inverse painted-path mask. The material reads OneMinus(VertexColor.G).
B: Explicit surface ownership. 1 = ground, 0 = cliff/support/underside.
A: Ownership validity marker. 0 = authored continuous terrain.

Ordinary modular meshes normally retain white vertex color. Their A channel is
therefore 1, allowing the material to keep its normal-based fallback.

Material wiring
---------------
Keep the existing normal-derived ground mask as NormalGroundMask.

Create:

    AuthoredMaskWeight = OneMinus(VertexColor.A)
    GroundMask = Lerp(NormalGroundMask, VertexColor.B, AuthoredMaskWeight)

Connect GroundMask to the Alpha inputs of both existing Cliff/Ground albedo and
normal Lerps.

For the later path layer use:

    PathMask = OneMinus(VertexColor.G) * GroundMask

This prevents path material from appearing on cliffs, stair risers, supports,
or undersides.

Expected classification
-----------------------
Ground: flat interiors, vertical ramps, horizontal cuts, stair treads, and
visible horizontal support-top complements.

Cliff: exposed cliff walls, the physical lip below the flat-top boundary,
stair risers, ramp banks/closures, support walls, and undersides.

Build and test
--------------
1. Close Unreal Editor and Visual Studio.
2. Replace only Plugins/TileMapEditor with this TileMapEditor folder.
3. Delete the project Binaries and Intermediate folders if Unreal does not
   rebuild the plugin automatically.
4. Regenerate project files and build the Editor target for Win64 Development.
5. Open the material and apply the GroundMask wiring above.
6. Reopen or edit the terrain so continuous chunks rebuild with the new vertex
   colors.
7. Test a flat top, outer rounded lip, straight cliff, convex and concave
   corners, stacked blocks, 45-degree ramp, 26.565-degree ramp, fixed stairs,
   horizontal cut, unsupported platform underside, and modular fallback.

Verification status
-------------------
The source was inspected and checked for channel ownership, but this revision
has not been compiled or visually verified inside the user's UE4.27 project.
