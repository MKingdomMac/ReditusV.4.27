# Reditus quests — 2.9.6 / UE4.27

The existing quest journal shows both the original Blueprint quests and the graph quests. **Is Main Quest** on a quest determines its active page. Completed quests stay in the finished list. Follow the existing journal arrows to switch pages.

For this update, merge the included `Plugins/ReditusQuestNPC` folder and build **Development Editor / Win64** with Unreal closed. If your journal is already connected, the update takes effect when you reopen it in play. If this is your first integration, run `Reditus.IntegrateQuestJournal` in the editor Output Log before play, then save `BP_QuestDetails` after testing the connection.

The quest clock now pauses during regular Level Sequence playback and while a scene waits for dialogue. **Turn In Quest on Arrival** on the existing objective component can finish a Reach Location quest and use the normal item reward delivery. See [ODIN_TIMED_QUEST.md](ODIN_TIMED_QUEST.md) for the editor setup and first play test.

## Cutscene controls

Close Unreal, merge the included **Plugins** folder into the project, and build **Development Editor / Win64**. Reopen the project and run DeusSilentii.

Your exported PlayLevelSequence already stores the active actor in Current Sequence and passes the finish event to On Skipped. If its Configure node has an empty Sequence Actor pin, the plugin reads that stored actor when called from the player controller. The existing Allow Skip checkbox controls the scene.

- **Allow Skip checked.** Hold Shift until the one-second gold bar fills. The scene should finish and take the existing transition to PrologueMap. Release Shift early to cancel.
- **Allow Skip unchecked.** Press Shift. The protected-scene message should appear while dialogue stays open.
- **Auto on.** A voiced line gets a quarter-second pause after speech. A silent line gets one to four seconds based on length. When a Sequencer dialogue finishes, the UI resumes the paused player so the next cue can run. Check one dialogue with the Play Level Sequence option and one using the Director's Pause pattern. The manual playpausedsequence event remains available.
- The cursor is hidden during gameplay dialogue and when a configured cutscene begins.

For a new playback route, call **Configure Level Sequence Skipping** for its actor before Play. Set Allow Skip for the scene and supply the existing finish handoff when skipping is enabled. The viewer uses this registered player when the controller has no active Current Sequence. Keep required quest setup in the shared finish handoff so normal completion and skipping reach the same gameplay state.

The standalone checks exercise the exported actor/checkbox setup, deferred dialogue close, authored play cue and manual resume. The Windows UE4.27 build and in-game pass still need verification.

**2.8.2 picker fix.** The Quest field in NPC and objective definitions now lets you select `DT_QuestCatalog` or `DT_Quests`. Build the update with Unreal closed to reload the property metadata.

**2.8.1 build compatibility patch:** corrected the const catalogue handle, editor asset opening, UE4 pin destruction and two matching-header include-order failures reported in Visual Studio. Merge the updated Plugins folder and rebuild with Unreal closed.

**New quest graph editor:** Content Browser → Reditus → Reditus Quest Graph. Complex quest definitions live in graph assets; DT_QuestCatalog lists them and can also reference existing simple rows. Linked conversations open your existing dialogue editor, preserving its events. See [QUEST_EDITOR_GUIDE.md](QUEST_EDITOR_GUIDE.md) for installation, the first hunt and compatibility details. Native UE4.27 compilation and live editor verification are pending.

Use **Simple Quest Text** for fetch quests and monster hunts. Fill four localized lines on the NPC row; the existing dialogue UI handles acceptance and turn-in. Quest progress, rewards, markers, timers and save integration use the current subsystem.

## Install and clean an existing installation

1. Close Unreal. Merge this package into the project root: the included Plugins folder merges with your existing Plugins; keep the existing DialoguePlugin and MinimapPlugin assets.
2. Copy **Cleanup_Reditus_QuestNPC.ps1** beside your `.uproject` file. Run it from that project root with PowerShell:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\Cleanup_Reditus_QuestNPC.ps1
   ```

3. Build **Development Editor / Win64** with Unreal closed; reopen after a successful build.

The helper removes the retired example-generator source only if it still matches the old shipped file, matching obsolete package guides/backups if present, and this plugin's **Binaries** and **Intermediate** build output. Modified old files are preserved and reported. It does not touch Content, Config, Saved, quest tables, dialogue assets or your Blueprint save data. It refuses to run while Unreal is open. Use `-WhatIf` to preview, or `-KeepBuildFiles` to retain generated build output. Build output is recreated by compilation, so its installed size can grow again.

## What was cleaned

- Removed the extortion example-generation command and its source file.
- Fresh setup now creates only the simple hunt and Letter-possession quest examples, when their prerequisites are available. It no longer seeds intro/follow-up/Wait/gate/NPC-challenge/recruitment tutorial quests.
- Removed duplicate old DialoguePlugin backup copies and superseded manual multi-step setup/migration/planning guides.
- Replaced the accumulated installation/changelog text with this current guide. Tests and the technical reference remain available for maintenance.

Existing table rows are preserved, including edited test rows. Existing authored quests, dialogue/recruitment, shared progress/save/reward code and dialogue artwork still load through their current APIs. Version 2.8.0 adds that editor as a separate authoring path; installing it does not convert existing table rows or recruitment dialogues.

## Hunt choices

Before accepting a hunt, the offer still shows **Accept Choice** and **Not Now Choice**. After completing the required monster kills and returning to the giver, a hunt with no required item possession or item hand-in costs shows only **Hand In Choice**. Selecting it runs the existing checked turn-in and then shows **Completed**; merely opening the conversation never pays a reward.

The same **Ready to Deliver** NPC line and **Hand In Choice** player text are used. No additional Hunt Complete field or new dialogue asset is needed. Fetch/mixed-item quests and item-cost exchanges keep their option to defer. Dialogue Assets mode, recruitment and UI artwork retain their existing behavior.

## Your one-enemy hunt

In **DT_Quests / Test_DefeatBoss**, use one stage and one required objective:

| Field | Value |
| --- | --- |
| Stage ID | Hunt |
| Objective Kind | Defeat Enemy |
| Target ID | DeathBoar |
| Required Count | 1 |
| Optional | Off |
| Timer / Mode | None |
| Rewards / Gold | 500 for the test |
| Rewards / Experience | 0 for the default exchange |
| Turn In Items | Empty |

The objective's Item Row is unused for Defeat Enemy. Item rewards belong under Rewards / Items.

In **DT_QuestNPCs / MonsterGiver**, select **Quest = DT_Quests / Test_DefeatBoss**, then **Dialogue Mode = Simple Quest Text**. Fill **Quest Text / Speaker Name, Offer, Reminder, Ready to Deliver, Completed**. Set the advanced Hand In Choice to “Claim reward.” if desired. Keep the placed giver's **Definition Row = DT_QuestNPCs / MonsterGiver**. Use your actual row name if you named it differently.

Your class-based combat enemy must report its confirmed defeat before destruction: **Get Quest System → Report Enemy Defeated**, Enemy = the defeated battle-unit actor, Enemy ID = DeathBoar. An Actor Tag on an overworld EnemyNPC alone does not send that report. A reusable enemy-base Name variable QuestEnemyID can supply the ID; leave unrelated enemies at None. Report on confirmed death rather than generic destruction/cleanup.

Accept the quest before fighting. One matching kill makes it Ready to Deliver. Return and confirm for the reward. Talking again after completion does not pay again. See [MONSTER_QUEST_WALKTHROUGH.md](MONSTER_QUEST_WALKTHROUGH.md) for the complete flow and optional five-kill/five-minute setup.

## Fetch quests, timers and rewards

See [FETCH_QUESTS_AND_LOCALIZATION.md](FETCH_QUESTS_AND_LOCALIZATION.md) for exact item rows, possession versus consumption, pre-owned items and translations. Simple text supports one stage of Collect Item and/or Defeat Enemy objectives, optionally with a Deadline timer. Required Count controls the goal. Deadline 300 seconds means five minutes; acceptance starts it, completing the required objectives stops it, and expiry first fails the quest. Different quests have independent clocks.

Final gold/item delivery uses the checked default exchange when On Quest Delivery Requested has no custom handler. A bound handler owns the entire exchange and must confirm its receipt. Experience and intermediate Step Actions need the central handler. Save quest state together with inventory/gold through your existing Blueprint save UI.

## Editor commands you still need

For an existing working setup, edit your existing rows; rerunning setup is unnecessary.

| Output Log command | Purpose |
| --- | --- |
| Reditus.CreateQuestSystem | Create missing tables/NPC assets; seed only simple hunt/fetch examples on a new quest table. Preserve existing rows. |
| Reditus.CreateQuestNPC | Create missing reusable NPC/objective assets and definition tables. |
| Reditus.IntegrateQuestJournal | Back up and connect existing BP_QuestDetails data handlers, keeping its Designer artwork. |
| Reditus.ExportStoryIntegration | Export current dialogue/controller integration for debugging. |

Existing complex quest data and recruitment dialogue can continue to use Dialogue Assets mode. Their shared runtime remains necessary for progress, saved choices, sequences and delivery. See [TECHNICAL_REFERENCE.md](TECHNICAL_REFERENCE.md), [NPC outcomes](START_HERE_NPC_OUTCOMES.txt), [post-battle follow-ups](START_HERE_NPC_REMOVAL_AND_SCENES.md), and [markers](START_HERE_QUEST_MARKERS.txt).

## Verification

Run `python Tests/run_checks.py` with Python and a C++14 compiler for offline checks. The cleanup adds an actual editor-setup test for fresh simple seeds, preservation of edited/complex rows, missing item catalogs, Play and asset-path guards. The existing 21 quest/dialogue/reward/save/timer checks remain.

Unreal/UHT, live Blueprint events, rendering, real save/load and the Windows cleanup helper have not been executed here. Cold-build and test your hunt/fetch/recruitment flows in the project.
