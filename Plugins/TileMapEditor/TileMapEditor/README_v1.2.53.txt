TileMapEditor UE4.27 v1.2.53

Correct stacked horizontal-cut endpoint repair based directly on v1.2.51.
The ineffective v1.2.52 center-fan experiment is not included.

Root cause:

- The stacked cut's diagonal wall bottom used RoundDiagonalTangentPoint.
- The lower support block's visible outside walls used RoundConvexPoint.
- The special horizontal support complement used raw square corners.
- At the two cut endpoints, the flat complement therefore did not share the
  rounded XY boundary of the lower wall tops, leaving visible white wedges.

Fix:

- The diagonal side of the support complement retains the exact upper-wall
  tangent-rounded samples.
- Every outside side of the complement now uses the same RoundConvexPoint
  transform and alpha sampling used by the lower block's visible walls.
- All four horizontal-cut rotations construct a complete closed boundary.
- No buried full-square face is restored, avoiding overlap and z-fighting.

Unchanged: materials, vertex-color masks, paths, chamfer density, collision,
bake optimization, ramps, stairs, and unrelated continuous geometry.

Test +X, -X, +Y, and -Y stacked cuts from above and below, checking both
endpoints and the baked static mesh.
