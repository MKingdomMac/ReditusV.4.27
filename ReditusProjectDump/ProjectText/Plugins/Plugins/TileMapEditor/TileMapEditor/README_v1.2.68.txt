TileMapEditor v1.2.68 - Corrected upper ledge chamfer beside vertical ramps

Baseline
--------
This revision is built directly from the accepted v1.2.67 source. It does not
contain the rejected wall-band implementation from the earlier v1.2.68
attempt. Horizontal cuts, stacked-ramp continuity, hybrid continuous/modular
baking, collision, paths, cliff-base blending, and surrounding ordinary blocks
retain their v1.2.67 behavior.

Fix
---
An ordinary continuous block beside a valid vertical ramp previously kept a
perfectly flat top right up to the shared edge. The ramp generator closed the
height difference with a vertical wall, but the ordinary block never treated
that height-aware boundary as an exposed upper ledge, so it had no physical
chamfer.

v1.2.68 now:

* recognizes a valid neighboring ramp as an upper drop only where the ramp
  surface is actually below the ordinary block's top;
* applies the existing physical top-surface chamfer to that local shared ledge;
* tapers the chamfer depth to zero at a ramp endpoint that reaches the flat top;
* makes the ramp-side closure end at the exact same chamfered boundary;
* keeps the entire remaining vertical closure ordinary cliff-wall material,
  so lower/internal ramp layers do not receive a false cliff-edge strip;
* interpolates the shared boundary from the ramp mesh's own longitudinal
  samples, avoiding a different curve or a new crack between the two meshes;
* leaves non-adjacent cubes and the separate two-slant corner behavior
  unchanged;
* uses the same continuous generator for live terrain and optimized baking.

UE4.27 test
-----------
1. Replace the previous TileMapEditor plugin source with this folder.
2. Delete the project's plugin Intermediate/Binaries only if Unreal requests a
   rebuild, then rebuild the UE4.27 editor target.
3. Place an ordinary continuous block beside the side of a valid vertical
   ramp, including a ramp stacked above another terrain layer.
4. Confirm the ordinary block's exposed upper ledge has a real bevel and the
   wall below it stays ordinary cliff wall with no lower cliff-edge band.
5. Rotate the ramp through X+, X-, Y+, and Y- and repeat the check.
6. Test a one-cell 45-degree ramp and a two-cell 26.565-degree ramp, including
   compatible stacked runs.
7. Bake an optimized static mesh and confirm the same chamfer and wall material
   ownership remain.
