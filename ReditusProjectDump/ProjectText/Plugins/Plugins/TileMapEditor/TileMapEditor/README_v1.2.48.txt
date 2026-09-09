TileMapEditor UE4.27 v1.2.48 - Irregular Inner Cliff Lip
========================================================

Baseline and scope
------------------
- Starts from the exact packaged v1.2.47 source.
- Corrects the ruler-straight inner boundary where a flat terrain top meets
  the physical continuous-terrain cliff chamfer.
- Does not change the material graph, vertex-color channel ownership, cliff
  height, outer silhouette waveform, paths, details, bake optimizer, slants,
  stairs, chunk ownership, or collision settings.

Cause and correction
--------------------
The existing edge wave was strongest at the exposed outer edge but faded to
exactly zero at the inner chamfer row. That zero produced a perfectly straight
ground-to-lip boundary even though the visible outer cliff edge was irregular.

v1.2.48 carries 50 percent of the same deterministic edge wave into the inner
row. The flat terrain remains level; only that row's horizontal position moves.
The full wave remains at the outer edge, endpoint/corner fading remains active,
and the inner-row displacement is bounded by half of the already-clamped Edge
Irregularity value.

Focused test
------------
1. Restore the accepted material mask:
   VertexColor.A -> OneMinus -> Multiply 100 -> Saturate, feeding the lower
   input of the final CliffTopMask multiply.
2. Rebuild the same live continuous terrain shown in the report.
3. Inspect a long straight exposed cliff in Lit and Unlit modes. The inner lip
   boundary should now meander gently instead of forming a ruler-straight band.
4. Check convex/concave corners, adjacent equal-height blocks, chunk borders,
   paths near a cliff, stacked platforms, collision, and the optimized bake.
5. Compare the outer cliff silhouette with v1.2.47; it should be unchanged.

Verification status
-------------------
The source and archive were statically checked here, but UE4.27 compilation and
visual verification still require the user's project. Keep v1.2.47 available
until this focused geometry test passes.
