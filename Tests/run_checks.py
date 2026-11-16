#!/usr/bin/env python3
"""Compile actual bridge logic using typed stand-ins; Unreal is still required."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
module = root / "Plugins/ReditusQuestNPC/Source/ReditusQuestNPC"
bridge = module / "Private/ReditusQuestBridge.cpp"
tests = root / "Tests"
source = "\n".join(line for line in bridge.read_text().splitlines()
                   if not line.lstrip().startswith("#include"))
template = (tests / "quest_bridge_test.cpp").read_text()
assert template.count("// ACTUAL_BRIDGE") == 1
with tempfile.TemporaryDirectory(prefix="reditus-quest-check-") as temporary:
    cpp = Path(temporary) / "actual_bridge_test.cpp"
    binary = Path(temporary) / "actual_bridge_test"
    cpp.write_text(template.replace("// ACTUAL_BRIDGE", source))
    command = [os.environ.get("CXX", "g++"), "-std=c++14", "-Wall", "-Wextra",
               "-Werror", "-pedantic", "-fsanitize=undefined", "-fno-sanitize-recover=all",
               "-I", str(tests), "-I", str(module / "Public"), str(cpp), "-o", str(binary)]
    subprocess.run(command, check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: actual bridge call/guard logic and policy matrix (C++14 + UBSan).")
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
print("PASS: actual sequence lookup; controller ownership, owner fallback, playing/paused, stopped, missing and wrong-type objects.")
print("Not covered: Unreal build/UHT, delegates, Blueprint execution, UI, rendering or saves.")
