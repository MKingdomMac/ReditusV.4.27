# Reditus Quest Editor — 2.8.2 / UE4.27.2

Create complex quests as graph assets, list them in a catalogue DataTable, and link existing DialoguePlugin conversations. Simple quests remain available through the current DT_Quests and Simple Quest Text workflow.

## Install

1. Close Unreal. Merge this package's Plugins folder into your project's Plugins folder. Replace included source files; keep the rest of DialoguePlugin and MinimapPlugin and all their assets.
2. Run the existing cleanup helper beside your .uproject if you still have the retired example source or stale plugin build output.
3. Generate Visual Studio project files. Build **Development Editor / Win64** with Unreal closed. New reflected types require a cold build.
4. Reopen after a successful build. No existing quest, NPC, dialogue or recruitment assets need recreation.

## First graph quest

1. Content Browser → right-click → **Reditus → Reditus Quest Graph**. Name it `Q_TestGraphHunt`.
2. Open it. A connected **Start Quest → Stage → Ready for Turn-in** graph is created. The stage initially has a placeholder Talk objective. The factory compiles/registers that draft when its settings are valid; it does not start it in the game.
3. Select the Stage. Under **Stage → Objectives → [0]**, change Kind to **Defeat Enemy**, Target ID to **DeathBoar**, Required Count to **1**, and give its Description your hunt text. Keep its generated Stage ID and Objective ID.
4. Click **Quest Settings**. Set the title, journal description, main/side flag and rewards in Settings. Use **Gold = 500**, Experience = 0 and no turn-in item costs for this smoke test.
5. Click **Compile & Register**, then Save. The editor's Save action compiles the quest and saves both its package and the catalogue package. A failed compile can still be saved as a draft; the runtime refuses an uncompiled graph rather than using its previous payload. Save All also persists other edited dialogue/NPC assets.
6. Click **Open Catalogue** to inspect `/Game/Reditus/Story/DT_QuestCatalog`. Your Quest ID is a row whose Graph Quest points to this graph asset. This table stores references, not kill counts or completed flags.

For a first test using your existing Simple Quest Text UI, point a test NPC's Quest handle to **DT_QuestCatalog / Q_TestGraphHunt**, choose **Simple Quest Text**, and fill its existing offer/reminder/ready/completed lines. A single-stage graph hunt is compatible with that mode. Accept it, report the confirmed DeathBoar defeat through your existing combat hook, return, and select the sole hand-in choice. Confirm the 500-gold reward happens once. Leave working recruitment NPC rows alone.

## Graph controls and branches

- Right-click the canvas → **Add Quest Stage**, or use the Add Stage button.
- Select a stage to edit its objectives, timer, choices, intermediate handoffs, marker overrides and sequences in Details. A stage can have several required objectives; they must all finish before its outgoing route is selected.
- Drag connections between pins. An output has one destination; several branches may join a later stage. Reconnecting an output replaces its old destination. Select stages and press Delete to remove them. Start and Ready for Turn-in are retained.
- Undo/redo use the normal editor transactions. Compile again after changing graph data, connections, or undoing changes. Moving nodes is cosmetic.
- To branch, add a Choice definition and its Options to the stage, plus a required **Choice** objective whose Target ID matches Choice ID and whose Required Count is 1. An output pin appears for every option. Connect every option output and the default **Next** output. The dialogue's accepted reply records that option through the existing **Reditus Quest Action → Record Choice** event. The graph does not automatically choose for the player.
- Choices are checked in the authored order; the first matching option route wins. Use separate stages for sequential decisions. Next is the fallback route. Links are compiled into the current quest runtime's Routes/NextStageID/FinishAfterStage fields.
- The graph must have one Start, at least one Stage and one Ready for Turn-in node. Every output must be connected; disconnected stages, cycles, duplicate/empty IDs and invalid objectives are rejected. Branch order is independent of canvas position. Quest loops are not supported by the current runtime.
- Ready for Turn-in finishes the objectives. It does not grant a reward. The player's explicit Turn In Quest action still uses the existing checked delivery transaction.

## Conversations and your existing recruitment events

Quest Settings has optional default conversations for Available, Ready to Deliver, Completed, Failed and Unavailable. Each Stage has an optional **Conversation** for the Active state at that stage. Assign an existing **UDialogue** asset through the asset picker.

The **Conversations** tab lists those links and opens the original dialogue editor. Double-clicking a stage with a conversation opens that same existing dialogue asset. This first version integrates that editor through asset links; it does not replace its graph implementation or create a second copy of a recruitment tree.

On an NPC using **Dialogue Assets** mode and this catalogue quest, selection precedence is:

1. Existing matching NPC **Extra Rules**.
2. The NPC's explicitly assigned dialogue slot for its current quest state.
3. This graph's matching default/state or active-stage conversation.
4. The NPC's existing Fallback Dialogue.

For a newly configured graph NPC to use graph conversations, leave the corresponding NPC state slots empty. Do not clear slots on already-working recruitment NPCs merely to install this editor.

An active-stage Conversation is selected when the player interacts with the quest NPC during that stage. It does not automatically interrupt play on entering a stage. It starts through the existing NPC/widget path, so the original dialogue events/conditions, widget and recruitment handling continue to own execution. The graph compiler never inserts, removes, executes, duplicates or rewrites dialogue events.

Add quest-specific events explicitly in the original dialogue editor:

| Purpose | Existing event/condition |
| --- | --- |
| Accept quest | Reditus Quest Action → Start Quest on the accepted reply |
| Record a branch | Reditus Quest Action → Record Choice with this stage's Choice ID/Option ID |
| Complete a Talk/Interact/Custom task | Reditus Quest Action → Report Objective Event with matching Kind/Target ID and a stable Unique Event ID |
| Pay a checked intermediate handoff | Reditus Quest Action → Request Step Action |
| Final reward | Reditus Quest Action → Turn In Quest on the confirmed hand-in reply |
| Gate a reply | Existing Reditus Quest State / Quest Choice / Quest Step Applied conditions |

Leave Quest Row Override entirely empty to inherit the talking NPC's selected catalogue quest, or explicitly choose its catalogue row. Both Data Table and Row Name must be empty to inherit. Sequence dialogues without an NPC context need an explicit quest handle.

If a recruitment conversation already adds the robot, it stays responsible for adding it. Use the existing **Confirm Quest Recruitment** only after confirmed success if a Recruit Unit objective needs that notification. Do not append a second Recruit action. Merely opening or closing the conversation never counts as recruitment.

## Catalogue, progress and saves

| Owner | Data |
| --- | --- |
| Graph asset | Editor graph, quest settings, linked conversations and checked compiled runtime definition |
| DT_QuestCatalog | Stable row ID and graph-asset reference, or a Simple Row reference |
| Existing DT_Quests | Existing simple/legacy quest definitions |
| Quest subsystem | Live state, stages, counters, saved choices, timers and pending deliveries |
| Existing Blueprint SaveGame integration | Persisted quest state together with inventory, gold, party and NPC state |

New graph assets register automatically in the default catalogue. Recompilation updates only the entry owned by that asset. An occupied Quest ID or an incompatible catalogue asset is preserved and reported. Registration does not edit NPC rows, existing quests, dialogue assets or saved progress.

To list an existing simple quest in the catalogue, add a row with **Kind = Simple Row**, point **Simple Quest** directly to its existing DT_Quests row, and leave Graph Quest empty. The resolver uses the original table/row as its runtime/save identity, so accepting through the catalogue cannot start a second copy of that same quest. Alias chains and mixed graph/simple references are rejected. The Blueprint helper **Find Quest In Catalog** resolves an ID to its usable handle.

Keep a registered graph's Quest ID and catalogue path stable after publishing saves. Asset renames can retain the registered ID. Duplicated graph assets clear their registration and need compilation with a unique Quest ID. Increment **Settings → Definition Version** when changing published stage/objective structure; compatibility/migration of shipped saves needs deliberate planning. No existing saves are rewritten by installation.

## Verification and current scope

Offline: `python Tests/run_checks.py` and `python Tests/run_editor_checks.py`.

After compiling in UE4.27, open Session Frontend → Automation and run **Reditus.QuestEditor.GraphCompileAndDialogueReferences**. This additional native test uses transient assets to exercise the actual graph schema/compiler, registration, invalidation, duplicate handling and preservation of an existing recruitment-event reference. It has been included but has not been executed here.

The first release includes graph authoring, branching stages, existing objective/timer/handoff/sequence fields, catalogue registration/resolution, linked dialogue editing, undo/redo and shared-runtime integration. It reuses the existing dialogue action/condition classes. It does not implement a new general-purpose AGS clone, automatically rewrite older dialogue trees, provide automatic conversation playback on stage entry, or migrate existing authored quests into graphs.

UnrealBuildTool/UHT, editor rendering/transactions, live Blueprint execution and real project save/load require verification in your project. Specifically test the new graph hunt and your existing recruitment accept/refuse branches before relying on this version.
