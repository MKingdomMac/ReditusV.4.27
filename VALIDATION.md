# Validation performed

## Version 1.1.3 — current checks

- Removed GetWidthOverride/GetHeightOverride calls reported unavailable by the user’s UE4.27 compiler. Portrait bounds now read the existing reflected float fields when the corresponding override flag is enabled, with the existing canvas/brush fallback retained.
- Renamed local Slot variables, including those in NativeTick and RefreshChoices, to avoid shadowing the inherited UWidget::Slot member (C4458).
- Reran the included bridge, binding, playback and sequence-lookup offline checks; all passed. Inspected the revised source for the removed calls and shadowing declarations. These checks do not compile the native UMG implementation in Unreal.
- The full package changes one runtime .cpp plus version/instructions from 1.1.2. No Blueprint assets or NPC/quest logic were changed.

## Version 1.1.2 — earlier checks

- Corrected sequence lookup to follow the actual exported `JrpgController → battleController → CurrentLevelSequence` ownership. The prior 1.1.1 lookup incorrectly searched for battleController directly on the widget. Exploration also checks the controller’s existing `Current Sequence ` actor and its SequencePlayer. Stopped players do not identify a cinematic.
- Added a regression that compiles the actual lookup function against small typed object stand-ins, covering that ownership, the owning-player fallback, playing/paused/stopped players, missing objects and wrong object types. This tests lookup logic, not Unreal playback.
- Separated the portrait and text into independent canvas slots, preserving the captured fixed Designer size and reclaiming the text margin when no portrait is shown. Added the original continue-visibility guard to pointer advance.
- Reran all included offline checks with C++14 strict warnings and UBSan; all passed.

## Version 1.1.1 — earlier checks

- Ran `python3 Tests/run_checks.py`. The actual quest-bridge logic, dialogue-close binding helper and engine-independent playback policy compiled as C++14 with strict warnings and UndefinedBehaviorSanitizer; all assertions passed.
- Added a playback-policy regression for normal NPC dialogue: Auto and held Skip do not advance it, Auto is cleared, and moving back into sequence dialogue requires a fresh playback request. Existing choice, voice, log, blocking and duplicate-advance checks passed.
- Compared the quest/NPC/objective/editor and both DialoguePlugin hook source files against the recovered v1.1.0 archive. They are byte-identical. Only presentation code/policy, its module dependency, the plugin version, tests and accompanying instructions change.
- Originally reviewed `battleController.CurrentLevelSequence` against StoryIntegration.json; the missing intervening controller lookup is corrected in 1.1.2. Also and checked the sequence-player and size-box APIs against Epic's UE4.27 documentation: [sequence player](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/MovieSceneSequencePlayer?application_version=4.27), [USizeBox](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/UMG/Components/USizeBox?application_version=4.27). This is an API/contract review, not engine execution.

Unreal Engine compilation/UHT, rendering, actual sequence-context detection, final portrait scale, input focus, Blueprint events, quest rewards, save/load and recruitment behavior have not been executed in this workspace. Run the cold project build and integration checks in README_FIRST.md. No full-sequence skipping is implemented.

## Earlier validation notes carried forward from v1.1.0

Version 1.1.0 adds the dialogue presentation extension. Its actual engine-independent Auto/Skip policy is compiled and executed with C++14 and UBSan: it covers choice counts 1–8, no automatic answer selection, release/press gating after choices or the log, input/focus/sequence readiness gates, voice waiting, resuming Auto after manual selection, reading delays and duplicate-advance prevention. Presentation field/function names and GamepadReply(Index) were checked against the supplied StoryIntegration.json. Two DialoguePlugin source files have additive initialization-hook changes; their original copies are in Backups. The existing Blueprint assets are not rewritten.

The UMG/Slate implementation and the initialization hook have not been compiled or rendered in Unreal here. Policy tests do not validate engine lifecycle, layout, focus, delegates or final gameplay behavior. A cold project build, visual check, and existing sequence/recruitment regression checks are required.


Version 1.0.2 was checked against the supplied live `StoryIntegration.json`. Its dialogue widget's Removed dispatcher has one RecruitmentEvents byte/enum argument, so the NPC adapter now supports that signature and the earlier zero-argument signature. Offline checks compile the actual binding helper and test both forms, duplicate binding prevention, preservation of an existing listener, rejection of unexpected arguments, output parameters, mismatched layouts and a missing handler. These checks use typed stand-ins; actual Unreal delegate execution still requires a project test.

Version 1.0.1 corrects two API names reported by the user's UE4.27 build: `FMapProperty::KeyProp` and `UBlueprint::GetAllGraphs`. The reflection stand-in also now uses the real `KeyProp` spelling; the previous stand-in's incorrect `Key` spelling allowed that mistake through the offline check.

- Compiled the actual `ReditusQuestBridge.cpp` logic with C++14, `-Wall -Wextra -Werror -pedantic` and UndefinedBehaviorSanitizer, using the included small reflection stand-ins. All assertions passed.
- Exercised the quest-state/progress matrix, marker state changes, reordered enum numeric values, Start/Advance/Turn In guards, single reward delivery, item requirements and consumption order, insufficient inventory, a missing inventory function, an unexpected function parameter, an unrelated enum, and a null player.
- Matched the bridge's controller function and event parameter names/types against the supplied `BP_JRPGPlayerController` graph export.
- Checked source delimiters, plugin JSON and generated-header include order.
- Compared the 148 supplied DialoguePlugin/MinimapPlugin source and content files against their original archives; all remain byte-identical. The 1.1.0 deliverable includes the new plugin, two DialoguePlugin native hook files, their original backups, the guide and offline tests.

These are offline checks. No Unreal Engine installation was available for UHT/UnrealBuildTool compilation or editor/gameplay testing. The first required integration check is a cold Development Editor / Win64 build, followed by the single talking-NPC test in `README_FIRST.md`. Widget input focus, Blueprint delegate execution, save/load, rendering, and special battle/recruitment transitions require testing in the project.
