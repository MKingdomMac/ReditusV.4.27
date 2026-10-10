Reditus Enemy Definition - migration package

FILES
-----
Data/ReditusEnemyDefinition.h
    Full static enemy definition schema intended for DT_Enemies_New.

Migration/EnemyBlueprintInventory.csv
    32 Blueprint classes detected as descendants of BP_EnemyUnitBase.
    Four base/template archetypes are marked as templates.

WHAT IS INCLUDED IN THE DEFINITION
----------------------------------
- Display identity, source enemy Actor class, thumbnails/tags
- Existing Initial / First Level / Last Level stat system + MaxLevel
- Enemy class and elemental affinity data
- Explicit weakness/resistance/immunity/absorb slots for the new data-driven system
- Fixed MaxStamina (CurrentStamina remains runtime)
- Movement/Battle speed, base aggro, ranged flag, Overdrive capability
- Block/parry, disassemble, steal, headshot and projectile configuration
- Weighted enemy skills (matching the current Skill Class -> Float Weight map)
- EXP, gold, drops and optional loot-table ID
- Current normal/hard difficulty configuration fields
- Recruitment mapping and robot temperament
- Pixel2D/PaperZD animation system/profile + legacy AnimBP reference
- Stable IDs plus transitional soft references so migration can be gradual

INTENTIONALLY NOT STORED IN THE DATATABLE
-----------------------------------------
These are runtime state, not definition data:
CurrentHP, CurrentMP, CurrentStamina, Staggered, current buffs/ailments,
current target, current AMP, current Overdrive, current exhaustion,
animation state booleans, current battle/controller references, current level,
temporary block/parry counters, current aggro after battle changes, etc.

Also NOT stored here:
Encounter spawn chance / min spawn level / max spawn level.
Those belong to encounter/spawn definitions because the same enemy can appear
in multiple encounters with different spawn rules.

IMPORTANT ABOUT EXACT OLD ENEMY NUMBERS
---------------------------------------
The current Reditus project dump contains Blueprint schemas/graphs and asset
relationships, but it does not contain inherited child Blueprint CDO override
values for most enemy classes. That means exact per-enemy Class Defaults
(HP/Attack/etc.) cannot be safely reconstructed for every enemy from this dump.
The inventory therefore lists the existing enemy classes without inventing
numbers. Existing values should be copied/exported from each child Blueprint
before its old defaults are removed.
