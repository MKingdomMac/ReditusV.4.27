TileMapEditor UE4.27 v1.2.51 - Affine Cliff-Blend Bake Culling
================================================================

Baseline and scope
------------------
- Starts from v1.2.50, which itself starts from the v1.2.49 fixed low-poly
  continuous-terrain baseline.
- Keeps Chamfer Subdivisions removed and the internal base density fixed at 2.
- Keeps the v1.2.50 split ground/cliff UV and tangent ownership at the physical
  lip seam.
- Changes only the static-mesh bake optimizer's attribute proof.
- Preserves modular path overlays, terrain details, collision, live topology,
  wavy geometry, material wiring, and the unconditional bake position weld.

Cause and correction
--------------------
The bottom-facing lip could simplify because its tangent and UV direction
matched the flat ground direction. The top, left, right, and internal lip bands
retained dense vertices because they combined direction-dependent cliff UVs and
tangents with a linear vertex-color blend. v1.2.49 required every triangle to
have one constant color, while the earlier standalone affine-color experiment
still encountered the unsplit UV seam.

v1.2.51 combines both necessary safeguards:
1. Ground and cliff triangles own separate UV/tangent wedges at the exact same
   physical seam position.
2. The bake optimizer accepts a connected planar region only when its UV and
   RGBA vertex-color fields are mathematically affine, share one tangent frame,
   preserve material and smoothing ownership, reproduce every original vertex
   within quantization tolerance, retain a single valid boundary, and generate
   correctly wound replacement triangles.

If any proof fails, the original v1.2.50 triangles are retained. No exposed
surface is deleted merely because it is a cliff blend.

Focused test
------------
1. Build UE4.27 Development Editor and confirm VersionName 1.2.51.
2. Reopen the map or edit one cell to rebuild the live chunks.
3. Bake a new static mesh; do not inspect an older baked asset.
4. Enable Vertices or Wireframe in the Static Mesh Editor.
5. Compare the previously circled bottom edge against the X-marked top, left,
   right, and internal cliff bands.
6. Confirm all directions now reduce consistently and the visible cliff blend,
   paths, masks, normals, wavy silhouette, collision, ramps, stairs, diagonal
   cuts, corners, and overpasses remain unchanged.
7. Search the Output Log for:
   TileMap affine cliff-blend bake optimizer
   TileMap bake position weld

Verification status
-------------------
The source diff, combined preservation rules, metadata, delimiter balance, and
source-only archive were statically checked here. UE4.27 compilation and the
same-terrain bake comparison still require the user's project. Keep v1.2.49 as
the accepted rollback and disregard v1.2.50 if this combined pass fails.
