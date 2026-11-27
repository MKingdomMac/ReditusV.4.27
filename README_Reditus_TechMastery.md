# Reditus Tech Mastery for UE 4.27

## Install

If an earlier run showed the Blueprint `REINST_` compiler error, save your unrelated work and close Unreal with that run's Mastery changes unsaved. If you already saved them, the updated command can recompile those assets in the next editor session.

1. With Unreal Editor closed, extract the updated ZIP contents beside `Chapter1Reditus.uproject` in `D:\Cloned Reditus\ReditusV.4.27`. Replace the older installer and patch files if prompted. This update builds on the Reditus Enemy Combat AI source already in your project.
2. Run this from the game folder in PowerShell.

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\Install_Reditus_TechMastery.ps1
   ```

   The script detects earlier versions of the Tech Mastery source and applies the matching update before rebuilding.

3. Reopen the project after the build. With Play stopped, open **Window → Developer Tools → Output Log**. For a first profile migration, run `Reditus.MigrateEnemyAIProfiles` and save `DT_Enemies`.
4. Run `Reditus.InstallTechMastery` in the same Output Log. Choose **Save All** after the updated command reports success. The updated command uses UE 4.27's newer Blueprint skeleton reinstancer during the install. If a compiler assertion appears, leave that run's Mastery changes unsaved and restart the editor.

## Where to set an enemy profile

| Spawn method | Set the profile here |
|---|---|
| Enemy row | `DT_Enemies` → enemy row → **AI → AI Profile**. Pick `DT_EnemyAIProfiles` and the **Row Name**. |
| Enemy Blueprint class | Open the enemy Blueprint and select **EnemyCombatAI → AI Profile**. |

Table initialization already sets `EnemyID`. The AI uses it to read the selected profile from that enemy's row. A row's valid profile takes priority. With the row handle empty, the AI can use the component's profile. The migration fills in valid legacy IDs and preselects the profile table on existing empty rows. It reports legacy IDs it could not resolve.

## Where to set Mastery

| Setting | Where to set it |
|---|---|
| Gear grants a Tech | `DT_Equipment` → item row → **BattleSkillIDs** |
| Points to learn a Tech | `DT_Skills` → skill row → **MasteryPointsRequired**. Set **4** for four ordinary wins. **0** leaves the Tech equipment-only. |
| Mastery reward for a table enemy | `DT_Enemies` → enemy row → **Rewards → Mastery Reward Override** |
| Mastery reward for a class enemy | The enemy Blueprint's **Class Defaults → Rewards → Mastery Reward Override** |

An override of **0** uses **1 Mastery** for a normal enemy or **2 Mastery** for a boss. The boss default reads the enemy's existing **Boss Death → Is Boss** setting. A positive override sets the reward amount. A victory grants the highest configured reward from the encounter once to each eligible battle participant's equipped Techs. Gear that grants the same Tech twice contributes one award. Learning and partial progress are stored per character in the existing save.

The victory screen shows **Mastery +1** or the actual award beside EXP when a Tech gains progress. It lists newly learned Techs below that line. Equipment and Techs show progress on the skill button, and the Tech stays available after the gear is removed once learned.

## Check in Play

1. Equip Airloft with **Drone Casing**. The installer sets the **Protect** row (`ProtectOtherBuff`) to **4** when it first adds the Mastery field. Win one ordinary fight and check **Mastery +1** and **Protect 1/4** in the equipped Tech list.
2. Win three more ordinary fights, or use a boss fight to check the larger award. The mastery victory should say **Airloft learned Protect**. Swap out Drone Casing and check Protect in the battle skill menu. Save and load to verify the Tech is still there.
3. In `DT_Enemies`, select a known profile on an enemy used in a table-spawned encounter and test its behavior. A class-spawned enemy can use the profile on its Blueprint component.

The source upgrade and graph insertion points were checked locally. The Windows build for this revision, Blueprint installation and Play checks still need to run in the live project.
