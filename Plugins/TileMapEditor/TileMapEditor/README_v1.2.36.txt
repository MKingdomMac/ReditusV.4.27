TileMapEditor UE4.27 - v1.2.36 Scrollable Tile Map Panel
========================================================

This source-only revision is based directly on v1.2.35 Fixed Gradual Stairs.
Its only code change is in FTileMapEdModeToolkit::Init:

- The complete existing SVerticalBox is placed inside an SScrollBox.
- The scroll box uses vertical scrolling, which is the Slate default.
- Every existing control and callback is retained unchanged.
- No runtime terrain source changed from v1.2.35.

When Tile Map mode is docked on the right, reduce the panel height until the
controls no longer fit. Use the mouse wheel or scrollbar and confirm that the
panel reaches the final help text. Then verify the stair, ramp, diagonal,
path, merge, and bake controls remain interactive.

Use README_v1.2.35.txt for the fixed-stair topology and required UE4.27 stair
tests. This environment cannot compile or visually test UE4.27, so v1.2.36
must be built and verified in the user's project before becoming a baseline.
