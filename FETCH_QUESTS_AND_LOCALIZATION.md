# Simple quest text: fetch quests and monster hunts — 2.7.8

Use **Simple Quest Text** for a simple “bring/show an item” or “defeat a monster and return” quest. You fill text fields on the NPC row. One shared flow handles acceptance, objective checks, turn-in and completion in your current dialogue widget. No new dialogue asset or manually attached event is needed.

Use **Dialogue Assets** for complicated story branches, Wait timers, custom battle chains and multi-step conversations. Existing NPC rows default to this mode and keep their current trees. Changing modes retains the old asset references; it does not delete them.

## Install this update

1. Close Unreal. Merge the ZIP's Plugins folder into the project's Plugins folder, replacing included files and keeping the rest of the existing plugins/assets.
2. Rebuild **Development Editor / Win64** with Unreal closed; reopen after a successful build. This update adds reflected fields/classes, so use a cold build.
3. Open the existing **DT_QuestNPCs** and your fetch NPC's row. You do not need to recreate the NPC, tables or quest journal.

## The same flow for monster hunts

For your current hunt, follow [MONSTER_QUEST_WALKTHROUGH.md](MONSTER_QUEST_WALKTHROUGH.md). Use **DT_QuestNPCs / MonsterQuestGiver → Dialogue Mode = Simple Quest Text**, with Quest pointing to **DT_Quests / Test_DefeatBoss**. Fill Quest Text's same four lines for offering the hunt, reminding, confirming completion and thanking the player. The quest's **Defeat Enemy / Target ID DeathBoar / Required Count 1** objective controls readiness. No separate dialogue assets or manual Start Quest / Turn In Quest events are needed.

The combat **Report Enemy Defeated** hook must already report the actual dead battle unit and matching Enemy ID. This mode does not detect kills from labels, tags or destruction. Accepting starts the hunt; only recorded defeats on active objectives unlock turn-in. Returning and confirming pays the reward once. A hunt with only required Defeat Enemy objectives and no Turn In Items shows only Hand In Choice at this point. Its initial offer still has accept/Not now. Fetch/mixed-item quests and item-cost hand-ins retain the defer choice. Under advanced Quest Text fields, set Hand In Choice to “Claim reward.” if desired.

Simple Quest Text supports one stage of **Collect Item and/or Defeat Enemy** objectives, with at least one required objective. A mixture requires all required objectives. Optional tasks do not block. Optional Deadline timers are supported: set the stage Timer to Deadline, Duration Seconds to the limit (300 = five minutes), and Show On Screen on. The deadline starts on acceptance, stops when all required objectives are satisfied, and fails the quest if it expires first. See the timed example in the hunt walkthrough. Wait timers, routes, saved choices, Step Actions, multiple stages and other objective kinds need Dialogue Assets mode. Existing fetch rows retain their text and their stored enum value; only the visible mode/property labels change.

## Example: deliver one Letter for 500 gold

First configure **DT_Quests**. You can edit the existing test or make a new row, such as `Fetch_Letter`.

| DT_Quests field | Example value |
| --- | --- |
| Title | Deliver the letter |
| Description | Bring the letter to the courier. |
| Is Main Quest | Your choice; this controls main/side markers |
| Stages | One entry |
| Stage ID | DeliverLetter |
| Objectives | One required entry |
| Objective ID | HaveLetter |
| Objective Description | Bring the letter |
| Objective Kind | Collect Item |
| Item Row → Data Table | DT_KeyItems |
| Item Row → Row Name | Letter |
| Required Count | 1 |
| Optional | Off |
| Timer → Mode | None |
| Choices / Actions / Routes | Empty |
| Next Stage ID | None |
| Turn In Items | One entry: DT_KeyItems / Letter, Amount 1 |
| Rewards → Gold | 500 |
| Rewards → Experience | 0 for the built-in exchange |

A Collect Item objective checks possession; it does not consume stock. **Turn In Items** specifies what the NPC takes. Leave Turn In Items empty if the NPC only needs to see the item and the player keeps it. Final reward items go under **Rewards → Items**, each with an Item Row and Amount.

The existing item table determines the bag automatically. DT_UsableItems uses usableItems; DT_MiscItems uses miscItems; DT_KeyItems uses KeyItems; DT_Equipment uses the unequipped equipment map. There is no second Item Kind selector. These are the game's four existing catalogs under `/Game/TurnBasedJRPGTemplate/Blueprints/DataTable`; arbitrary new catalogs are not automatically mapped.

Now configure **DT_QuestNPCs → your fetch NPC row**:

| NPC field | Example value |
| --- | --- |
| Display Name | Courier |
| Quest → Data Table | DT_Quests |
| Quest → Row Name | Fetch_Letter (or the exact row you edited) |
| Dialogue Mode | Simple Quest Text |
| Quest Text → Speaker Name | Courier (empty uses Display Name) |
| Quest Text → Offer | Could you bring me the letter? I'll pay you for it. |
| Quest Text → Reminder | Have you found the letter yet? |
| Quest Text → Ready to Deliver | You've brought the letter. May I have it? |
| Quest Text → Completed | Thank you. Your payment is ready. |

Keep the placed NPC's **Definition Row** pointing to this NPC row. A selected table row takes precedence over Inline Definition. Fill its flipbooks as usual. The existing default dialogue widget is reused.

Under advanced Quest Text settings, you may change the choice labels (“I'll help.”, “Here you go.”, “Not now.”) and declined response. **Portrait Speaker Type** is an optional preset for one of your existing portraits. None is valid and does not hide the written name. Set the localizable **Speaker Name** text separately; no new enum member is needed for each NPC. Portrait sizing and the main dialogue panel are preserved. The speaker name is left-aligned. Choice cards have a light translucent navy background and a brighter selected row/text; the “YOUR RESPONSE” heading is removed.

You do not need Available/Active/Ready/Completed dialogue assets or Extra Rules for this mode; those fields are hidden. You do not manually attach Start Quest or Turn In Quest to its shared flow.

## What the player sees

| Situation when talking | Line / behavior |
| --- | --- |
| Not accepted; missing required items | Offer, with accept/decline choices |
| Accepts; still missing items | Starts quest and shows Reminder |
| Already owns the required items on the first interaction | Ready to Deliver immediately; confirming accepts and hands in during that conversation |
| Accepted; still missing an item or turn-in cost | Reminder |
| Accepted; owns all required items and costs | Ready to Deliver, with hand-in/decline choices |
| Chooses Not now | No item consumption, reward or automatic acceptance |
| Confirmed exchange succeeds | Completed line and quest-completed notice listing rewards received |
| Talks again after completion | Completed line; no repeated payment |
| An existing custom delivery is pending | Processing line; no second exchange |
| Start/exchange is rejected | Failure line; no success claim |

Inventory is checked before opening and again on confirmation. Selling/losing the item before hand-in prevents delivery. Ownership alone does not award a reward: the player confirms the exchange. A conversation already open is not forcibly replaced if another system changes inventory.

Offer and Ready contain only the lines you enter. Requirements and reward previews belong in quest details, not appended to NPC speech. Completion notices still show the final rewards after the receipt is confirmed. An immediate hand-in replaces stale Added/Updated messages for the same quest so its reward notice is not buried behind them. Other quests' messages remain queued.

## Gold and item payout

For either simple fetch or hunt text, when **On Quest Delivery Requested has no bound custom handler**, the built-in exchange updates the existing saved item maps and `gold` integer. It verifies the entire exchange first, applies costs and rewards together, then confirms the quest. A missing/wrong inventory field, insufficient stock or integer overflow fails before changing balances. Unexpected receipt rejection restores the previous balances and map membership.

If your project already binds **On Quest Delivery Requested**, that handler owns the complete exchange and must call **Confirm Quest Delivery** with its receipt. The built-in path does not also pay it. Do not grant the same reward through a legacy dialogue event as well.

**Experience rewards require the central handler**, because character XP/level/stat changes must use the game's party APIs. Without it, a fetch exchange containing XP fails before taking items or giving gold. The Reditus Quest Action / Turn In Quest event in authored Dialogue Assets now uses the same default gold/item exchange. Intermediate Step Actions and direct Blueprint Turn In Quest calls still require the central handler for nonempty deliveries. See README_FIRST.md, section 11.

The completed notice lists reward items and gold. This update does not call the old per-item obtain popup for every item or repair an inventory entry incorrectly added with the row name None. Ensure the real inventory-add operation uses the selected row name.

## Saving

Keep your existing new-quest save integration: save the exported quest state together with inventory/gold in the same save operation, and respect **Can Save Quest State**. Completed state prevents another payout after reload only when that progress is saved/restored with the player's inventory. This update does not rewrite your Blueprint save UI or add automatic saving.

## How translations work

These fields are **Text / FText**, not plain FString. Unreal uses each text's namespace/key identity to find the translation for the selected culture. It does not automatically translate the English you enter.

1. Enter source-language text directly in the NPC's Text fields. Quest titles/descriptions, item display names and speaker display names also use localizable Text. Preserve the editor-generated identities.
2. Configure a Game localization target in the Localization Dashboard. Gather from your NPC/quest/item tables and dialogue assets. Include this plugin's Source path for the native status, choice and reward labels.
3. Gather and export PO files. Supply reviewed translations through your translator or translation workflow, then import them.
4. Compile the localization resources and include the supported cultures when packaging. Select the player's culture and test the actual widget with longer text and the appropriate fonts/fallback fonts.

Translations live in the localization target's culture archives/resources. You do not need separate language columns in the NPC table or a different dialogue tree for each language. String Tables can supply shared lines if useful later; direct FText fields already support this workflow.

The runtime copies each FText into the shared conversation, preserving its localization identity. The native presenter reads the active simple-text conversation's Speaker Name as FText, including the Display Name fallback; it does not depend on the legacy portrait enum to show a name. The legacy node's FString SpeakerName is also populated for compatibility. Reward summaries remain separate FText in the quest information/notification UI. The existing game's other UI is not automatically translated by this patch.

## First integration test

After the cold build, set Speaker Name to Courier and Portrait Speaker Type to None. Verify the name appears, speech has no appended rewards, and each choice has a visible background with the selected text/row highlighted. Move between options by mouse and your usual controller/keyboard input. No heading should appear above the choices. Then test the Letter example with no custom payout handler and XP zero:

1. Start without Letter: talk, decline, talk again, accept; the quest should show a reminder.
2. Add Letter using your working inventory operation, then talk and choose Here you go: Letter is taken, gold rises by 500, Completed plays, and the reward notice appears.
3. Talk again and verify there is no further payout.
4. Test a fresh game already owning Letter before the first talk: Ready to Deliver should appear immediately.
5. Save after completion, reload the map/save, and verify completed status, inventory/gold and no repeated reward.

Offline checks cover shared flow decisions, actual native phase/action methods and the reflected exchange against typed stand-ins. Unreal compilation/UHT, live Blueprint/delegate execution, rendering, real saves and packaged localization still need project testing.

Official references:

- [UE4.27 Text Localization](https://dev.epicgames.com/documentation/en-us/unreal-engine/text-localization?application_version=4.27)
- [UE4.27 Data Driven Gameplay](https://dev.epicgames.com/documentation/en-us/unreal-engine/data-driven-gameplay-elements?application_version=4.27)
