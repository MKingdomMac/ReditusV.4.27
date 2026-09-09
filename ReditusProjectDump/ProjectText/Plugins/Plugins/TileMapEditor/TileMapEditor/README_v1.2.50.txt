TileMapEditor UE4.27 v1.2.50 - Direction-Independent Cliff UV Seam
===================================================================

Baseline and scope
------------------
- Starts from the exact v1.2.49 fixed low-poly topology source.
- Keeps the internal chamfer density of 2 and every v1.2.49 subdivision,
  cross-cell simplification, and bake-position-weld rule.
- Changes only render-vertex ownership at the ground/cliff-lip UV seam for
  ordinary and diagonal continuous top surfaces.
- Does not move geometry, change triangle winding/count, edit vertex colors,
  alter path/cliff masks, change collision, or require a material edit.

Cause and correction
--------------------
Ground top UV0 is planar actor-local X/Y. The dedicated cliff-top strip UV0 is
oriented along each exposed boundary. Before this revision, a ground triangle
and an adjacent lip triangle reused one render vertex at their seam, even
though those two surfaces require different UV0 and tangent values.

On the bottom-facing edge the two UV directions happened to agree, allowing
the bake optimizer to simplify that region. On the other three directions the
rotated or reversed cliff-strip direction made the shared attributes
incompatible, leaving dense square bands in the baked mesh and producing an
axis-dependent cliff blend.

v1.2.50 keeps the seam at exactly the same position but lazily creates a second
render vertex only when a cliff-lip triangle needs it:
- Ground triangles retain standard ground UV0 and tangent data.
- Lip triangles use the directional cliff-strip UV0 and tangent data.
- Both copies retain the same position, normal, vertex color, and path/cliff
  masks.
- The raw-mesh position weld can still share the physical position while UE4
  preserves the separate wedge UV/tangent attributes.

Focused test
------------
1. Build UE4.27 Development Editor and confirm VersionName 1.2.50.
2. Reopen the map or edit one terrain cell so live chunks rebuild.
3. Bake a new static mesh; an already baked asset cannot update itself.
4. Inspect the same outer rectangle from above with Vertices or Wireframe on.
5. Confirm the top, left, right, internal, and bottom cliff edges now behave
   consistently, while the physical wavy lip remains unchanged.
6. Check paths, diagonal cuts, ramps, stairs, corners, collision, vertex colors,
   and the dedicated cliff-top texture on every axis.

Verification status
-------------------
The focused source diff, metadata, delimiter balance, and source-only archive
were statically checked here. UE4.27 compilation and visual verification still
require the user's project. Keep v1.2.49 as rollback until this exact directional
test passes.
