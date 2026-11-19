# NPC removal and repeatable battle follow-ups — 2.5.1

A story can run battle → sequence → dialogue → another battle, repeating as many times as authored. Sequence and dialogue are optional. Each encounter gets a fresh request ID. The working central battle-start/result graph remains the adapter to the game's real combat system.

This package changes source, not installed .uasset graphs or the Blueprint save UI. Unreal compilation/gameplay still require project verification.

## Install once

Close Unreal. Merge the ZIP's Plugins folder into the project's Plugins folder, keeping the rest of DialoguePlugin/MinimapPlugin and their assets. Rebuild **Development Editor / Win64** with Unreal closed, then reopen. Do not use Hot Reload for these reflected struct/event changes. Existing NPC rows gain empty follow-up fields; no table regeneration or PowerShell script is required.

## If the actor disappears before Post Dialogue

Version 2.5.1 fixes a queued-removal race: a removal callback already scheduled for the next tick now checks again for pending combat, dialogue or follow-up work. It cancels the premature destruction, restores the prior actor visibility/collision and waits. Linked EnemyNPC sources use the same guard. The order is **confirmed victory → Explore → Post Sequence/Post Dialogue → successful endpoint → permanent physical removal**.

**Finish Follow Up marks the end of an already-open post dialogue; it does not launch it.** If Post Dialogue never opens, adding that event alone cannot fix startup.

Keep **No Destroy = true** on the central EnemyNPC StartBattle call. Remove any separate raw Destroy Actor/Set Life Span operation for these story actors from the old Battle Over path; use the one quest removal command. Unreal destroys a component with its owning actor, so the plugin cannot continue its post dialogue after external code destroys that owner. A warning now identifies destruction during a pending battle/follow-up.

First project check after updating: keep TestNpc's Post Dialogue = AmbushDialogue1, Next Encounter ID empty and Remove NPC On Battle Victory on. Win the fight. AmbushDialogue1 should open while the story NPC is still alive, then Finish Follow Up on its successful final node permits the final removal. If it does not open, inspect the real Resolve NPC Battle call/result and the new warning; offline tests cannot establish the installed Blueprint path.

## Keep the three actors' roles clear

| Actor | Role | Settings |
|---|---|---|
| BP_QuestNPC_New | Talking/story NPC | QuestNPC component: stable unique Persistent NPC ID, e.g. `TestNpc_01`; Definition Row = DT_QuestNPCs/TestNpc. |
| EnemyNPC5 | Supplies the real encounter | Actor Tag = `DeathBoar`; existing Enemy List or Table Enemy Spawns and Battle reference. |
| BP_InteractionBattle2 | Shared battle machinery | Keep this actor. Permanent story removal never targets it. |

Keep the pasted EnemyNPC **StartBattle → No Destroy = true**. Class enemies and DataTable enemies still use that existing function. The NPC's Encounter ID selects the uniquely tagged overworld encounter source; it is not automatically an enemy class or DT_Enemies lookup.

To remove the separate EnemyNPC with the talking NPC, add **Reditus NPC Linked Actor** to EnemyNPC5 and set **Linked NPC ID = TestNpc_01**. Link other encounter sources/visuals only when they belong to this same story character. Never attach this removal link to the shared battle actor.

Each independent QuestNPC needs its own globally unique, stable Persistent NPC ID. Several linked actors may intentionally share that one ID. A row shared by several talking NPC instances is configuration, not their identity.

## Post-battle settings on DT_QuestNPCs

Open **DT_QuestNPCs → TestNpc → Battle → Post Battle Follow Up**.

| Field | Purpose |
|---|---|
| Post Sequence | Optional Level Sequence after winning and after the combat/results UI returns to Explore. |
| Placed Sequence Actor Tag | Optional advanced setting: use exactly one placed LevelSequenceActor with this Actor Tag to retain its bindings. If Post Sequence is also supplied, it must match that actor's sequence. |
| Post Dialogue | Optional existing DialoguePlugin asset; uses the current approved dialogue widget and its events/conditions. |
| Next Encounter ID | Optional automatic next fight after the sequence/dialogue finishes successfully. Empty means no automatic next fight. |

With both presentation fields set, sequence runs first, then dialogue. Either field can stand alone. With all fields empty, the existing immediate result behavior remains.

**Post Battle Follow Up is the default for Won only.** For different behavior on a specific encounter/result, add an **Encounter Follow Ups** entry: Encounter ID, Result, Follow Up settings. Exact matches override the entire default. One empty matching override ends the chain without default presentation. Never duplicate the same Encounter ID + Result; ambiguity stops the follow-up without automatic removal or continuation. Lost, Escaped and Cancelled have no default victory follow-up/removal, but can have explicit result-specific follow-ups.

For cinematics, use a non-looping sequence that finishes naturally. A stopped/cancelled scene aborts this follow-up and does not automatically start the next fight or remove the NPC. Other playing/paused sequences and queued quest scenes finish first. Missing/ambiguous placed-actor tags fail rather than choosing an arbitrary scene.

## Two-battle example, then extend it

1. Keep TestNpc's **Encounter ID = DeathBoar**. On its introductory dialogue's Fight branch, use **Reditus NPC Action → Start Battle**. Empty Encounter ID Override uses DeathBoar.
2. On TestNpc's default **Post Battle Follow Up**, select a post sequence and/or post dialogue. Set **Next Encounter ID = GuardReinforcements** for automatic continuation.
3. Give another existing EnemyNPC source the Actor Tag `GuardReinforcements`, configure its Enemy List/Table Enemy Spawns and Battle reference. Each encounter tag must identify exactly one valid source. The sources may share BP_InteractionBattle2.
4. Add **Encounter Follow Ups [0]**: Encounter ID `GuardReinforcements`, Result **Won**, your final Post Dialogue or Post Sequence, **Next Encounter ID empty**.
5. If there is a Post Dialogue, put **Reditus NPC Action → Finish Follow Up** on every successful terminal branch. This marks the intended story endpoint; the normal widget close then finishes the flow. Closing/disappearing without that marker aborts automatic continuation/removal.
6. Enable **Remove NPC On Battle Victory** if this character should permanently leave after the final successful chain end. Link the relevant overworld source actors to TestNpc_01 as described above.

Add another encounter/rule to extend the chain. Next Encounter ID can also refer to an earlier encounter, including the same encounter, for a deliberate repeating fight. There is no fixed chain-length counter or recursive battle call. A configured automatic loop continues until your authored dialogue/encounter rules stop or redirect it; do not accidentally point a final automatic step back to itself.

## Let the player choose the next battle

For a choice-controlled transition, leave that step's **Next Encounter ID empty**. In its Post Dialogue:

- Fight branch: **Reditus NPC Action → Start Battle**, **Encounter ID Override = the chosen encounter tag**. This queues the fight and approves the chosen continuation; it starts after the dialogue closes normally.
- Peaceful/final branch: perform the desired quest/reward/recruitment actions, then **Finish Follow Up** at the actual successful endpoint.
- Several fight branches can supply different encounter overrides. Existing conditions/choices decide which node runs. Record choices through the quest system when they must survive saves.

An explicitly queued next battle takes priority over the row's automatic Next Encounter ID. **Finish Follow Up** alone does not clear an automatic Next Encounter ID, so leave that field empty when a choice should be able to end the chain.

Post Dialogue keeps the source NPCActor and selected quest context. Existing start/turn-in/report/record-choice/step-action/recruit/remove events and conditions remain available. A Finish Follow Up marker does not grant rewards itself or prove an asynchronous inventory delivery succeeded; continue from your existing confirmed delivery/state path. Mandatory choices still require player selection.

## One permanent-removal command

**Get Quest System → Remove NPC Permanently**, with Persistent NPC ID and reason, records one saved removal and notifies the talking NPC and linked actors. Dialogue **Remove Permanently**, the victory checkbox and confirmed recruitment all call this same operation. Old removal node names remain hidden compatibility wrappers.

| Intent | Setup |
|---|---|
| Leave immediately after one victory | Remove NPC On Battle Victory on; no follow-up configured. |
| Leave after the final battle/sequence/dialogue | Checkbox on; configure the follow-up chain. Intermediate victories do not remove the actors. |
| Keep the NPC after the chain | Checkbox off; finish normally with no next encounter. |
| Remove at a specific dialogue/sequence event | Checkbox off; call the one Remove Permanently command at that authored point. |

Explicit permanent removal prevents another queued fight for that NPC. Physical destruction waits until the active conversation/follow-up releases the actor, so a final node's event array can finish. Cancelling a follow-up does not undo an explicit removal/recruitment/reward that already succeeded; it only stops automatic next-fight/removal decisions.

After final removal, both the talking NPC and linked source suppress themselves on map reload from the saved record. A bare Destroy Actor does not save story removal. The shared battle actor remains available for other encounters.

## Keep the existing central battle graph

Bind **On NPC Battle Requested** once in the active player controller. Capture Request ID, source and battle actor; start the existing EnemyNPC encounter. When the real battle ends, unbind that result listener, reset the source's isStartingBattle flag, clear captured controller references, then call **Resolve NPC Battle** with the captured ID and actual result. Startup failure resolves Cancelled. Duplicate/old-world results cannot advance the chain twice.

The plugin temporarily holds the resolved source's isStartingBattle flag during follow-up presentation, then restores it before the next request. This prevents that source from retriggering during the post dialogue/scene. Do not independently destroy it on Battle Over.

You can retain custom hooks:

| Event | Use |
|---|---|
| On NPC Battle Resolved (Request, Result) | Raw accepted combat result, once. The configured follow-up may still be waiting for Explore. |
| On NPC Follow Up Finished (NPC ID, Encounter ID, Completed) | Configured follow-up finished or aborted; a queued next battle submits on a later tick. Completed describes presentation flow, not the battle result. |

Do not also launch the same dialogue/scene/next battle from an old Level Blueprint Battle Over binding. Avoid placing the same scene in both an NPC follow-up and a quest stage. Win Battle objectives and quest stage sequences remain available for saved quest progression; quest stages still move forward and reward/choice receipts are not reset by repeated combat.

## Save/load and practical limits

Store quest state in the same existing Blueprint save operation as inventory/party:

1. BP_JRPGSaveGame: **QuestState**, type Reditus Quest Save Data, SaveGame enabled.
2. Before writing that same save object: **Write Quest State To Save**; branch on success. False means stop this save attempt, not write stale quest data.
3. On load: restore inventory/party, then **Read Quest State From Save**; check success/error and refresh inventory. Do not reset progress on ordinary map BeginPlay.

Pending battles, queued next encounters, active follow-ups, queued/playing quest sequences and pending deliveries block safe saves. Mid-chain/cinematic save-resume is not implemented. Save after the final flow settles; removal alone does not write a disk slot. If the old battle autosave tries during a chain, it must respect the false save result and save again at a safe endpoint.

Reset/New Game, teardown and world changes cancel outstanding follow-ups without granting automatic completion/removal. Reload the map when restoring a previous/new game's actors. Runtime-spawned NPCs need stable IDs supplied by their spawner/save data.

Offline checks exercise the real follow-up driver with stand-ins, including 10,000 repeated cycles. They do not execute UE4.27/UHT, live Blueprint delegates, rendering or disk saves. Cold build first; then verify one battle with a post dialogue, two battles in sequence, peaceful/fight choices, loss/escape, explicit removal and save/reload in your project.
