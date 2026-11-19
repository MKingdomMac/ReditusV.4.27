# Monster hunt: offer, defeat, return and reward — 2.7.8

Use the same **Simple Quest Text** mode as a fetch quest. No separate dialogue assets or manually attached Start Quest / Turn In Quest events are needed. This walkthrough uses your existing `DT_Quests / Test_DefeatBoss` and `DT_QuestNPCs / MonsterQuestGiver`. Keep the monster's internal identifier `DeathBoar`; displayed names are your choice.

## 1. Install

Close Unreal. Merge the ZIP's Plugins folder into your project's Plugins folder, replacing included source files and retaining the rest of the existing plugins/assets. Build **Development Editor / Win64** with Unreal closed. Reopen after a successful build. Do not recreate working tables, NPC Blueprints or the quest journal.

Existing fetch rows keep their authored text and behavior. The old visible mode name Fetch Quest Text is now Simple Quest Text; its serialized enum value and property names are retained. Dialogue Assets mode and its authored trees remain available.

## 2. Set the objective and reward in DT_Quests

Open `Test_DefeatBoss`:

| Field | Value |
| --- | --- |
| Stages | One entry |
| Stage ID | Hunt |
| Objective ID | Keep your existing ID, such as Defeat Macy |
| Objective Kind | Defeat Enemy |
| Target ID | DeathBoar |
| Required Count | 1 |
| Optional | Off |
| Timer Mode | None |
| Choices / Actions / Routes | Empty |
| Next Stage ID | None |
| Turn In Items, at quest level | Empty |
| Rewards / Gold, at quest level | 500 |
| Rewards / Experience | 0 for this test |
| Rewards / Items | Empty for this test |

Descriptions are quest-journal text. The objective's Item Row is ignored for Defeat Enemy. It does not award the equipment selected there. Put any item rewards under the quest-level **Rewards / Items** instead. The required count can be greater than one for a repeatable enemy type.

## Optional: defeat five enemies within five minutes

Use the same quest and NPC text mode. In **DT_Quests → your hunt row → Stages → [0]**, set:

| Field | Value |
| --- | --- |
| Objectives → [0] → Kind | Defeat Enemy |
| Objectives → [0] → Target ID | DeathBoar, or your actual shared enemy ID |
| Objectives → [0] → Required Count | 5 |
| Timer → Mode | Deadline |
| Timer → Duration Seconds | 300 |
| Timer → Count Up | Off for a countdown |
| Timer → Show On Screen | On |
| Timer → Label | Defeat 5 enemies |

60 seconds displays as 1:00; 300 displays as 5:00. Count Up changes the displayed direction but keeps the same deadline duration. Leave Timer Mode None for an untimed hunt. Do not add a Timer Elapsed objective to a deadline hunt; keep the Defeat Enemy objective.

The clock starts when the player accepts the quest, not when a marker or offer appears. Each distinct confirmed matching enemy defeat adds one toward Required Count; duplicate reports for the same actor do not count twice. Enemy class or DataTable origin does not change the requirement: the actual defeated battle unit's report must carry the matching ID.

If the fifth kill is recorded before expiry, the quest becomes Ready to Deliver and its timer stops/hides. The player can return later, confirm the ready conversation and receive the configured reward. If time expires with fewer than five kills, it becomes Failed, the failure notice appears, late kills cannot finish it, and no reward is paid. Talking to the NPC then uses the shared failed response; it does not silently restart the quest. Use the existing explicit Retry Failed Quest action if your design needs retries.

Time follows the existing game-world clock: world pause, time dilation and Pause Quest Timers still apply; it does not run while the game is closed. For a limit that includes combat, do not enable Pause Quest Timers during the battle. The existing Blueprint save integration must save/restore quest progress with inventory; it preserves partial kill counts and the remaining deadline. Completion before expiry remains ready after loading.

This is a limit on finishing the stage's required objectives, not on returning to the giver. To time the return journey as a separate step, use a multi-stage quest in Dialogue Assets mode.

### Multiple timers at the same time

Each different active quest row keeps its own remaining time and HUD label. For example, a 300-second hunt and a 60-second task count down together. Finishing or failing one does not reset, stop or fail the other. Give each stage Timer a useful Label so the player knows which clock belongs to which objective.

Starting the same quest row again does not create another timer or reset it. Two NPCs referencing the same quest row share its one quest state/clock. Two active hunt quests matching the same enemy ID can both receive credit for the same confirmed kill. The global Pause Quest Timers flag and world pause affect all active clocks; queued/active quest sequences can pause them through their existing Pause Quest Timers setting.

## 3. Set the NPC's four text lines

Open `DT_QuestNPCs / MonsterQuestGiver`:

| Field | Example |
| --- | --- |
| Display Name | Your giver's name |
| Quest / Data Table | DT_Quests |
| Quest / Row Name | Test_DefeatBoss |
| Dialogue Mode | Simple Quest Text |
| Quest Text / Speaker Name | Your giver's name; empty uses Display Name |
| Quest Text / Offer | Will you defeat the monster in the cave? |
| Quest Text / Reminder | The monster is still out there. Come back when it is defeated. |
| Quest Text / Ready to Deliver | You defeated it! Are you ready to collect your reward? |
| Quest Text / Completed | Thank you for helping us. |

Under the advanced Quest Text fields, you can set **Hand In Choice** to “I've defeated it.” or “Claim reward.” The default “Here you go.” is useful for fetch quests. Accept Choice and Not Now Choice can also be customized; both remain available on the initial offer. A completed hunt with no required item possession or item hand-in cost has only the Hand In Choice on return. These are localizable Text fields. No additional Hunt Complete field is needed.

Keep the NPC's existing flipbooks. **Portrait Speaker Type** is optional; None still shows the written speaker name. You do not need Available/Active/Ready/Completed dialogue assets or Extra Rules in this mode. The shared flow starts the selected quest only when the player accepts, and turns it in only when they confirm the ready branch.

On your placed giver's **QuestNPC component**, set **Definition Row** to `DT_QuestNPCs / MonsterQuestGiver`. A selected row takes precedence over Inline Definition. Give any NPC needing persistent identity its own unique Persistent NPC ID. Leave Encounter ID empty for this talking giver; the target monster keeps its existing battle interaction.

## 4. Connect confirmed enemy death once

The quest system needs a report from your working combat death handler. It cannot infer a kill from a quest title, a map actor's label/tag, or an ordinary Destroy Actor call.

1. In **BP_EnemyUnitBase**, add a **Name** variable `QuestEnemyID`, default None, Instance Editable, if you have not already added it.
2. Set it to `DeathBoar` on the intended enemy child Blueprint or on the actual spawned battle unit during initialization. Leave unrelated enemies at None or their own IDs.
3. In the existing confirmed **On Unit Die** path, before destroying the dead unit, check QuestEnemyID is not None and call **Get Quest System → Report Enemy Defeated**.
4. Set **Enemy** to the actual defeated battle-unit actor and **Enemy ID** to its QuestEnemyID. Self is correct only inside that dying enemy. In a controller's handler use its dead-unit reference. Keep the existing death/animation/reward logic connected afterward.

For DataTable enemies, the ID must reach the actual spawned combat unit. An overworld actor's label or Actor Tag alone does not set it. This hook is reusable for all matching active hunt quests; you do not create a custom quest event for every monster. Repeated reports for the same enemy actor are deduplicated. Reports affect active objectives; defeating a target before accepting this hunt does not automatically complete a future hunt.

If this confirmed-death hook already exists and sends DeathBoar, keep it. Do not add a second parallel report path. Changing the NPC's text mode does not change battle launch or NPC removal.

## 5. Test the complete flow

Save your edited assets and restart Play after definition changes.

1. Talk and choose Not now: the quest remains available.
2. Talk and accept: the quest is added, then Reminder plays. A return visit before the kill also shows Reminder.
3. Defeat the matching target in normal combat: the quest becomes **Ready to Deliver**. The reward is not paid yet.
4. Return to the giver: **Ready to Deliver** text plays with only your Hand In Choice for this hunt. Select it to receive 500 gold, the completion/reward notice and the Completed line.
5. Talk again: Completed text, with no extra payout.
6. Save through your existing quest/inventory save integration, reload, and verify completed status and gold stay together.

If Reminder keeps playing after a kill, check that the confirmed-death report reached the subsystem with the exact Target ID. A Win Battle report is a different objective kind. If ready confirmation fails, check any bound **On Quest Delivery Requested** handler: a custom handler owns the exchange and must confirm its receipt. Without a bound handler, the default final exchange handles the existing gold/item maps; XP requires your central handler.

## Reuse it for another simple quest

Use one stage with required **Collect Item**, **Defeat Enemy**, or a mixture. Required item and defeat objectives must all be satisfied. Optional objectives do not block. Item possession alone cannot finish a mixed hunt. Turn In Items can add a live item cost even to a hunt; missing costs keep it on Reminder until they are owned.

Use **Dialogue Assets** for branching extortion, multiple stages, Wait timers, choices, Step Actions or other objective kinds. Optional Deadline timers are supported in Simple Quest Text. Simple Quest Text deliberately refuses those definitions instead of running only part of a complex quest. Both modes use the same quest progress, rewards, markers and save integration.

Offline logic checks pass; Unreal compilation/UHT, your live combat hook, rendering and real save/load still need testing in the project.
