TileMapEditor UE4.27 v1.2.47 - Modular Path Printf Compile Fix
================================================================

Baseline and scope
------------------
- Starts from the exact v1.2.46 terrain-detail source.
- Fixes the UE4.27 FString::Printf template error reported in
  FindOrCreateContinuousChunkComponent.
- Does not change modular path-overlay behavior, continuous terrain, terrain
  details, bake topology, collision, slants, stairs, materials, or placement.

Cause and correction
--------------------
The conditional operator between two TEXT format literals converted the first
Printf argument into const TCHAR*. UE4.27 requires its checked Printf template
to receive the literal array type directly.

v1.2.47 uses two explicit FString::Printf branches, each with its own literal
format string, then passes the completed string to FName.

Verification status
-------------------
The rejected conditional-format pattern is absent from the source and all other
FString::Printf calls use direct TEXT literals. The source and archive were
statically checked here, but the final Development Editor build still requires
verification in the user's UE4.27 project.
