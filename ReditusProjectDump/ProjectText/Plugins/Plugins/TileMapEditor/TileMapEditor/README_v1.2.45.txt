TileMapEditor UE4.27 v1.2.45 - Modular/HISM Path Painting
================================================================

Baseline and scope
------------------
- Starts from the user-accepted v1.2.43 source.
- Does not include the no-benefit v1.2.44 bake experiment.
- Changes only path rendering/editing while the full continuous-terrain mode
  is disabled, plus the matching modular bake input.
- The accepted continuous geometry, masks, collision, slants, stairs, corners,
  chunk ownership, and v1.2.43 bake simplifier are not rewritten.

Focused implementation
----------------------
- Paint Path and Erase Path no longer reject a compatible upward surface just
  because Use Continuous Terrain Prototype is disabled.
- Surface compatibility is now independent of which renderer is active.
- In modular/HISM mode, only chunks touching a visible painted path evaluate
  the existing continuous path mask.
- From that evaluation, the plugin retains only upward triangles touched by
  OneMinus(VertexColor.G). Cliff walls, supports, undersides, and all unrelated
  continuous triangles are discarded.
- The compact path-only procedural component is offset upward by 0.25 Unreal
  units to avoid coplanar flicker, casts no shadow, and has no collision. The
  original modular/HISM meshes continue to own rendering fallback and collision.
- Neighboring chunks are already dirtied by the persistent path edit API, so
  path blends remain continuous across a chunk boundary.
- Modular-mode static-mesh baking appends the compact path-only sections and
  their vertex colors so the existing material path mask survives the bake.

Material contract
-----------------
Keep the current path material hookup:

    PathMask = OneMinus(VertexColor.G)

Use PathMask as the alpha between ordinary ground and the path material. The
path-only surface uses the source tile material; world-aligned ground/path
projection is recommended so its mask-zero ground matches the modular mesh.

Required UE4.27 verification
----------------------------
1. Install and compile using INSTALL_AND_TEST.txt.
2. Disable Continuous Terrain and paint flat, L-shaped, concave, erased, and
   chunk-crossing paths.
3. Test vertical ramps, fixed-45 horizontal cuts, and fixed stairs in every
   rotation. Inspect upper surfaces, side banks, walls, and undersides.
4. Confirm path editing does not add collision or shadow artifacts.
5. Bake in modular mode and verify the path mask, rendered surface, triangle
   count, and collision.
6. Re-enable Continuous Terrain and confirm accepted v1.2.43 behavior did not
   change.

Verification status
-------------------
This environment cannot compile or visually test the user's UE4.27 project.
The change is source-inspected only and is not claimed user-verified.
