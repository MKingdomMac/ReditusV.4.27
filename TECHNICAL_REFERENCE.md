Current NPC follow-up authoring: see START_HERE_NPC_REMOVAL_AND_SCENES.md (2.5.0). NPC battle chains may repeat without a fixed limit; quest stage routes still move forward. Active chains block saves and defer automatic victory removal.

# Reditus quest system — technical reference — 2.7.2 / UE4.27

This is a new quest runtime. It does not use the old controller's activeQuests/completedQuests maps or AddQuest/UpdateQuest/DeliverQuest functions. Definitions live in **DT_Quests**; the game-instance subsystem owns progress; your current Blueprint save UI stores the exported quest data in its existing save object/file. The working NPC, minimap and dialogue presentation remain in place. Simple fetch and hunt NPCs author localized FText lines in DT_QuestNPCs (Simple Quest Text mode). The runtime generates one shared DialoguePlugin flow per conversation, using the current widget. Complex dialogue continues to use authored DialoguePlugin assets.

**2.7.7 cleanup:** Fresh-table bootstrap now seeds only simple hunt/fetch examples. The retired complex example generator and its command were removed. Existing stages, saved choices, branch routes and delivery/runtime APIs remain for existing authored assets and save compatibility. No existing DT_Quests rows are automatically deleted. The planned visual complex-quest editor is not part of this release.

**2.2.0 marker update:** read START_HERE_QUEST_MARKERS.txt. Merge both Plugins/ReditusQuestNPC and the additive Plugins/MinimapPlugin files, then cold-build. Existing working NPCs/journal do not need recreating. Main quests always use !; side quests always use ?, including turn-in. Available/active/ready markers default on; completed quests hide both displays. Classification and visibility are authored on DT_Quests, with an optional active-stage NPC visibility override. NPC icon/type overrides are retired and ignored. MinimapPlugin supplies default pixel artwork and creates a real screen-space overhead marker. Dialogue selection cannot change the attached quest's marker.

## Install and create the table

1. Close Unreal. Merge this package's Plugins folder into the project's Plugins folder, replacing included source files. Keep the rest of DialoguePlugin/MinimapPlugin and their assets.
2. Build Development Editor / Win64. Do not use Hot Reload for these reflected type changes. Reopen Unreal after a successful build.
3. In Output Log, run `Reditus.CreateQuestSystem`.
4. Run `Reditus.IntegrateQuestJournal` to connect the current BP_QuestDetails. It backs up the saved asset first, preserves its Designer tree/Back/menu input, and replaces six old quest-data event bodies. Review Compiler Results, then Save All.

The command creates `/Game/Reditus/Story/DT_Quests` with small test quests (intro/follow-up, 60-second wait, five-minute deadline, boss defeat, and Letter possession when the existing key-item table is present). It also ensures the reusable NPC/objective assets exist. It preserves existing authored rows/assets and refuses unrelated objects occupying its generated paths. No PowerShell script or project-module dependency is required.

The table row type is **Reditus Quest Definition**. Each row contains title/description, main/side classification, prerequisites, ordered stages, objectives, item turn-in costs, rewards and optional follow-up quest row handles.

## Quest row handles and giving quests anywhere

On a DT_QuestNPCs row, **Quest** is a Data Table Row Handle: select DT_Quests and a row. It is no longer a quest Blueprint class. Extra Rules can select dialogue against another quest row handle; the marker continues to represent the primary attached Quest row. Quest objective markers also select a quest row handle, Stage ID and Objective ID.

In any Blueprint, use **Get Quest System → Start Quest** and supply a quest row handle. Use it from a level script, cutscene event, interaction, NPC reply, robot recruitment result or other gameplay event. Start Quest returns false for duplicate active/completed quests. Prerequisites normally apply; Ignore Prerequisites is an explicit designer override.

Three places can start other quests automatically: an objective's Start Quests On Completion, a stage's Start Quests On Completion, and the quest row's Start Quests On Completion. Use the quest-level list when the next quest should start after turn-in. Repeated event notifications and loading a save do not replay these transitions. There is no automatic quest start just from displaying an NPC marker or reading a row.

## First simple quest test

Use MONSTER_QUEST_WALKTHROUGH.md for the existing one-enemy hunt, or FETCH_QUESTS_AND_LOCALIZATION.md for item possession/hand-in. New tables seed Test_DefeatBoss and, when the Letter item exists, Test_PossessLetter. Older tutorial rows already present in your table are preserved, but setup no longer adds them. Simple Quest Text handles acceptance and final turn-in; confirmed gameplay still reports defeats.

## Objective types and event identity

Objectives support Talk, Collect Item, Defeat Enemy, Reach Location, Recruit Unit, Interact, Custom, Timer Elapsed, Win Battle, Choice and Step Action. Choice/Step Action objectives resolve only through their dedicated nodes, rather than Report Quest Event. A stage advances when all its required objectives are complete; optional objectives do not block. Only the current stage receives events. Routes can choose a later stage based on saved choice IDs/options; otherwise stages remain ordered as before. One event can count toward multiple matching active quests. It does not spill into a newly activated stage or a newly started quest.

Report events only after the relevant gameplay succeeds: a confirmed enemy death, actual robot recruitment, confirmed interaction/choice, etc. A duplicate notification must reuse its Unique Event ID. Use stable authored names for one-off story actions; generate one ID per enemy instance/defeat and reuse it if that death reports more than once. Do not generate a fresh ID on every duplicate callback. Seen IDs are saved. Empty IDs are rejected.

For enemies, call **Report Enemy Defeated** from the existing confirmed On Unit Die path, before destroying the actor. Pass the enemy actor and its enemy ID (for example the DT_Enemies row name DeathBoar). The helper ignores subsequent death callbacks for that actor, including after another quest starts. Do not call it from Destroyed, battle cleanup, escape or despawn. It works for class enemies too: pass a stable matching ID instead of requiring a data-table enemy. Enemy spawn persistence remains your existing save system's responsibility.

Stage/Objectives IDs must be unique within their scope, nonempty and contain no slash. Row handles include the full table path plus row name, so equal quest row names in different tables do not collide. The adapter normalizes FName IDs for case-insensitive Unreal semantics.

The existing objective component has **Report Objective Reached**. Call it explicitly from your interaction/overlap success path and supply the event ID; it reports the configured current objective. Visibility alone never completes it. The generated marker actor has no automatic trigger volume.

## Your item, equipment and key-item row handles

Collect Item objectives have **Item Row** and Required Count. Turn In Items and reward Items contain Item Row and Amount. The existing item table selects the inventory category; authoring no longer exposes a second category selector. Select your existing table/row directly. No new item catalog or item Blueprint class is created.

| Item table | Existing controller inventory map |
| --- | --- |
| DT_UsableItems | usableItems |
| DT_MiscItems | miscItems |
| DT_KeyItems | KeyItems |
| DT_Equipment | equipment |

The row must exist in one of the four canonical existing item tables. Counts are read from that table's bag using the selected row name; equal names in different categories stay separate. Legacy authored Kind/ItemKind values and the old count-node Kind input are ignored. Serialized fields/pins are retained for compatibility; normalized delivery payloads still expose the computed Kind for the inventory handler. Equipment means unequipped bag stock; worn equipment is not consumed. Unrecognized item tables are rejected instead of falling back to another bag. Recognition uses the full canonical asset path under /Game/TurnBasedJRPGTemplate/Blueprints/DataTable; an unrelated same-named table cannot borrow stock.

Collect Item uses the current inventory quantity, not accumulated pickup notifications. Call **Refresh Quest Inventory** after additions/removals/consumption/load. NPC interaction and quest mutation also refresh current inventory. While playing, a 0.25-second inventory refresh keeps the UI current if an existing inventory call has not yet been explicitly hooked; explicit refresh is immediate. If an item is spent before final turn-in, the quest can return from Ready to Active. Collect Item objectives do not use Report Quest Event.

A collection objective by itself does not consume items. Add the appropriate row/quantity to **Turn In Items** when the quest must take them away.

## NPC battles, permanent removal and recruitment

Read **START_HERE_NPC_OUTCOMES.txt** for the exact settings, nodes and battle-success connections. Give each placed NPC a globally unique **Persistent NPC ID** on its QuestNPC component; keep that ID stable across saves. Duplicate IDs in the loaded world refuse component-driven persistent actions. IDs live on instances rather than a shared NPC table row.

**Reditus NPC Action** is an explicit dialogue-node event with Start Battle, Recruit and Remove Permanently. Use it only on the relevant chosen/success branch. Start Battle waits until the conversation and other close listeners finish before requesting an encounter. One On NPC Battle Requested handler routes the authored Encounter ID to your current encounter setup. Resolve NPC Battle must receive the same Request ID and the real result. Only Won reports a Win Battle objective and applies Remove NPC On Battle Victory; loss, escape and cancellation do not. Startup failures must resolve Cancelled. No encounter-spawn function is guessed or called automatically without that binding.

Recruit uses the exported existing **UnlockCharacterAtLevel(CharacterID, StartingLevel)** and **AddCharacterIDToActiveParty(CharacterID)** facade, preserving your initialization/equipment/exploration logic. Select an existing DT_PartyMembers row in Recruited Character. Ownership is verified in UnlockedPartyCharacterIDs before reporting success. The adapter validates function names/types before writing and never resets a previously owned character's level. With no active slot, the new unit stays in the recruited roster. Active Party Limit defaults to 4 and must match your game's actual cap. For your existing recruitment UI, keep its successful add/unlock path and append **Confirm Quest Recruitment** after actual success instead of recruiting again. Confirmation verifies ownership and records each character's recruitment once; closing the widget/Join intent alone never counts.

**Remove NPC Permanently** records the stable ID and removal reason; loaded/new map instances suppress themselves and hide their marker. A conversation already in progress finishes before removal. Actor destruction is deferred outside the dialogue's event-array execution. Ordinary Destroy Actor, level unload and despawn do not silently become permanent story removal. A queued removal rechecks restored state before destroying the actor.

Removed NPC IDs and recruitment receipts are included in the existing QuestState save struct. They do not replace your actual roster save. Both must be stored/restored by the current Blueprint save pipeline; no disk save occurs merely by removing/recruiting an NPC. Native actions and unresolved requested battles block Can Save Quest State. For legacy successful recruitment, make its existing roster update plus Confirm Quest Recruitment one synchronous chain, without a save between them. Resolve battle outcomes before the existing post-battle save. Loading does not replay joins or victories. Additive NPC fields default empty when reading an older quest save. New Game clears these flags; reload the map to respawn previously destroyed actors for a new/earlier game state.

Fresh setup no longer inserts recruitment/NPC-challenge tutorial rows. Existing recruitment and battle APIs remain available for your authored dialogue trees and saved progress; original robot assets are not automatically migrated or changed.

## Timers in seconds, displayed as minutes:seconds

Each stage has one Timer setting. Enter **60** for one minute or **300** for five minutes; decimals are allowed. Count Up changes only the display direction, not when the timer ends.

| Mode | Required objective | At time limit |
| --- | --- | --- |
| None | Any normal objective | No clock |
| Wait | At least one required Timer Elapsed, Required Count 1 | Completes Timer Elapsed; advances when all required objectives are done |
| Deadline | Reach, defeat, collect, talk, custom, etc. | Fails the quest if still incomplete |

For Timer Elapsed, Target ID is automatic. It cannot be completed by a fake Report Quest Event call. A deadline succeeds as soon as its required objectives finish, stops the clock, and advances to the next stage or becomes Ready To Deliver. A failed deadline fires its On Timeout Sequences and Start Quests On Timeout lists once. It does not grant success rewards. For a quest with no committed choices or step actions, Retry Failed Quest restarts from its first stage. Once choices/handoffs have been committed, retry preserves progress and restarts the failed stage clock without replaying prior rewards. NPCs have a Failed Dialogue slot and dialogue actions include Retry Failed Quest.

For a simple timed hunt, use the walkthrough's Defeat Enemy objective and Deadline timer. Older Test_Wait60/Test_Deadline300 rows remain only if already present; new setup does not generate them. Set Count Up true to display 0:00 toward 5:00 instead of 5:00 toward 0:00. The on-screen label is editable; Show On Screen hides a clock without stopping it. Get Quest Timers provides remaining/elapsed seconds, direction, pause state and formatted time for any custom HUD.

Timers use game-world seconds: they pause when the world is paused, follow world time dilation, and do not advance while the game is closed/loading. The subsystem's Pause Quest Timers lets your menu/battle flow pause them explicitly. Remaining time is saved/restored. Collect Item always checks present inventory: losing a final required item before turn-in revokes Ready; a deadline resumes its saved remaining time. Keep possession requirements together in the final stage if they must all remain true at turn-in; completed earlier stages are historical progress.

## Level Sequences from quest progress

Stages have **On Enter Sequences** and **On Complete Sequences**; quests have **On Complete Sequences**; deadlines also have **On Timeout Sequences**. Select a Level Sequence asset in each entry. Enter effects run once per stage entry, stage completion effects before the next stage's entry effects, and quest completion effects on confirmed turn-in. Loading does not replay them. Quest effects are queued, so they do not start simultaneously or interrupt an already-playing/paused LevelSequenceActor.

For a cinematic using map-specific actors, place/configure a LevelSequenceActor in that map and give it a unique actor tag. Put that tag in Placed Actor Tag; leave Sequence empty or select the same asset for validation. Turn Auto Play off on the placed actor. The placed actor keeps its authored playback settings/binding overrides: configure input locks and disable looping/Pause At End as appropriate. A missing/duplicate tag, mismatched asset or failed player creation logs a warning and broadcasts On Quest Sequence Finished with Completed false; it never falsely completes an objective.

For a sequence using spawnables or its existing asset bindings, leave Placed Actor Tag empty. A temporary player/actor is created with Restore State and optional movement/look locks, then cleaned up. Author camera cuts and event/dialogue tracks in Sequencer as usual. The runner publishes its actor through your existing controller's **Current Sequence ** member during playback so the approved dialogue presentation can detect it; the previous reference is restored afterward. Its dialogue layout and choice/Auto/Skip policy are unchanged. Map bindings still need to be authored in your actual sequences.

Pause Quest Timers defaults true per sequence. It pauses quest clocks while queued/playing. Set false if a timer should keep running. Optional Completion Target ID reports a **Custom** objective event only after natural sequence completion; stopping/aborting is not treated as success. You can use a Custom objective with that Target ID when a stage must wait for the cinematic. Call **Play Quest Level Sequence** from any Blueprint to queue one explicitly.

Save writes are blocked while a quest sequence is queued/active, so load cannot skip half-finished cinematic side effects. Mid-cinematic resume is not implemented. Level travel cancels current/old-map queued playback without reporting success. Core quest progress and timers survive travel. Start the new map's sequences from its normal progression hooks. Full-sequence skip remains a separate feature; no choice is automatically selected here.

## One central inventory and reward handler

Definitions can grant gold, experience and item/equipment/key-item rows. The subsystem does not guess how your party XP or inventory notifications should be applied.

Bind **On Quest Delivery Requested** once in the player controller or another persistent gameplay owner. It supplies a delivery struct containing Quest, Receipt ID, Items To Remove and Rewards. In one synchronous execution chain:

1. Recheck the listed item costs through Get Quest Item Count. If unavailable, cancel the receipt.
2. Remove costs and grant item rewards through your existing category-specific inventory functions, passing each Item Row's Row Name and Amount. Grant the listed gold and XP through your current party/stat methods.
3. After successful application, call **Confirm Quest Delivery** with that Quest and Receipt ID. On failure before applying anything, call **Cancel Quest Delivery**.

Your exported inventory APIs include AddUsableItemToInventory, AddMiscItemToInventory, AddKeyItem, AddEquipmentToInventory, RemoveMiscItem, RemoveKeyItem and RemoveEquipmentFromInventory. Use the appropriate existing usable-item removal path. No legacy quest function is involved.

Do not add latent nodes, an asynchronous save, or duplicate grant handlers between applying the delivery and confirming it. A second request for the same pending receipt is rejected. Confirmed quests cannot deliver twice. If no handler is bound for a nonempty delivery, Turn In Quest returns false and logs the missing connection; it does not silently complete the quest or discard rewards. Empty deliveries complete directly.

Normal save writes are blocked while delivery is pending so they cannot capture a half-applied trade. Can Save Quest State exposes this guard. Get Pending Deliveries is available for reconciling pending final-turn-in transactions; importing does not automatically execute them. Direct unsafe exports with pending intermediate step deliveries are rejected on import. Always obey Can Save Quest State before an explicit export. Your handler must reconcile or cancel a recovered pending transaction before saving again.

## Keep the Blueprint save UI and the same save file

Your export contains **BP_SaveUI**, its **currentSaveGame** reference, and **BP_JRPGSaveGame**, including its LoadJRPGCharacter function. Keep that pipeline.

1. Add one variable to BP_JRPGSaveGame called **QuestState**, type **Reditus Quest Save Data** (struct). Enable SaveGame on the variable.
2. In the actual save path, before the save object is written to its slot, call **Get Quest System → Write Quest State To Save**, passing the same save object. Branch on success. If false, stop that save attempt and clear the UI's saving state; do not write stale quest data.
3. After loading that save object and restoring inventory/player data, call **Read Quest State From Save** on the same object. Branch on success; keep/report its Error text if it fails. Refresh Quest Inventory after restoring inventory. Do not reset quest state again on level BeginPlay.
4. Call **Reset Quest State For New Game** only for New Game. The subsystem survives level transitions.

Export Quest State / Import Quest State are also available if your UI prefers explicit struct setters. The normal convenience save node rejects pending deliveries; a manual struct export must respect Can Save Quest State too.

The save contains stable quest row references, version, current/visited stage IDs, objective counts, remaining timer seconds, consumed event IDs, choices with authored option/value and confirmed step action receipts; no live NPC/actor references. A bad save import validates before replacing current progress. Load does not grant rewards, restart quests or replay completion chains. New quests are not migrated from the old quest maps. Old saves have no new quest progress unless explicitly authored into the new format.

Keep stage/objective IDs stable. Increment Definition Version when publishing structural changes that need a save migration; mismatches are rejected rather than resetting progress silently. Restart Play after editing definitions; a session's registered definitions are cached. Cook the quest/item tables by referencing them from your saved NPC/objective assets or Blueprint defaults.

## Dialogue, minimap and quest menu

Keep using your working dialogue assets. Reditus Quest Action now uses the new system: Start Quest, Turn In Quest, Report Objective Event, Record Choice or Request Step Action. Leave Quest Row Override empty to use the NPC's selected quest; set it explicitly when granting another quest. Put Start Quest on the accepted choice branch. Reditus Quest State can check both state and an optional stage ID. The old integer Advance Quest option is retired; report the actual objective event instead.

NPC state slots choose Available/Active/Ready/Completed dialogue from the new state. Minimap and overhead NPC markers use DT_Quests classification/visibility and the current stage visibility rule. Destination markers use the matching active objective. MinimapPlugin owns the shared !/? artwork and style. Dialogue appearance and the opening-flash fix remain byte-identical to approved 1.1.5. Dialogue-table authoring/variants remain later work.

Your **existing BP_QuestDetails** remains the journal. The integration command connects ShowMainQuests, Show Side Quests, ShowCompletedQuest, HighlightSelectedQuest, ChangeHighlightedQuest and ShowProgress to the journal renderer and preserves the Designer and menu/input flow. Runtime fills the existing BP_QuestButton rows, QuestName, QuestDescription, QuestBar and ProgressDetails. It reads original quest classes and progress from the owning player's activeQuests and completedQuests alongside the subsystem's started quests. The journal reads both sources without writing progress or granting rewards. Each quest uses its own Is Main Quest setting. Saved graph decisions appear in the description. Only reachable graph branches count toward progress. Finished includes timed-out graph quests marked as failed. The command verifies the graph and layout, restores the original connections if compilation fails, and leaves saving to the user. Repeat runs detect the saved integration. Its file backup under Saved/ReditusQuestNPC/Backups contains the last saved widget asset.

Added/updated/completed/failed notices appear automatically in a non-interactive navy-and-gold HUD, using the pixel font referenced by your current quest UI (with a fallback if that font is unavailable). It does not change input mode or take focus. Repeated queued progress updates for a quest are coalesced. **On Quest Notification** also exposes the payload if you prefer another notification widget; set Show Quest Notifications false to suppress the default notices. Clocks share the same HUD and remain available. The journal reads the controller's current lists when a page opens. On Quest Changed and On Quest State Restored refresh the open journal for graph quest changes.

Keep the existing story Blueprint calls for quests that still use the original controller. The subsystem owns graph quest progress and rewards. Both systems show their entries in the same journal. Deprecated compatibility symbols still return false. The integration only edits the journal widget Blueprint.

## Verification and first build

Run `python3 Tests/run_checks.py` with Python and a C++14 compiler for the offline checks. These compile the actual quest state machine and the actual item-row lookup (typed reflection stand-ins), plus the existing dialogue binding/playback tests.

**Unreal/UHT compilation and gameplay have not been run here.** Cold build first, then test the sample quest, save/reload, NPC start/decline/turn-in, duplicate enemy callbacks, inventory removal before turn-in, main/side minimap markers, the central delivery handler, and your existing dialogue/sequence/recruitment flows. BP save and delivery connections are documented work still required in your installed project; the ZIP cannot modify its installed .uasset graphs from this workspace.

## 2.7.8 shared simple quest text mode

DialogueAssets is the serialized default, preserving existing rows. The existing FetchQuestText serialized value is displayed as Simple Quest Text, and the existing FetchQuestText property is displayed as Quest Text. Neither IDs nor authored text values are migrated. It requires one stage containing CollectItem and/or DefeatEnemy objectives, at least one required objective, no choice/route/step actions or NextStageID. Timer mode may be None or Deadline; Wait remains in authored dialogue mode. The NPC definition owns FText lines; DT_Quests owns objectives, optional item consumption and final rewards. Internal conditions/events drive the shared flow; authors do not add those objects manually. Required items and TurnInItems are checked before displaying the ready branch and again before confirmation. Already-owned items can enter the hand-in branch while an item-only quest is still Available; only confirming accepts and turns it in. Any required DefeatEnemy objective requires the runtime Ready state, so no-item hunts and mixed item/hunt quests cannot infer completion from inventory. Defeat callbacks still use ReportEnemyDefeated on the actual battle unit. Optional objectives do not block; live TurnInItems costs still do. The existing runtime owns the deadline: acceptance starts it, Active ticks down, Ready stops/hides it, and Failed blocks late defeats and delivery. Finishing required objectives in time does not require returning before expiry. Live item possession can revoke Ready on a mixed/item quest and resume its saved remaining deadline, as before. Final TurnInItems costs do not extend the stage's timed objectives. No timer/save/UI implementation is duplicated. Concurrent different quest rows have independent deadlines and labeled HUD entries; one quest becoming Ready/Failed does not alter the others. Starting an existing quest again cannot reset its timer. NPCs referring to the same row share one record/clock. Completed state takes priority and cannot pay again.

The shared TurnInDialogueQuest native entry enables the built-in final item/gold exchange for generated simple text and authored Turn In Quest dialogue events. TurnInFetchQuest remains a forwarding compatibility wrapper. Direct Blueprint TurnInQuest and intermediate Step Actions retain their central-handler requirements for nonempty deliveries. A bound OnQuestDeliveryRequested handler always takes priority and must confirm its own receipt. The permission is reset before external callbacks. Without a handler, exact catalog identity resolves the existing FName-to-int32 maps, all balances are preflighted, costs/rewards commit synchronously, and confirmation completes the quest. Experience is refused before any write when no custom handler exists. Unexpected confirmation rejection rolls back both values and map membership. Custom inventory side effects/XP remain the project's central handler's responsibility.

The four main lines and advanced labels are FText properties in the saved NPC table, copied as FText into a transient UDialogue. Native status/choice/reward formats use NSLOCTEXT. Original authored trees are never edited. Existing save hooks persist completed state and inventory/gold together; neither the transient flow nor localized text is quest-progress save data. Existing save hooks remain necessary.

## 2.7.2 speech, names and choice readability

FetchQuestText.SpeakerName is a localizable FText, with empty falling back to NPC DisplayName. The generated Dialogue.Name retains that FText and legacy node SpeakerName strings are populated for compatibility. ReadFetchSpeakerName supplies it only to this component's active fetch widget; normal authored dialogue names are left to their existing flow. The presenter refreshes the original name label before name-frame visibility is calculated, so portrait NodeType None cannot suppress a nonempty fetch name.

The builder no longer requests/appends a reward summary to Offer or HandIn. Quest journal previews and confirmed reward notices remain unchanged. Choice cards use a stronger translucent navy backdrop and a separate selected-row style, without changing main dialogue/portrait layout or cinematic controls. The redundant heading is removed. Labels remain fully readable while source input is temporarily gated; the existing ChooseReply/SelectReply handlers still enforce InputAllowed, log state, live reply identity and focus before an action.


## Hunt return choices

The shared generated flow accepts a per-definition AllowHandInDecline policy. Only the Ready NPC node's reply links change. A definition with at least one required DefeatEnemy objective, no required non-defeat objective and no TurnInItems has just the HandIn action reply. Optional item objectives and stale unused ItemRow selections on a DefeatEnemy task do not reintroduce a decline reply. Required CollectItem or TurnInItems keeps deferral available. Offer always retains Accept and NotNow, regardless of the initial phase or timer. Node IDs, FText identities, event/condition classes, turn-in transaction, saved data and authored dialogue remain unchanged. One visible hand-in reply still requires the player's selection; building/displaying the flow does not execute it.
