# Reditus C++ damage and enemy-table migration checkpoint

Reviewed against ReditusProjectDump(5), exported 2026-10-09 in UE4.27.2. These
files prepare the next integration step; they do not change your live project,
placed NPCs, or Blueprint graphs automatically. The C++ additions have not
been compiled or run in Unreal here.

## Findings

- All 28 DT_Enemies rows match the native FReditusEnemyDefinition schema.
- All 28 rows exactly match ReditusEnemyStatsMigrationV2: initial, first-level,
  and last-level stats, MaxLevel, MaxStamina, MoveSpeed, and difficulty opt-out.
- Enemy classes exist and derive from the existing BP_EnemyUnitBase. All
  nonempty skill IDs exist in DT_Skills, and checked legacy class paths resolve
  to assets in the dump.
- 25 enemies contain an empty SkillID/None-class weighted entry. That is the
  legacy regular-attack option. Preserve it and its weight when migrating AI.
- Five RecruitedUnitID values still use the old class-based ...Recruit names.
  DT_PartyMembers uses BucketOfBolts, Desmodus, HackedHarrier, HeremusHarrier,
  and HeremusRover. The supplied CSV changes only those five ID fields.
- NPC encounter pools still use Blueprint class keys. Their spawnChance,
  minLevel, and maxLevel belong to each encounter slot. They must be retained
  when replacing class keys with enemy row IDs; they do not belong in DT_Enemies.
- In this snapshot BP_BattleController still uses its Blueprint damage path.
  The native calculator and bridge already exist, but are not wired into it.
- BP_EnemyUnitBase still initializes from its legacy stat fields. Merely
  selecting a class from DT_Enemies will not apply the row's stats and skills.
- The main enemy Actor spawn is in BP_BattleBase.EventGraph, after
  Get Enemy Spawn Data -> Get Random Enemy -> Branch. BP_Spawn.EventGraph
  also directly spawns BP_HeremusHarrier for a scripted summon.
- BP_DamagedHeremusHarrier, BP_BucketOfBolts, and BP_BattleDummy override
  Set Unit Stats without calling the parent implementation in this snapshot.
  Apply the table data before their initialization too; changing only the
  BP_EnemyUnitBase Set Unit Stats event would miss those overrides.

## Source changes

Merge the supplied Source/Chapter1Reditus files into the matching module folder.
Avoid creating an extra nested Source/Source folder.
If these source files have newer edits than the reviewed snapshot, merge the
new helper functions and the fallback change into your current files rather
than replacing newer work with the complete snapshot-based files.

1. ReditusDataLibrary.h / .cpp retain GetEnemyDefinition, validate the table row
   type, and add three callable helpers:
   - ResolveEnemySpawnDefinition: input table, EnemyID, and expected base class;
     output full definition and the resolved existing Actor class; false on an
     invalid row or incompatible class.
   - FindEnemyIDForLegacyClass: uniquely maps a configured legacy class to its
     row ID, supporting migration of existing encounters. It does not change
     an NPC or choose spawn weights for you.
   - ResolveLegacyEnemySpawnDefinition combines both steps in one call. Feed
     it the class already selected by the existing encounter logic, then use
     its row definition and resolved Actor class in the central spawn path.
     This is the first migration path: existing placed NPC lists, encounter
     weights, and level ranges remain usable without re-entering map data.
2. ReditusBattleBridgeComponent.cpp retains the existing implementation and
   changes only its damage fallback. Without a battle subsystem it uses the
   stateless calculator rather than returning an empty result.
3. Private/Tests/ReditusCombatMigrationTests.cpp adds engine automation checks
   for damage rules, the bridge fallback, and the existing difficulty rules.
   No new runtime stats, tuning values, schemas, or enemy classes are introduced.

## Corrected enemy table

Data/DT_Enemies_RecruitIDs_Fixed.csv is a full table export from the reviewed
snapshot, with only the five recruitment IDs corrected. If your current table
has edits since October 9, apply the five changes below to the current table
instead of importing the older full CSV over those edits.

| Enemy row | Old RecruitedUnitID | Correct current party row ID |
| --- | --- | --- |
| BucketOfBolts | BucketOfBoltsRecruit | BucketOfBolts |
| Desmodus | DesmodusRecruit | Desmodus |
| HackedHarrier | HackedHarrierRecruit | HackedHarrier |
| HeremusHarrier | HeremusHarrierRecruit | HeremusHarrier |
| HeremusRover | HeremusRoverRecruit | HeremusRover |

## Intended enemy spawn path

For each existing encounter slot, retain the weighted pool and level range.
For existing maps, keep the current weighted class selection and chosen level;
ResolveLegacyEnemySpawnDefinition translates that selected class into its
EnemyID and reads the table. Newly authored row-ID encounters can call
ResolveEnemySpawnDefinition directly. Both load LegacyEnemyActorClass.
Use the existing enemy child Actor for its
animations and custom behavior. Use deferred spawning so the selected level
and row data can be installed before BeginPlay. Populate the existing enemy
initial/first/last stat fields, skills, affinity, rewards, recruitment data, and
relevant configuration before finishing the spawn. The row should supply the
data; the child Blueprint may still supply behavior. Register the finished
Actor in the existing enemyUnits and AllUnits collections exactly as before.

The generic Actor class output may require a cast to BP_EnemyUnitBase in the
current Blueprint graph. Inspect that graph before replacing its spawn node.
Do not change the existing encounter map's key type in place. The central
adapter preserves those configured NPC entries; converting their editor
authoring representation to row IDs can be a later, separately validated step.

## Damage integration rules to preserve

- Existing tuning: normal 1.0x; weak or critical 1.5x; weak plus critical 2.0x.
- Stagger guarantees critical when allowed and adds no independent multiplier.
- Physical and magical offense roll within the existing 90-100% tuning range.
- Keep modifiers, QTE results, AMP, defending, and overdrive contributions in
  the request. Resolve each action once; avoid rolling offense or applying
  elemental/critical multipliers again in the remaining Blueprint path.
- The request's critical chance is in percent. Inspect the current runtime
  Critical and crit values before wiring it. The snapshot's legacy roll uses
  Critical * crit * 0.01 as Random Bool with Weight's input.
- The native result distinguishes hit, damage, healing, weakness, resistance,
  null, and absorb. The old CalculateDamage outputs have no separate healing
  pin. Absorb needs an explicit HP restoration path during integration.
- Preserve weakness-related stamina reduction, stagger triggering, aggro,
  damage-text flags, and TakeDamage/HP handling around the calculator.
- Native FReditusUnitStats and Blueprint S_UnitStats are separate struct types;
  copy verified fields explicitly instead of treating them as interchangeable.

## Build and validation checkpoint

Build the existing Chapter1Reditus project as Development Editor for UE4.27.2.
Open Window > Developer Tools > Session Frontend, select Automation, and run
Reditus.Combat.Migration. The included tests validate the native calculation
rules; they do not prove that the live battle graph is calling the calculator.

Epic's UE4.27 references:
- https://dev.epicgames.com/documentation/en-us/unreal-engine/automation-technical-guide?application_version=4.27
- https://dev.epicgames.com/documentation/en-us/unreal-engine/automation-system-user-guide?application_version=4.27

Next graph checkpoint: export the current BP_BattleBase.EventGraph section
from Get Random Enemy through SpawnActor and Add to enemyUnits, together with
the enemy initialization path, plus BP_BattleController CalculateDamage and DealDamage.
Those determine the exact field mapping and old calculation nodes to replace.
The latest NPC screenshot confirms class-based encounter authoring but does
not show the actual Actor spawn node or its initialization execution order.

## Verification completed here

The offline audit checked schema columns, every migrated stat value, IDs,
weights, referenced assets, enemy inheritance, and the recruitment corrections.
The corrected CSV was round-tripped and compared field by field: only five
RecruitedUnitID values differ. The bridge patch was compared to the dumped
source and is confined to the calculator fallback. Unreal build, automation
execution, live enemy AI, boss behavior, and map-level migration remain to be
verified in your editor.
