TileMapEditor v1.2.69 - Fillet where two horizontal slants meet

Baseline
--------
This revision is built directly from the accepted corrected v1.2.68 source.
The v1.2.68 upper ledge chamfer beside vertical ramps remains unchanged.

Fix
---
Two exposed horizontal 45-degree diagonal cuts could meet at a sharp V-shaped
cliff point. The existing tangent-junction system only recognized a diagonal
edge meeting a straight edge, so it never rounded a diagonal-to-diagonal pair.

v1.2.69 now:

* recognizes exactly two exposed diagonal boundary rays meeting at 90 degrees;
* applies the established physical tangent fillet only at their shared point;
* uses the same rounded point and normal for the top, cliff lip, and wall;
* supports all four rotated horizontal-slant orientations;
* rejects collinear diagonal seams and mixed junctions that also own a straight
  edge, preventing the rounding from leaking into nearby cubes;
* preserves the accepted diagonal-to-straight fillet, single-slant edges,
  ordinary cube corners, flat tops, vertical ramps, materials, collision, and
  optimized static-mesh baking behavior.

UE4.27 test
-----------
1. Replace the previous TileMapEditor plugin source with this folder.
2. Rebuild the UE4.27 editor target and confirm VersionName 1.2.69.
3. Make two horizontal 45-degree slants meet at an exposed V-shaped point.
4. Confirm only the shared V is rounded and its cliff wall follows the curve.
5. Rotate the pair through all four orientations and repeat the check.
6. Confirm a single horizontal slant, a collinear two-slant seam, nearby normal
   cubes, and all vertical ramps retain their v1.2.68 shapes.
7. Bake an optimized static mesh and confirm the same isolated fillet remains.
