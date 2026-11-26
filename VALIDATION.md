# Version 2.9.13 — quest HUD during cutscene handoff

- Quest delegates still broadcast during quest changes. The default HUD queues those notices and attaches during the subsystem tick in a hit-test-invisible Slate layer. The countdown shares that layer.
- Standalone checks exercise deferred attachment, late viewport creation, replacement across maps, timer-only display and cleanup. The timed destination check also verifies that a dialogue hand-in completes the quest and pays once after reaching the destination.
- Native UE4.27 compilation and a play test with the compiled quest at the skip handoff are pending.

# Version 2.9.12 — deferred playback and input handoff

- A configured sequence remains registered while waiting to play. Starting playback shows the prompt before dialogue, and removal from the viewport during playback is retried. Existing Allow Skip and finish delegate settings control both immediate and deferred playback.
- Skip still hands off to the authored event once. The next game tick restores exploration input after that handoff, including the controller's Blueprint input gate and any movement/look input lock attributable to the old scene. Release of the old lock is guarded by the local controller, world, game state and next scene or dialogue.
- Standalone checks model viewport attachment, the actual imported playback order, deferred focus restoration, a new handoff input lock, and already blocked movement. UE4.27 compilation and in-editor testing require the game project.

# Version 2.9.11 — immediate Skip prompt and game input recovery

- The playback start delegates show a non-focusable prompt for a registered, skippable sequence. Release before the one-second hold completes to restore the prompt. Finish, stop and world changes clear it. Protected scenes still display a timed message on a skip attempt.
- Skip restores the local exploration controller's game input mode and enables controller input after the old scene stops. A different sequence, visible replacement dialogue or changed world guards against restoring input over the new presentation. The existing authored finish bindings still run once.
- The standalone checks compile the updated sequence methods with stand-ins and verify prompt and input transitions. Rebuild and play a same-map sequence in UE4.27 to verify the controller and input focus in the actual editor.

# Version 2.9.10 — level-sequence finish event on Skip

- The exported controller wires `EventOnFinished` to the sequence player's `OnFinished` and to the skip configuration. Skip uses the original finish bindings after stopping the player, including when a stop listener replaces the live bindings. A scene with a distinct skip-only callback uses that callback.
- Standalone checks exercise both paths, repeated holds, protected scenes and a stop listener that changes the next finish bindings. Rebuild and play both finish paths in UE4.27 to verify the actual Blueprint callback.

# Version 2.9.5 — timed destination and cinematic pause

- Both the quest clock and its HUD pause status use the same sequence check. It recognizes external Level Sequence players and pauses between dialogue cues. Quest-owned scenes still use their authored pause setting.
- The existing objective component can turn in a completed reach objective through the existing checked reward transaction. Arrival after expiry, repeated overlaps after completion and pending delivery receipts cannot pay again. A cancelled delivery can be retried at the same destination.
- Standalone checks cover the real pause and arrival adapters, the quest runtime, the existing delivery transaction and the editor graph. Native UE4.27 compilation, actual overlaps and Sequencer playback still require a project run.

# Version 2.9.4 — cinematic connection and continuation

- The current PlayLevelSequence export sets Current Sequence before Configure. It passes Allow Skip and On Skipped but has an unwired Sequence Actor input. The configure node resolves that exact stored actor when called from the local player controller. The explicit actor input still supports other callers.
- The Director export creates DemoDialogueWidget and pauses its own player. The plugin observes that player and the widget's Removed dispatcher. It checks the pause and ownership on the next game tick, including closures from the legacy reply timer. A displayed Play Level Sequence cue uses the same player. Manual playback can resume it first.
- Auto waits 0.25 seconds after voice playback and 1–4 seconds for a silent line. Its preference follows the active player and world. Native dialogue uses a hidden cursor and scene registration hides the gameplay cursor.
- Existing offline fixtures execute the runtime bodies for unwired actor resolution, the two skip policies, end handoff, deferred closure, authored cues, manual Play, replaced widgets, and the Auto delays. The previously shipped quest and editor checks remain in the suite.
- Windows UE4.27 compilation and live Blueprint, audio, cursor and Sequencer behavior need in-project verification.

# Version 2.9.3 — cutscene continuation and protected skip

- Removed the cinematic Shift/RT fallback into per-line fast-forward. The native presenter consumes cinematic skip input and calls the registered sequence controller directly. Unregistered scenes show the blocked notice. Configured protected scenes also consume held repeats and show the notice.
- Added a one-second hold with gold progress fill and percentage. The existing notice widget displays progress or the blocked message without taking focus. Release, focus loss and map travel cancel progress. Dialogue and voice remain until a complete hold.
- A complete skip dismisses the current presentation, stops its Sound2D voice, jumps to the final visible frame with UE4.27 EUpdatePositionMethod::Jump, stops at that position and invokes the explicitly supplied handoff once. The exclusive upper bound is avoided to prevent an additional natural-finish callback. Intervening timeline cues are not replayed. Required mission setup remains the shared handoff's responsibility.
- Auto and cinematic confirm use the existing SimulateNextClicked graph. A narrowly guarded adapter resumes a captured paused player only after the old presentation closes/hides and the player, controller, world and dialogue ownership remain compatible. Visible new lines, replacement widgets, explicit stops, scene replacement and map travel are guarded. Auto preference is transient per sequence and carries across new dialogue widgets.
- Executed checks compile real lookup/continuation methods, input routing, skip control and notice construction/update methods against typed stand-ins. Regressions cover blocked/unregistered skip input, partial holds, unchanged dialogue/timeline before completion, final-frame seeking, one handoff, paused sequences, Auto scope, cancellation and notice text/fill visibility. The notice builder also uses strict shadow diagnostics for the inherited UWidget::Slot issue.
- Existing runtime, editor, save, quest and recruitment checks pass. Native UE4.27/UHT compilation, delegate dispatch, Slate rendering and live project execution have not been run here.

# Version 2.9.2 — cinematic focus, voice timing and prompt artwork

- The exported dialogue Blueprint sets its own bIsFocusable false. The native panel now enables its own focus and takes it after first-frame setup, then restores its own focus when handling clicks. The existing source-ownership checks continue to guard later screens.
- The actual focus, routing, Auto and Log functions are compiled against typed stand-ins. Regression coverage includes a non-focusable Blueprint parent, focus transfer to the native child, Auto key handling, Log during protected cinematic dialogue and refusal to steal focus from the next screen.
- Auto tracks whether the current line's voice has played. It waits for that voice to stop and then applies a 0.65-second pause. Unvoiced lines use reading time. Added checks cover long voiced lines, consecutive unvoiced lines, late Auto selection and protected continue gates.
- The actual prompt construction helpers are compiled with strict shadow checks. They verify translucent backing, decorative strokes, caption retention and hit-test-invisible ornamentation.
- Native UE4.27 build, audio-component behavior and live Slate focus/rendering still require verification in the project.

# Version 2.9.1 — skip-notice build correction

- Renamed the local canvas-slot pointer in UReditusSequenceSkipNotice::RebuildWidget to NoticeSlot, resolving the C4458 member-shadow error shown in the user's UE4.27 build.
- Native UE4.27 rebuild and live intro verification are pending.

# Version 2.9.0 — per-scene skipping and readable prompts

- Each cinematic prompt has a dark backing with a gold edge and brighter 17-point text. Captions are drawn beside their button's hit surface, keeping waiting text readable while the actual button remains disabled.
- Initial labels use keyboard controls. Key input updates the selected device even during a temporary source-widget focus block. Mouse movement/clicks select keyboard labels, while gamepad analog input must exceed the existing deliberate-input threshold to select controller labels.
- Added an offline regression using the actual key/analog routing and caption-refresh functions with typed stand-ins. It checks keyboard defaults, blocked-focus device updates, analog noise, trigger releases, both prompt sets, disabled controls and choice-stop behavior.
- Configure Level Sequence Skipping provides a per-call Allow Skip checkbox and an explicit On Skipped gameplay handoff. Blocked attempts display a two-second popup. An allowed 0.65-second hold stops the registered scene and invokes the handoff once. Registration clears before that callback to handle repeated input or re-entrant scene changes.
- A second offline regression exercises the actual skip-control methods with typed stand-ins. It checks blocked keyboard/mouse/trigger attempts, callback requirements, tap cancellation, one execution per hold, paused scenes, focus loss, world changes and cleanup on natural finish.
- Native UE4.27 compilation, Slate layout and the live intro still need verification in the project.

# Version 2.8.1 — reported UE4.27 build compatibility errors

- Fixed the five source errors in the user's Visual Studio screenshot: the editor compiler now explicitly obtains a mutable catalogue from a const UE4 row handle; catalogue opening uses the asset path overload; pin refresh disposes old pins through UEdGraphNode::DestroyPin; ReditusFetchQuestDialogue.cpp and ReditusQuestNPCComponent.cpp include their own matching header first. The MSB3073 line was the overall failed-build result.
- Catalogue runtime fixtures now expose const UDataTable pointers, matching UE4.27 row handles. Added a compiled check of the actual editor catalogue lookup, catalogue opening and pin-refresh bodies against the UE4 API contracts, including preservation of pin IDs/wires and removal of deleted outputs.
- Matching-header-first checks now cover every shipped C++ source with a corresponding header, alongside generated-header-last checks.
- All 22 existing offline groups and 4 editor groups pass. The compatibility patch changes no quest progression, reward, recruitment, dialogue-event, battle, UI or save logic.
- This is a source correction validated with offline checks, not a completed UnrealBuildTool/UHT build. Rebuild Development Editor / Win64 in the project to verify native compilation. The unrelated PaperZD/Niagara dependency warning shown in the screenshot is outside this patch.

# Version 2.8.0 — quest graph editor and catalogue

- Editor-only graph asset authoring: Start, Stage and Ready for Turn-in; choice pins, forward branching, stage Details, linked existing conversations, compile/register, save, catalogue opening and editor transactions. Graph data is stripped during cook; the runtime definition stays on the asset.
- Quest catalogue rows point to graph assets or existing simple rows. Simple aliases preserve the original runtime/save key. Graph IDs/catalogue registrations are checked; draft, missing, mismatched, duplicate and mixed references are rejected. Existing quest APIs accept either table kind through a shared resolver.
- Moved the original native definition-to-runtime conversion into a shared adapter used by both graph compilation and runtime registration. Existing objective, timer, route, choice and handoff validation stays in the existing runtime.
- NPC Extra Rules and explicit state slots precede graph conversation defaults. Referenced UDialogue assets and their conditions/events are not modified or executed by compilation. Existing recruitment APIs, event class names, widget/reflection execution, party facade, battle/follow-up and presentation implementation remain intact.
- All 22 existing offline groups pass. New executed checks compile the real graph policy and real reflected catalogue/definition adapter against typed stand-ins: topology errors, 100 graph-order permutations, real runtime branching and save import, alias identity/deduplication, draft/registration guards, conversation references, and invalid definitions. Editor module boundaries and generated-header ordering are checked.
- Included native UE automation test `Reditus.QuestEditor.GraphCompileAndDialogueReferences`; not executed here. No UnrealBuildTool/UHT, Slate rendering/undo, live Blueprint recruitment or real project save/load has been run. Those remain required verification.

# Version 2.7.8 — one hand-in reply after a monster hunt

- Shared flow keeps Accept/NotNow on the initial offer. A completed hunt with only required DefeatEnemy tasks and no item turn-in costs has a single HandIn reply at Ready. Fetch/mixed-item quests and item-cost exchanges retain deferral. Existing four NPC text fields are reused; no new reflected field or save format is introduced.
- Extended the actual shared flow policy test across every phase and both reply modes, asserting the offer always retains both choices and Ready links to only HandIn for the hunt mode. The actual native quest helper tests classification for ordinary/timed hunts, ignored stale item rows, optional item/defeat objectives, required item objectives and item costs.
- The actual transient dialogue builder is compiled/executed with typed stand-ins, checking one hunt-return reply, two offer replies, the unchanged hand-in event, and two fetch-return replies. Existing name/FText/portrait/input/payout guards remain covered. All 22 offline groups pass with strict C++14 warnings and UBSan.
- Compared with 2.7.7, runtime changes are limited to the generated flow's Ready reply links and the builder's definition-based policy. Source comparison checks unchanged quest progression, save format, reward transaction, recruitment/battle/follow-ups, UI presentation artwork, editor cleanup, DialoguePlugin and minimap code. Matching-header/generated-header order and ZIP contents/CRC are checked.
- UnrealBuildTool/UHT, live widget/controller/delegate execution, rendering and real save/load have not been run here. Cold-build and check: decline the offer, accept later, kill the target, return to a single hand-in button, select it for the reward, and talk again without another payment. Check a fetch quest still has its defer choice.

# Version 2.7.7 cleanup validation

- Removed the standalone extortion example generator/console registration and old bootstrap seeding for intro/follow-up/Wait/gate/NPC challenge/recruitment tutorials. New tables seed only the existing simple Hunt and optional Letter-possession examples. Existing table rows, including edited test rows and complex definitions, are not rewritten or deleted.
- The actual CreateQuestSystem function is compiled/executed against typed editor/table stand-ins. Checks fresh hunt/fetch definitions, missing key-item catalog/Letter handling, existing authored/complex row preservation without new writes, and Play/occupied-path guards.
- Existing 21 offline groups remain, covering actual quest/runtime/save logic, branching receipts, NPC removal/follow-ups/recruitment, dialogue presentation, item routing/rewards and concurrent deadlines. All 22 groups pass with strict C++14 warnings and UBSan.
- Removed duplicate old DialoguePlugin backup copies and outdated planning/manual multi-step guides; replaced accumulated install/changelog text with current instructions. Runtime, source dialogue hooks, minimap, reward exchange, UI artwork and serialized names/save format are byte-identical to 2.7.6.
- Added a narrowly scoped Windows cleanup helper: checks a project root and closed Unreal process, removes only hash-matched retired shipped files and this plugin's generated Binaries/Intermediate directories, supports WhatIf/KeepBuildFiles, skips modified legacy files and reparse points. Content/Config/Saved and authored assets are outside its target list. Script paths/hashes are checked against the baseline package; it has not been executed under Windows/PowerShell here.
- Matching-header-first and generated-header ordering, all documentation links and archive contents/CRC are checked. No native UE4.27/UHT build, live Blueprint/delegate/gameplay, rendering, real save serialization or installed-project cleanup has been run here. A cold project build and in-game verification remain required.
