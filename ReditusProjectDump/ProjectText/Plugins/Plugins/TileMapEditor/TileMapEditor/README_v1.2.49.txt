TileMapEditor UE4.27 v1.2.49 - Fixed Low-Poly Continuous Topology
=================================================================

Baseline and scope
------------------
- Starts from the exact packaged v1.2.48 source, including its irregular inner
  cliff-lip correction, modular path overlay, optional terrain-detail pass, and
  cross-cell bake optimizer.
- Removes Chamfer Subdivisions Per Tile from the actor Details panel.
- Uses one internal base density of 2 for live continuous terrain and baking.
- Does not change chamfer width/depth, edge irregularity amplitude/wavelength,
  material masks, UV ownership, paths, decorations, collision settings, block
  data, or editor tools.

Topology correction
-------------------
The former property defaulted to 8, but several downstream loops multiplied it
and then imposed fixed minimums of 16, 12, or 32. Setting the property to 2
therefore could not reach the requested low density.

v1.2.49 uses these fixed derived densities:
- Detailed diagonal surfaces: 8 instead of 32 (old low-value floor: 16).
- Ramp longitudinal sampling: 6 instead of 24 (old low-value floor: 12).
- Diagonal supports: 8 instead of 32 (old low-value floor: 16).
- Partial diagonal surface grids: 8 instead of the hardcoded 32.
- Partial wall coverage: at most 8 instead of the hardcoded 32.

Ordinary exposed walls no longer import every rounded-junction alpha merely
because their lip is wavy. Straight top and wall boundaries share one sample
set derived from physical length and Edge Wavelength, capped at 8 segments.
Corner/tangent samples remain protected where a cell touches a rounded junction;
path and partial-cut samples are also mirrored across the shared boundary.

Bake welding
------------
The existing v1.2.48 optimizer already reused position indices when it replaced
at least one coplanar component. If no component qualified, however, the source
position array remained duplicated. v1.2.49 always runs the same quantized
position-index weld after continuous simplification. Wedge UVs, colors, normals,
tangents, materials, and smoothing data remain separate and unchanged.

Focused test
------------
1. Build UE4.27 Development Editor and confirm VersionName 1.2.49.
2. Confirm Chamfer Subdivisions Per Tile is absent from the actor Details panel.
3. Rebuild the exact v1.2.48 test terrain and inspect ordinary cliffs, the new
   irregular inner lip, convex/concave corners, all ramp directions, horizontal
   cuts, stairs, paths, supports, overpasses, chunk borders, and collision.
4. Bake the same terrain and compare LOD0 triangles/vertices with v1.2.48.
5. Search Output Log for both the cross-cell optimizer message and:
   TileMap bake position weld: old -> new positions.

Verification status
-------------------
The source and archive were statically checked here, but UE4.27 compilation and
visual verification still require the user's project. Keep v1.2.48 as rollback
until the lower-density topology passes the focused geometry/collision test.
