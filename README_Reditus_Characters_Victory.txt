Reditus Characters and victory repair for UE 4.27

The editor command rewires the existing character and victory Blueprints.
This revision replaces the enum switch setup with standard Blueprint enum
comparisons. It addresses both reported UE4.27 build errors.

1. Close Unreal Editor.
2. Extract this ZIP into D:\Cloned Reditus\ReditusV.4.27, beside Chapter1Reditus.uproject.
   Merge the Plugins folders and overwrite the previous repair files when prompted.
3. Open PowerShell in that game folder and run this command.

   powershell -NoProfile -ExecutionPolicy Bypass -File .\Install_Reditus_Characters_Victory.ps1

4. After the build finishes, open Chapter1Reditus.uproject.
5. Open Window > Developer Tools > Output Log. In its Cmd field, run this command.

   Reditus.RepairCharactersAndVictory

6. Wait for this message.

   Characters and victory Blueprints repaired and compiled. Save All to keep the graph changes.

7. Choose Save All, then test the Characters menu and a fresh victory.

The included .patch file installs the same plugin source with Git on a clean checkout.
When updating an earlier repair, extract and overwrite the files as described above.

What the command changes

- BP_UnitDetails runs ShowUnitDetails when the Characters screen opens.
  This populates its character name, class, stats and affinity data.
- The equipment slots read Weapon, Armor, Accessory, Helm and Boots from the selected
  character's S_PlayerUnitData.
- Equipment artwork uses the texture when a sprite reference is empty.
- A recognized portrait Image receives the selected character's Battle Image thumbnail.
  If the layout has a Canvas Panel, the command can add a small 56-by-56 header image.
  If the Designer layout has neither target, the Output Log identifies the missing
  portrait target. An Image named UnitPortrait can be connected on the next run.
- The resistance widgets look up each element through the existing GetElementResponse
  function and use the Normal, Weak, Resist, Null and Absorb responses.
- An installed core supplies its saved CurrentUnitAffinity map. Other characters
  use the Element Affinity map from DT_PartyMembers.
- BP_ExpBar supplies ExpExponent to the next-level GetExp calculation.
- BP_BattleController records party levels before UpdatePlayerUnitsDataBattleOver
  awards EXP. GetLeveledUpUnits then compares against those recorded levels.
- The prologue victory processes level-ups before its existing story continuation.
- BP_VictoryDialogue and BP_LevelUpDialogue use Do Once before continuing.
  This handles the automatic victory timer and player confirmation.

Expected checks

- Select Odin from Characters. Check his name, class, portrait and equipment.
- Odin's exported base affinities are Holy Weak and Dark Resist.
  Check the current Odin row and any installed core when comparing the icons.
- On the saved example with Odin at level 5 and 60 progress, the bar should show
  60 / 150. Odin's exported ExpExponent is 15. His total EXP is then 360 and the
  level 6 threshold is 450, so another 90 EXP reaches level 6.
- Compare the character's EXP before and after a fresh win. The awarded amount comes
  from the current battle's totalExpReward, which is authored through enemy rewards.
- After crossing a level threshold, dismiss the level-up window and check the level
  and stats in the Characters menu.
- Dismiss one victory early with Confirm. On another victory, let the timer dismiss
  the window. Confirm that movement and menu input work after both paths.

Installation behavior

The command recognizes nodes by the GUIDs in the supplied project export and checks
their connections before rewiring. A mismatch or Blueprint compile error aborts the
operation and invokes editor Undo. All graph edits form one Undo action.
Run it while Play is stopped. Repeating the command recognizes the installed wiring.

The source and graph operations passed a C++14 smoke test using the exported graph
connections, including repeat installation. This workspace has no Unreal Editor
installation. UE 4.27 compilation, Designer placement and gameplay validation must
be performed on the game PC.
