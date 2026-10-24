# Typed enemy animation fields — UE4.27.2

The enemy definition now matches the animation class choices in your player
`S_PartyMemberDefinitions`. It uses your existing initializer setter nodes.

| AnimationSystem | Row field | Allowed parent class |
| --- | --- | --- |
| Pixel2D | Pixel2DAnimClass | Pixel2DAnimInstance |
| PaperZD | PaperZDAnimClass | PaperZDAnimInstance |
| Existing | Neither new field | Preserve the actor's own animation |

The second option in your player screenshot is PaperZD. Paper2D itself is the
sprite/flipbook system, not the animation-instance type in that screenshot.

Both new fields are hard class references (`TSubclassOf`), matching your player
fields. They connect directly to the corresponding setter's class input without
a soft-class conversion or load node. Their types exclude enemy actor Blueprints.

## Install the native changes

1. Close Unreal Editor before changing this reflected native row structure.
2. Update the real project's `Source/Chapter1Reditus/Data/ReditusEnemyDefinition.h`.
   Use the focused `EnemyDefinition_Animation_Only.patch`, or paste the code in
   `Animation_Section_Replacement.txt` into that header.
3. A full header is also supplied under `Source/Chapter1Reditus/Data/`. It is
   based on the latest enemy SkillID header available here. If your real header
   has newer unrelated changes, use the focused patch/code instead of overwriting
   those changes with the full file.
4. Ensure **Pixel2D** and **PaperZD** are in `PublicDependencyModuleNames` in
   `Chapter1Reditus.Build.cs`. Add missing names while preserving other modules.
   See `Build_cs_Additions.txt`.
5. Build `Chapter1ReditusEditor`, Development Editor / Win64, then reopen UE.
   Use a full editor build for this USTRUCT change rather than hot reload.

No new C++ library or native base class is required. No `.uasset` edits are
included. The focused patch preserves all unrelated enemy fields and enums,
including SkillID, stats, skills, rewards, recruitment, and legacy actor classes.

## Set the new field on the row

In `DT_Enemies → HeremusHarrier`, set:

- Animation System = **Pixel2D**
- Pixel2D Anim Class = **BP_HeremusHarrierBattleAnim**

Save the table. The new picker excludes the actor `BP_HeremusHarrier`.
For a PaperZD enemy, select PaperZD and assign its animation Blueprint in
PaperZD Anim Class. The row editor shows only the selected system's new field.

The new fields initially have no value. The old path does not automatically
populate them. Assign the appropriate new field on every row you use.

`LegacyBattleAnimationBlueprintClass` retains its original reflected name and
`FSoftClassPath` type so saved values and Blueprint pins remain readable. It is
no longer editable in the row editor. No DataTable assets are automatically
migrated or overwritten. `bUsingPaperZD` and `AnimationProfileID` remain for
compatibility; the new graph selection uses `AnimationSystem`.

## Complete the existing initializer graph

Your latest screenshot already has the correct component Targets:
PaperZDAnimation for Set Anim Instance Class, and Pixel2D for Set Pixel2D Anim
Instance Class. Both setter execution outputs already continue into Clear,
followed by the enemy skills loop. Both class inputs shown are empty.

After the C++ build, in `BP_EnemyUnitBase → InitializeEnemyUnitBase`:

1. Refresh the existing **Break Reditus Enemy Definition** node if new pins are
   missing. Expose AnimationSystem, Pixel2DAnimClass, and PaperZDAnimClass.
2. Connect **Pixel2DAnimClass → Set Pixel2D Anim Instance Class → New Anim
   Instance Class**.
3. Connect **PaperZDAnimClass → Set Anim Instance Class → In Anim Instance Class**.
4. Replace the Boolean Branch immediately after **Set Current MP** with
   **Switch on EReditusAnimationSystem**. Connect the row's AnimationSystem to
   Selection, and Set Current MP's execution output to the switch.
5. Route Pixel2D to its Pixel2D setter path, PaperZD to its PaperZD setter path,
   and Existing directly to the existing Clear node.
6. Before each setter, validate that row's class with **Is Valid Class → Branch**.
   True calls the setter. False prints `Enemy animation class missing` and
   returns **Success = false**, instead of applying an empty class.
7. Both successful setters continue into **Clear → skills For Each Loop**.
   Preserve **Completed → Return with Success checked** and the existing
   row/skill failure paths.

Remove any temporary hardcoded Harrier selection when connecting the row's
class wire. Each enemy then uses its own row. These are manual wiring
instructions, not editor clipboard text.

The legacy class-spawn branch remains unchanged. The DataTable branch still
spawns the shared BP_EnemyUnitBase; the initializer applies its row animation.

## Verification and limits

The focused patch was applied locally and compared with the supplied full
header. Existing enums and unrelated definition content were checked for
preservation. This has **not been processed by Unreal Header Tool, compiled
against your installed plugins, or run in UE4.27**.

After building, check the two pickers exclude incompatible classes, compile the
refreshed Blueprint, then test HeremusHarrier and a second enemy with a different
animation. Confirm no hardcoded Harrier assignment remains. Test a PaperZD row
with its inherited component's animation source/render setup configured as in
your player implementation. Setting a class does not configure missing sources.

Temporarily leave a selected animation field empty: the guard should report
initialization failure instead of quietly keeping the base appearance. Restore
it afterward. Also test an unchanged legacy class-spawn encounter.
