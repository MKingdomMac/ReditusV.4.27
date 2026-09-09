TileMapEditor UE4.27 v1.2.66
================================

Slant hover preview
-------------------

- Hovering with Apply Slant selected now previews the actual replacement
  shape instead of showing only a generic cube outline.
- Ramps preview the complete multi-cell wedge and every affected cell boundary.
- Diagonal Edge previews the triangular prism half that will remain.
- Stairs preview their fixed twelve-step profile across both occupied cells.
- A thick cyan arrow points from the low side toward the high / retained side.
- Invalid placements display the complete preview in red.

Preserved behavior
------------------

- v1.2.65 stacked ramp continuity remains unchanged.
- v1.2.64 strict hybrid baking and per-block continuous-terrain exclusions
  remain unchanged.
- Preview rendering is editor-only and does not add runtime terrain geometry.

Validation note
---------------

This source package was statically checked, but must still be compiled and
tested in Unreal Engine 4.27.
