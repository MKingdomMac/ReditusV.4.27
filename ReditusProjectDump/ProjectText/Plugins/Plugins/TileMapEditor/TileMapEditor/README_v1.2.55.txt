TileMapEditor UE4.27 v1.2.55

Compile correction for v1.2.54.

- Moves the existing GetCliffBaseBlendHeight local lambda to the beginning of
  each continuous block build, after GridPosition and BlockMinimum exist but
  before the stair, diagonal, ramp, and ordinary-wall branches diverge.
- This makes the shared cliff-foot height calculation available to the new
  diagonal wall band as well as its existing ordinary-wall consumers.
- The calculation itself is byte-for-byte unchanged.
- Retains the v1.2.54 stacked-slant junction and cliff-foot geometry changes.
