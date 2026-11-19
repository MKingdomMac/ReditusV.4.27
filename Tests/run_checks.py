#!/usr/bin/env python3
"""Compile actual quest runtime and binding/presentation checks; Unreal is still required."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
module = root / "Plugins/ReditusQuestNPC/Source/ReditusQuestNPC"
tests = root / "Tests"
editor = root / "Plugins/ReditusQuestNPC/Source/ReditusQuestNPCEditor/Private/ReditusQuestNPCEditor.cpp"
editor_source = editor.read_text()
setup = "    void CreateQuestSystem()" + editor_source.split("    void CreateQuestSystem()", 1)[1].split("\n}\n", 1)[0]
with tempfile.TemporaryDirectory(prefix="reditus-editor-setup-") as temporary:
    cpp = Path(temporary) / "editor_setup.cpp"; binary = Path(temporary) / "editor_setup"
    cpp.write_text((tests / "quest_editor_setup_test.cpp").read_text().replace("// ACTUAL_EDITOR_SETUP", setup))
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++14", "-Wall", "-Wextra", "-Werror", "-pedantic",
                    "-fsanitize=undefined", "-fno-sanitize-recover=all", str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: actual editor setup; only simple hunt/fetch seeds on new tables, edited/complex rows preserved on existing tables, missing item catalog/row and Play/path guards.")
for stem in ["quest_fetch_flow_test", "quest_fetch_dialogue_test", "quest_fetch_speaker_test", "quest_fetch_exchange_test", "quest_notice_queue_test"]:
    template = (tests / (stem + ".cpp")).read_text()
    if stem == "quest_fetch_speaker_test":
        actual = (module / "Private/ReditusFetchQuestDialogue.cpp").read_text()
        builder = "UDialogue* UReditusQuestNPCComponent::BuildFetchQuestDialogue(" + actual.split("UDialogue* UReditusQuestNPCComponent::BuildFetchQuestDialogue(", 1)[1].split("bool UReditusFetchQuestCondition::IsConditionMet_Implementation(", 1)[0]
        actual = (module / "Private/ReditusDialoguePresentationWidget.cpp").read_text()
        refresh = "void UReditusDialoguePresentationWidget::RefreshSpeakerName(" + actual.split("void UReditusDialoguePresentationWidget::RefreshSpeakerName(", 1)[1].split("void UReditusDialoguePresentationWidget::NativeTick(", 1)[0]
        choose = "void UReditusDialoguePresentationWidget::ChooseReply(" + actual.split("void UReditusDialoguePresentationWidget::ChooseReply(", 1)[1].split("void UReditusDialoguePresentationWidget::RefreshChoices(", 1)[0]
        template = template.replace("// ACTUAL_FETCH_BUILDER_AND_NAME", builder).replace("// ACTUAL_REFRESH_SPEAKER", refresh).replace("// ACTUAL_CHOOSE_REPLY", choose)
    if stem == "quest_fetch_dialogue_test":
        actual = (module / "Private/ReditusFetchQuestDialogue.cpp").read_text()
        helpers = actual.split("namespace\n{", 1)[1].split("    FText FetchLine(", 1)[0]
        actions = "ReditusFetchQuestFlow::Phase UReditusQuestNPCComponent::GetFetchQuestPhase(" + actual.split("ReditusFetchQuestFlow::Phase UReditusQuestNPCComponent::GetFetchQuestPhase(", 1)[1].split("UDialogue* UReditusQuestNPCComponent::BuildFetchQuestDialogue(", 1)[0]
        template = template.replace("// ACTUAL_FETCH_ELIGIBILITY", "namespace\n{" + helpers + "}\n").replace("// ACTUAL_FETCH_PHASE_ACTION", actions)
    if stem == "quest_fetch_exchange_test":
        routing = (module / "Private/ReditusQuestItemRouting.h").read_text()
        actual = (module / "Private/ReditusFetchQuestExchange.cpp").read_text()
        strip = lambda text: "\n".join(line for line in text.splitlines() if not line.lstrip().startswith(("#include", "#pragma")))
        template = template.replace("// ACTUAL_ITEM_ROUTING", strip(routing)).replace("// ACTUAL_FETCH_EXCHANGE", strip(actual))
    if stem == "quest_notice_queue_test":
        actual = (module / "Private/ReditusQuestUI.cpp").read_text()
        enqueue = "void UReditusQuestNotificationsWidget::Enqueue(" + actual.split("void UReditusQuestNotificationsWidget::Enqueue(", 1)[1].split("void UReditusQuestNotificationsWidget::ClearNotices(", 1)[0]
        template = template.replace("// ACTUAL_ENQUEUE", enqueue)
    with tempfile.TemporaryDirectory(prefix="reditus-fetch-check-") as temporary:
        cpp = Path(temporary) / (stem + ".cpp"); binary = Path(temporary) / stem
        cpp.write_text(template)
        subprocess.run([os.environ.get("CXX", "g++"), "-std=c++14", "-Wall", "-Wextra", "-Werror", "-pedantic",
                        "-fsanitize=undefined", "-fno-sanitize-recover=all", "-I", str(module / "Public"), str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("PASS: " + stem + "; actual shared fetch flow/exchange policy, reflected inventory transaction or same-quest reward notice priority (typed stand-ins).")
with tempfile.TemporaryDirectory(prefix="reditus-quest-reward-text-check-") as temporary:
    binary = Path(temporary) / "quest_reward_text"
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++14", "-Wall", "-Wextra", "-Werror", "-pedantic",
                    "-fsanitize=undefined", "-fno-sanitize-recover=all", "-I", str(module / "Public"),
                    str(tests / "quest_reward_text_test.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: actual reward presentation plan; offered/received/failed states, gold, experience, item indexes/stacks, empty rewards, large amounts and read-only previews. Native labels use localizable FText formats.")
component_source = (module / "Private/ReditusQuestNPCComponent.cpp").read_text()
state_source = (module / "Private/ReditusQuestNPCState.cpp").read_text()
removal = "bool UReditusQuestNPCComponent::ApplyPersistentRemoval(" + component_source.split("bool UReditusQuestNPCComponent::ApplyPersistentRemoval(", 1)[1].split("void UReditusQuestNPCComponent::HandleNPCRemoved(", 1)[0]
busy = "bool UReditusQuestSubsystem::IsNPCConversationActive(" + state_source.split("bool UReditusQuestSubsystem::IsNPCConversationActive(", 1)[1].split("bool UReditusQuestSubsystem::RemoveNPCPermanently(", 1)[0]
with tempfile.TemporaryDirectory(prefix="reditus-npc-removal-check-") as temporary:
    cpp = Path(temporary) / "actual_npc_removal.cpp"; binary = Path(temporary) / "actual_npc_removal"
    cpp.write_text((tests / "quest_npc_removal_test.cpp").read_text().replace("// ACTUAL_NPC_REMOVAL", busy + removal))
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++14", "-Wall", "-Wextra", "-Werror", "-pedantic",
                    "-fsanitize=undefined", "-fno-sanitize-recover=all", str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: actual NPC destruction/restore callbacks and shared battle/dialogue lifetime guard; late holds cancel already-queued destruction and preserve the speaker for follow-up.")
for stem, source_path, marker in [
    ("quest_follow_up_test", module / "Private/ReditusNPCBattleFollowUp.cpp", "// ACTUAL_FOLLOW_UP"),
    ("quest_linked_actor_test", module / "Private/ReditusNPCLinkedActorComponent.cpp", "// ACTUAL_LINKED_ACTOR"),
    ("quest_sequence_gate_test", module / "Private/ReditusQuestSequenceGate.h", "// ACTUAL_SEQUENCE_GATE"),
]:
    actual = "\n".join(line for line in source_path.read_text().splitlines()
                       if not line.lstrip().startswith(("#include", "#pragma")))
    with tempfile.TemporaryDirectory(prefix="reditus-outcome-check-") as temporary:
        cpp = Path(temporary) / (stem + ".cpp"); binary = Path(temporary) / stem
        cpp.write_text((tests / (stem + ".cpp")).read_text().replace(marker, actual))
        subprocess.run([os.environ.get("CXX", "g++"), "-std=c++14", "-Wall", "-Wextra", "-Werror", "-pedantic",
                        "-fsanitize=undefined", "-fno-sanitize-recover=all", "-I", str(module / "Public"), str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("PASS: actual " + stem + "; follow-up lifecycle, linked removal/restore or exploration sequence gating.")
with tempfile.TemporaryDirectory(prefix="reditus-new-quest-check-") as temporary:
    binary = Path(temporary) / "new_quest_runtime_test"
    command = [os.environ.get("CXX", "g++"), "-std=c++14", "-Wall", "-Wextra",
               "-Werror", "-pedantic", "-fsanitize=undefined", "-fno-sanitize-recover=all",
               "-I", str(module / "Public"), str(tests / "quest_runtime_test.cpp"), "-o", str(binary)]
    subprocess.run(command, check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: actual new quest runtime; stages, prerequisites, event deduplication, inventory snapshots, rewards, wait/deadline timers, clock formatting, lifecycle ordering and atomic save import.")
with tempfile.TemporaryDirectory(prefix="reditus-npc-world-check-") as temporary:
    binary = Path(temporary) / "quest_world_test"
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++14", "-Wall", "-Wextra", "-Werror", "-pedantic",
                    "-fsanitize=undefined", "-fno-sanitize-recover=all", "-I", str(module / "Public"),
                    str(tests / "quest_world_test.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: actual persistent-NPC/recruitment receipt state; removal reasons, atomic import, reset, duplicate guards and RecruitUnit/WinBattle objectives.")
facade = "\n".join(line for line in (module / "Private/ReditusQuestPartyFacade.h").read_text().splitlines()
                   if not line.lstrip().startswith(("#include", "#pragma")))
with tempfile.TemporaryDirectory(prefix="reditus-party-facade-check-") as temporary:
    cpp = Path(temporary) / "actual_party_facade_test.cpp"; binary = Path(temporary) / "actual_party_facade_test"
    cpp.write_text((tests / "quest_party_facade_test.cpp").read_text().replace("// ACTUAL_PARTY_FACADE", facade))
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++14", "-Wall", "-Wextra", "-Werror", "-pedantic",
                    "-fsanitize=undefined", "-fno-sanitize-recover=all", "-I", str(tests), str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: actual existing-party facade adapter; starting level, signature guards, full-party reserve, no duplicate unlock/add, missing/wrong fields and rejected unlock.")
inventory = (module / "Private/ReditusQuestSubsystem.cpp").read_text()
item_routing = "\n".join(line for line in (module / "Private/ReditusQuestItemRouting.h").read_text().splitlines()
                         if not line.lstrip().startswith(("#include", "#pragma")))
validation = "    bool ValidItemRow(" + inventory.split("    bool ValidItemRow(", 1)[1].split("    bool ValidItems(", 1)[0]
lookup = "bool UReditusQuestSubsystem::GetQuestItemCount(" + inventory.split("bool UReditusQuestSubsystem::GetQuestItemCount(", 1)[1].split("bool UReditusQuestSubsystem::RefreshQuestInventory", 1)[0]
template = (tests / "quest_item_rows_test.cpp").read_text()
with tempfile.TemporaryDirectory(prefix="reditus-item-row-check-") as temporary:
    cpp = Path(temporary) / "actual_item_lookup.cpp"
    binary = Path(temporary) / "actual_item_lookup"
    cpp.write_text(template.replace("// ACTUAL_ITEM_ROUTING", item_routing).replace("// ACTUAL_ITEM_ROW_VALIDATION", validation).replace("// ACTUAL_INVENTORY_LOOKUP", lookup))
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++14", "-Wall", "-Wextra", "-Werror", "-pedantic",
                    "-fsanitize=undefined", "-fno-sanitize-recover=all", str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: actual item-table inference/lookup and delivery normalization; all four bags, stale legacy kinds, equal row names, unknown catalogs, invalid rows/types and missing inventory.")
reflection = module / "Private/ReditusQuestNPCReflection.h"
source = "\n".join(line for line in reflection.read_text().splitlines()
                   if not line.lstrip().startswith(("#include", "#pragma")))
template = (tests / "dialogue_binding_test.cpp").read_text()
assert template.count("// ACTUAL_REFLECTION") == 1
with tempfile.TemporaryDirectory(prefix="reditus-dialogue-check-") as temporary:
    cpp = Path(temporary) / "actual_binding_test.cpp"
    binary = Path(temporary) / "actual_binding_test"
    cpp.write_text(template.replace("// ACTUAL_REFLECTION", source))
    command = [os.environ.get("CXX", "g++"), "-std=c++14", "-Wall", "-Wextra",
               "-Werror", "-pedantic", "-fsanitize=undefined", "-fno-sanitize-recover=all",
               "-I", str(tests), str(cpp), "-o", str(binary)]
    subprocess.run(command, check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: actual dialogue-close binding helper, legacy/current signatures, listener preservation and layout guards.")
with tempfile.TemporaryDirectory(prefix="reditus-playback-check-") as temporary:
    binary = Path(temporary) / "presentation_policy_test"
    command = [os.environ.get("CXX", "g++"), "-std=c++14", "-Wall", "-Wextra",
               "-Werror", "-pedantic", "-fsanitize=undefined", "-fno-sanitize-recover=all",
               "-I", str(module / "Public"), str(tests / "dialogue_presentation_test.cpp"), "-o", str(binary)]
    subprocess.run(command, check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: actual Auto/Skip policy; NPC playback reset, sequence transitions, choices, voice, log, input gates, fresh-release latch and duplicate-advance prevention.")
presentation = (module / "Private/ReditusDialoguePresentationWidget.cpp").read_text()
lookup = presentation.split("    UMovieSceneSequencePlayer* SequencePlayer(", 1)[1].split("    bool Shown(", 1)[0]
lookup = "    UMovieSceneSequencePlayer* SequencePlayer(" + lookup
template = (tests / "sequence_context_test.cpp").read_text()
continuation = "void UReditusDialoguePresentationWidget::ContinueDialogue(" + presentation.split("void UReditusDialoguePresentationWidget::ContinueDialogue(", 1)[1].split("void UReditusDialoguePresentationWidget::ToggleAuto(", 1)[0]
template = template.replace("// ACTUAL_CONTINUE_DIALOGUE", continuation)
assert template.count("// ACTUAL_SEQUENCE_LOOKUP") == 1
with tempfile.TemporaryDirectory(prefix="reditus-sequence-check-") as temporary:
    cpp = Path(temporary) / "actual_sequence_lookup_test.cpp"
    binary = Path(temporary) / "actual_sequence_lookup_test"
    cpp.write_text(template.replace("// ACTUAL_SEQUENCE_LOOKUP", lookup))
    command = [os.environ.get("CXX", "g++"), "-std=c++14", "-Wall", "-Wextra",
               "-Werror", "-pedantic", "-fsanitize=undefined", "-fno-sanitize-recover=all",
               str(cpp), "-o", str(binary)]
    subprocess.run(command, check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: sequence dialogue routing; owning controller, tracked exploration player, legacy battle player and continue notification before Blueprint execution.")
marker_source = (module / "Private/ReditusQuestSubsystem.cpp").read_text()
adapter = "bool UReditusQuestSubsystem::GetQuestNPCMarkerState(" + marker_source.split("bool UReditusQuestSubsystem::GetQuestNPCMarkerState(", 1)[1].split("bool UReditusQuestSubsystem::GetQuestDetails(", 1)[0]
with tempfile.TemporaryDirectory(prefix="reditus-quest-marker-check-") as temporary:
    cpp = Path(temporary) / "actual_quest_marker.cpp"; binary = Path(temporary) / "actual_quest_marker"
    cpp.write_text((tests / "quest_marker_test.cpp").read_text().replace("// ACTUAL_MARKER_ADAPTER", adapter))
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++14", "-Wall", "-Wextra", "-Werror", "-pedantic",
                    "-fsanitize=undefined", "-fno-sanitize-recover=all", "-I", str(module / "Public"),
                    "-I", str(root / "Plugins/MinimapPlugin/Source/MinimapPlugin/Public"),
                    str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: actual quest marker adapter/policy and minimap glyph raster; classification, states, stage overrides, display switches, invalid rows, terminal hiding and transparent !/? artwork.")
with tempfile.TemporaryDirectory(prefix="reditus-quest-steps-check-") as temporary:
    binary = Path(temporary) / "quest_steps"
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++14", "-Wall", "-Wextra", "-Werror", "-pedantic",
                    "-fsanitize=undefined", "-fno-sanitize-recover=all", "-I", str(module / "Public"),
                    str(tests / "quest_steps_test.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: actual branching step engine; all five bargain amounts, save at every step, one-time handoffs, path validation, corrupt choice rejection, paused delivery deadlines, safe retry and legacy linear saves.")
steps_source = (module / "Private/ReditusQuestSteps.cpp").read_text()
choice_lookup = "bool UReditusQuestSubsystem::GetQuestChoice(" + steps_source.split("bool UReditusQuestSubsystem::GetQuestChoice(", 1)[1].split("TArray<FReditusQuestSavedChoice>", 1)[0]
build_delivery = "bool UReditusQuestSubsystem::BuildStepDelivery(" + steps_source.split("bool UReditusQuestSubsystem::BuildStepDelivery(", 1)[1].split("bool UReditusQuestSubsystem::RequestQuestStepAction(", 1)[0]
subsystem_source = (module / "Private/ReditusQuestSubsystem.cpp").read_text()
confirm = "bool UReditusQuestSubsystem::ConfirmQuestDelivery(" + subsystem_source.split("bool UReditusQuestSubsystem::ConfirmQuestDelivery(", 1)[1].split("bool UReditusQuestSubsystem::CancelQuestDelivery(", 1)[0]
start_quest = "bool UReditusQuestSubsystem::StartQuest(" + subsystem_source.split("bool UReditusQuestSubsystem::StartQuest(", 1)[1].split("bool UReditusQuestSubsystem::ReportQuestEvent(", 1)[0]
refresh_inventory = "bool UReditusQuestSubsystem::RefreshQuestInventory(" + subsystem_source.split("bool UReditusQuestSubsystem::RefreshQuestInventory(", 1)[1].split("bool UReditusQuestSubsystem::ReadQuest(", 1)[0]
turn_in = "bool UReditusQuestSubsystem::TurnInQuest(" + subsystem_source.split("bool UReditusQuestSubsystem::TurnInQuest(", 1)[1].split("bool UReditusQuestSubsystem::ConfirmQuestDelivery(", 1)[0]
clock_source = (module / "Private/ReditusQuestSequences.cpp").read_text()
objective_source = (module / "Private/ReditusQuestObjectiveComponent.cpp").read_text()
exchange_source = (module / "Private/ReditusFetchQuestExchange.cpp").read_text()
timed_arrival = "\n".join(start + source.split(start, 1)[1].split(end, 1)[0] for source, start, end in [
    (clock_source, "bool UReditusQuestSubsystem::AreQuestTimersPaused(", "bool UReditusQuestSubsystem::CanSaveQuestState("),
    (objective_source, "bool UReditusQuestObjectiveComponent::ReportObjectiveReached(", "void UReditusQuestObjectiveComponent::EndPlay("),
    (exchange_source, "bool UReditusQuestSubsystem::TurnInDialogueQuest(", "bool UReditusQuestSubsystem::ApplyBuiltInFetchDelivery("),
])
with tempfile.TemporaryDirectory(prefix="reditus-step-delivery-check-") as temporary:
    cpp = Path(temporary) / "actual_step_delivery.cpp"; binary = Path(temporary) / "actual_step_delivery"
    cpp.write_text((tests / "quest_step_delivery_test.cpp").read_text().replace("// ACTUAL_ITEM_ROUTING", item_routing).replace("// ACTUAL_CHOICE_LOOKUP", choice_lookup)
                   .replace("// ACTUAL_STEP_DELIVERY", build_delivery).replace("// ACTUAL_DELIVERY_CONFIRM", confirm)
                   .replace("// ACTUAL_START_QUEST", start_quest).replace("// ACTUAL_REFRESH_INVENTORY", refresh_inventory).replace("// ACTUAL_TURN_IN", turn_in)
                   .replace("// ACTUAL_ARRIVAL_AND_CLOCK", timed_arrival))
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++14", "-Wall", "-Wextra", "-Werror", "-pedantic",
                    "-fsanitize=undefined", "-fno-sanitize-recover=all", "-I", str(module / "Public"),
                    str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: actual native start/inventory refresh/turn-in, choice lookup, step delivery and confirmation; pre-owned/returning items, stale readiness, missing payout handler, one confirmed reward, duplicate receipts and multi-step handoffs.")
print("PASS: cinematic timer gate and timed arrival adapter; playing and dialogue pauses, finished held frames, clock continuation, single in-time reward, expired arrival and cancelled delivery retry.")
ui = (module / "Private/ReditusQuestUI.cpp").read_text()
journal = "    bool MatchesPage(" + ui.split("    bool MatchesPage(", 1)[1].split("    UClass* QuestRowClass(", 1)[0]
with tempfile.TemporaryDirectory(prefix="reditus-quest-journal-check-") as temporary:
    cpp = Path(temporary) / "actual_quest_journal.cpp"; binary = Path(temporary) / "actual_quest_journal"
    cpp.write_text((tests / "quest_journal_test.cpp").read_text().replace("// ACTUAL_JOURNAL_PAGE_AND_BLUEPRINT_READER", journal))
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++14", "-Wall", "-Wextra", "-Werror", "-pedantic",
                    "-fsanitize=undefined", "-fno-sanitize-recover=all", "-I", str(tests), str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: actual journal page rules and read-only Blueprint quest list; active main/side and finished entries, class defaults, progress, duplicate history, invalid entries and graph quest filtering.")
refresh = "void UReditusQuestNotificationsWidget::RefreshTimers(" + ui.split("void UReditusQuestNotificationsWidget::RefreshTimers(", 1)[1].split("void UReditusQuestNotificationsWidget::NativeTick(", 1)[0]
with tempfile.TemporaryDirectory(prefix="reditus-stable-timer-ui-check-") as temporary:
    cpp = Path(temporary) / "actual_timer_refresh.cpp"; binary = Path(temporary) / "actual_timer_refresh"
    cpp.write_text((tests / "quest_timer_ui_test.cpp").read_text().replace("// ACTUAL_TIMER_REFRESH", refresh))
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++14", "-Wall", "-Wextra", "-Werror", "-pedantic",
                    "-fsanitize=undefined", "-fno-sanitize-recover=all", str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: actual timer refresh adapter; stable frame/text identity across seconds, unchanged-label suppression, pause text, multiple clocks, removal and restart without per-second widget reconstruction.")
print("Not covered: Unreal build/UHT, delegates, Blueprint execution, UI, rendering or saves.")

footer_source = (module / "Private/ReditusDialoguePresentationWidget.cpp").read_text()
footer_test = (tests / "dialogue_footer_input_test.cpp").read_text()
for marker, start, end in [
    ("// ACTUAL_CAN_ROUTE", "bool UReditusDialoguePresentationWidget::CanRouteInput(", "bool UReditusDialoguePresentationWidget::InputAllowed("),
    ("// ACTUAL_RESTORE_FOCUS", "void UReditusDialoguePresentationWidget::RestoreSourceFocus(", "FReply UReditusDialoguePresentationWidget::NativeOnMouseButtonDown("),
    ("// ACTUAL_TOGGLE_AUTO", "void UReditusDialoguePresentationWidget::ToggleAuto(", "void UReditusDialoguePresentationWidget::ToggleLog("),
    ("// ACTUAL_TOGGLE_LOG", "void UReditusDialoguePresentationWidget::ToggleLog(", "void UReditusDialoguePresentationWidget::UpdateHeld("),
    ("// ACTUAL_UPDATE_HELD", "void UReditusDialoguePresentationWidget::UpdateHeld(", "void UReditusDialoguePresentationWidget::SkipPressed("),
    ("// ACTUAL_SKIP_BUTTON", "void UReditusDialoguePresentationWidget::SkipPressed(", "void UReditusDialoguePresentationWidget::InputInactive("),
    ("// ACTUAL_ROUTE_KEYS", "bool UReditusDialoguePresentationWidget::RouteKeyDown(", "bool UReditusDialoguePresentationWidget::RouteAnalog("),
    ("// ACTUAL_ROUTE_ANALOG", "bool UReditusDialoguePresentationWidget::RouteAnalog(", "void UReditusDialoguePresentationWidget::SelectReply("),
    ("// ACTUAL_REFRESH_CONTROLS", "void UReditusDialoguePresentationWidget::RefreshControls(", "void UReditusDialoguePresentationWidget::UpdateLog("),
]:
    actual = start + footer_source.split(start, 1)[1].split(end, 1)[0]
    footer_test = footer_test.replace(marker, actual)
construct = footer_source.split("void UReditusDialoguePresentationWidget::NativeConstruct()", 1)[1].split("void UReditusDialoguePresentationWidget::NativeDestruct()", 1)[0]
assert "UsingGamepad = false;" in construct and "UsingKeyboard" not in construct
assert "bIsFocusable = true;" in construct
assert "SetCursor(EMouseCursor::None);" in construct
assert "if (bFirstPresentation) RestoreSourceFocus();" in footer_source
with tempfile.TemporaryDirectory(prefix="reditus-footer-check-") as temporary:
    cpp = Path(temporary) / "dialogue_footer_input_test.cpp"
    binary = Path(temporary) / "dialogue_footer_input_test"
    cpp.write_text(footer_test)
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++14", "-Wall", "-Wextra", "-Werror", "-pedantic",
                    "-fsanitize=undefined", "-fno-sanitize-recover=all",
                    "-I", str(module / "Public"), str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: actual native focus, Auto/Log handlers, footer routing and captions; non-focusable Blueprint parent, native-child focus, next-screen ownership, keyboard labels, analog noise and choice-stop behavior.")


skip_source = (module / "Private/ReditusSequenceSkip.cpp").read_text()
skip_test = (tests / "sequence_skip_test.cpp").read_text()
skip_test = skip_test.replace("// ACTUAL_CONFIGURE_NODE_AND_CONTINUATION",
    "bool UReditusSequenceSkipLibrary::ConfigureLevelSequenceSkipping("
    + skip_source.split("bool UReditusSequenceSkipLibrary::ConfigureLevelSequenceSkipping(", 1)[1])
for marker, start, end in [
    ("// ACTUAL_CONTROLLER_ACTOR", "    ALevelSequenceActor* ControllerSequenceActor(", "    bool SkipKey("),
    ("// ACTUAL_CONFIGURE", "bool UReditusSequenceSkipSubsystem::Configure(", "bool UReditusSequenceSkipSubsystem::HasActiveSequence("),
    ("// ACTUAL_HAS_ACTIVE", "bool UReditusSequenceSkipSubsystem::HasActiveSequence(", "bool UReditusSequenceSkipSubsystem::CanCapture("),
    ("// ACTUAL_CLEAR", "void UReditusSequenceSkipSubsystem::ClearSequence(", "void UReditusSequenceSkipSubsystem::SequenceEnded("),
    ("// ACTUAL_SEQUENCE_ENDED", "void UReditusSequenceSkipSubsystem::SequenceEnded(", "void UReditusSequenceSkipSubsystem::ShowBlockedNotice("),
    ("// ACTUAL_ATTEMPT", "bool UReditusSequenceSkipSubsystem::BeginAttempt(", "bool UReditusSequenceSkipSubsystem::HandleKey("),
    ("// ACTUAL_KEY", "bool UReditusSequenceSkipSubsystem::HandleKey(", "bool UReditusSequenceSkipSubsystem::HandleTrigger("),
    ("// ACTUAL_TRIGGER", "bool UReditusSequenceSkipSubsystem::HandleTrigger(", "void UReditusSequenceSkipSubsystem::SetMouseHeld("),
    ("// ACTUAL_MOUSE", "void UReditusSequenceSkipSubsystem::SetMouseHeld(", "void UReditusSequenceSkipSubsystem::Tick("),
    ("// ACTUAL_TICK", "void UReditusSequenceSkipSubsystem::Tick(", "bool UReditusSequenceSkipLibrary::ConfigureLevelSequenceSkipping("),
    ("// ACTUAL_AUTO_SEQUENCE", "void UReditusSequenceSkipSubsystem::SetAutoAdvance(", "void UReditusSequenceSkipSubsystem::DismissSequenceDialogue("),
]:
    actual = start + skip_source.split(start, 1)[1].split(end, 1)[0]
    skip_test = skip_test.replace(marker, actual)
with tempfile.TemporaryDirectory(prefix="reditus-sequence-skip-check-") as temporary:
    cpp = Path(temporary) / "sequence_skip_test.cpp"
    binary = Path(temporary) / "sequence_skip_test"
    cpp.write_text(skip_test)
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++14", "-Wall", "-Wextra", "-Werror", "-pedantic",
                    "-fsanitize=undefined", "-fno-sanitize-recover=all", str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: scene registration with the exported unwired actor pin, protected and permitted holds, single finish handoff, timer-driven dialogue closure, authored sequence cue, manual resume and replacement guards.")

notice = "void UReditusSequenceSkipNotice::SetMessage(" + skip_source.split("void UReditusSequenceSkipNotice::SetMessage(", 1)[1].split("void UReditusSequenceSkipSubsystem::Initialize(", 1)[0]
with tempfile.TemporaryDirectory(prefix="reditus-skip-notice-") as temporary:
    cpp = Path(temporary) / "notice.cpp"; binary = Path(temporary) / "notice"
    cpp.write_text((tests / "sequence_skip_notice_test.cpp").read_text().replace("// ACTUAL_NOTICE_WIDGET", notice))
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++14", "-Wall", "-Wextra", "-Wshadow", "-Werror", "-pedantic",
                    "-fsanitize=undefined", str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: actual skip notice builder and updates; visible hold percentage/fill, blocked message, decorative mouse transparency and inherited Slot compatibility.")

frame_test = (tests / "cinematic_prompt_frame_test.cpp").read_text()
for marker, start, end in [
    ("// ACTUAL_SOLID", "    UBorder* Solid(", "    UBorder* Frame("),
    ("// ACTUAL_ANCHORED", "    UCanvasPanelSlot* Anchored(", "    UCanvasPanel* DialogueBackdrop("),
    ("// ACTUAL_GLASS_FRAME", "    void Corner(", "    void OrnamentStroke("),
    ("// ACTUAL_ORNATE_PROMPT", "    void OrnamentStroke(", "    UWidget* Rule("),
]:
    actual = start + footer_source.split(start, 1)[1].split(end, 1)[0]
    frame_test = frame_test.replace(marker, actual)
with tempfile.TemporaryDirectory(prefix="reditus-prompt-frame-") as temporary:
    cpp = Path(temporary) / "cinematic_prompt_frame_test.cpp"
    binary = Path(temporary) / "cinematic_prompt_frame_test"
    cpp.write_text(frame_test)
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++14", "-Wall", "-Wextra", "-Wshadow",
                    "-Werror", "-pedantic", "-fsanitize=undefined", "-fno-sanitize-recover=all",
                    str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: actual ornate prompt construction; transparent backing, decorative geometry, preserved caption and open mouse hit paths.")
