# Reditus Tech Mastery and Victory for UE 4.27

## Install

1. Close Unreal Editor. Extract the ZIP beside `Chapter1Reditus.uproject` in `D:\Cloned Reditus\ReditusV.4.27`. Replace the earlier installer and patches if prompted.
2. In PowerShell from that folder, run the installer.

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\Install_Reditus_TechMastery.ps1
   ```

3. Reopen the project after the build. With Play stopped, open **Window → Developer Tools → Output Log** and run `Reditus.InstallTechMastery`. Choose **Save All** when the command reports success. Run this step even if the earlier Level Up panel already works.

The installer checks the source, upgrades the previous Tech Mastery install in place, and builds the editor target. The editor command wires Overdrive availability, equipment previews, and Mastery into the existing Blueprints. If the editor reports a Blueprint compiler assertion, restart Unreal with the installer changes from that run unsaved.

## Victory and battle HUD

- The results slide in over the left side of the battlefield. The panel shows EXP, gold, Mastery, progress and unlocks for equipped Techs and Overdrives, and item drops.
- The party is framed on the right with a short zoom using `BP_BattleBase.staticCamera`. The camera holds that framing through Victory and every Level Up in the battle. It restores its previous settings after the final results screen closes.
- Victory and Level Up use slightly translucent backgrounds with solid text. Level Up shows the new stat totals, the changes since the EXP award, and any newly learned moves in a fixed layout that scales to fit.
- Players and enemies reveal when the native battle HUD is added. The skill list highlights the selected row with its frame. Skill names fit on one line beside their MP cost.

## Configure Tech Mastery

| Setting | Where to set it |
|---|---|
| Gear grants a Tech | `DT_Equipment` → gear row → **BattleSkillIDs** |
| Gear grants an Overdrive | `DT_Equipment` → gear row → **OverDriveSkillIDs** |
| Mastery needed | `DT_Skills` → move row → **MasteryPointsRequired**. Set **4** for four ordinary wins. Set **0** to leave the move equipment-only. |
| Table-spawned enemy award | `DT_Enemies` → enemy row → **Rewards → Mastery Reward Override** |
| Class-spawned enemy award | The enemy Blueprint's **Class Defaults → Rewards → Mastery Reward Override** |

An override of **0** uses **1 Mastery** for an ordinary enemy or **2** for a boss. Each eligible battle participant earns the highest reward in the encounter once toward each of their equipped moves. Progress and learned moves are tracked per character in the existing save. Select a Tech or Overdrive to see its Mastery in the lower half of the right-hand details pane. Gear previews with both types of moves use the existing left and right page controls.

On the first install of Overdrive Mastery, the editor command sets equipment-granted Overdrives with no cost to **4**. Existing positive costs carry over. After installation, edit the cost on the move's `DT_Skills` row.

## Configure enemy AI profiles

Set the **AI Profile** row handle on a `DT_Enemies` row. Pick `DT_EnemyAIProfiles` and the profile row. For an enemy spawned by class, set **EnemyCombatAI → AI Profile** on the enemy Blueprint. For a first migration of legacy IDs, run `Reditus.MigrateEnemyAIProfiles` in the Output Log and save `DT_Enemies`. Existing valid row handles take priority.

## Check in Play

1. Equip Airloft with **Drone Casing**. Check that `DT_Skills → ProtectOtherBuff` has **MasteryPointsRequired = 4**. This is the example the installer sets when it first adds the field.
2. Win an ordinary battle with Airloft participating. With fresh progress, the Victory screen should show **Mastery +1** and **Airloft / Protect 1/4**. Check the same progress in the Tech description and equipment preview.
3. At **4/4**, check the learned message. Unequip Drone Casing and verify Protect in the battle skill menu. Save and reload to check persistence.
4. Equip Caesar with **Iron Sword+**. Check **Fire Sword** in the **Overdrives** tab and its Mastery in the details pane. Open Caesar's Overdrive menu while Airloft is outside Overdrive. Fire Sword should show **WAIT** with **Airloft must enter Overdrive.** It becomes selectable when both units are in Overdrive. Win with the weapon equipped to check progress, then master Fire Sword and verify it stays available after unequipping the weapon.
5. Win a battle that levels up two party members. Check the signed changes beside the new stat values and that the whole panel fits at once. The battlefield should show faintly through both screens. The party should stay framed through both Level Ups and the next fight should start with the regular static camera framing.

Patch application and Blueprint hook locations were checked against the available source and export. The full Windows build and Play checks require the live project.
