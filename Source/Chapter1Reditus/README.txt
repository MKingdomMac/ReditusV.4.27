Reditus Player Unit Defaults Exporter (UE4.27)
================================================

INSTALL
1. Close Unreal Editor.
2. Copy ReditusPlayerDefaultsExporter.cpp into:
   <YourProject>/Source/Chapter1Reditus/
3. Build Chapter1ReditusEditor in Development Editor / Win64.
4. Reopen the project.

RUN
1. Open Window > Developer Tools > Output Log.
2. Enter this console command:

   Reditus.ExportPlayerUnitDefaults

OUTPUT
Files are written to:

   <YourProject>/Saved/ReditusMigration/

PlayerUnitDefaults_Import.csv
  Filtered rows with the columns shown in S_PartyMemberDefinitions.
  Initial Stats, First Level Stats, and Last Level Stats are each exported as
  complete S_UnitStats values suitable for a DataTable CSV cell.
  IsRangedUnit is exported from the shared BP_UnitBase Boolean default.
  DefaultProjectileClass is exported from BP_UnitBase's existing projectile
  class reference.

PlayerUnitDefaults_AllCandidates.csv
  Every concrete BP_PlayerUnitBase child, including generic template classes.

PlayerUnitDefaults_Expanded.csv
  Every member of Initial Stats, First Level Stats, and Last Level Stats in its
  own Excel-readable column. It also preserves the three legacy affinity values
  and the old Robot Core class.

PlayerUnitDefaults_AllEditable.csv
  Long-form audit of every editable property inherited by each player unit.

PlayerUnitDefaults_ExporterLog.txt
  Export counts and the exact Blueprint-to-row mapping.

IMPORTANT
- CoreDropID is derived from the existing Robot Core class reference.
- Add IsRangedUnit (Boolean) to S_PartyMemberDefinitions before importing.
  Keep the existing BP_UnitBase IsRangeUnit function: it can still combine this
  definition flag with AttackAnimation/equipment behavior at runtime.
- Add DefaultProjectileClass as a BP_ProjectileBase Class Reference. Ranged
  units receive their existing projectile; melee units can leave it as None.
- Starting equipment IDs are written as None because the old player Blueprint
  defaults do not contain stable equipment DataTable row IDs.
- Element Affinity is blank in the import CSV. The old project has single
  Blueprint affinity enums, while the new field is a map of
  EReditusElementAffinity to EReditusElementResponse. The old values are kept in
  PlayerUnitDefaults_Expanded.csv for manual conversion without data loss.
- The exporter only runs in the editor and adds nothing to packaged builds.
