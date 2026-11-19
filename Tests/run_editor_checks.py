#!/usr/bin/env python3
"""Exercise the real graph policy, catalogue adapter and shared definition compiler."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
runtime = root / 'Plugins/ReditusQuestNPC/Source/ReditusQuestNPC'
editor = root / 'Plugins/ReditusQuestNPC/Source/ReditusQuestNPCEditor'
tests = root / 'Tests'

def without_includes(path):
    return '\n'.join(line for line in path.read_text().splitlines()
                     if not line.lstrip().startswith(('#include', '#pragma')))

catalog = (tests / 'quest_catalog_test.cpp').read_text()
for marker, path in [
    ('// ACTUAL_SYSTEM_TYPES', runtime / 'Public/ReditusQuestSystemTypes.h'),
    ('// ACTUAL_ASSET_TYPES', runtime / 'Public/ReditusQuestAsset.h'),
    ('// ACTUAL_CATALOG_TYPES', runtime / 'Public/ReditusQuestCatalog.h'),
    ('// ACTUAL_CATALOG', runtime / 'Private/ReditusQuestCatalog.cpp'),
    ('// ACTUAL_DEFINITION_COMPILER', runtime / 'Private/ReditusQuestDefinitionCompiler.cpp'),
]:
    catalog = catalog.replace(marker, without_includes(path))
source = (runtime / 'Private/ReditusQuestSubsystem.cpp').read_text()
key = 'FString UReditusQuestSubsystem::Key(' + source.split('FString UReditusQuestSubsystem::Key(', 1)[1].split('\nUReditusQuestSubsystem*', 1)[0]
catalog = catalog.replace('// ACTUAL_QUEST_KEY', key)

compat = (tests / 'quest_editor_ue427_compat_test.cpp').read_text()
compiler = (editor / 'Private/ReditusQuestGraphCompiler.cpp').read_text()
lookup = '        UDataTable* Catalog =' + compiler.split('        UDataTable* Catalog =', 1)[1].split('        const auto* Existing', 1)[0]
toolkit = (editor / 'Private/ReditusQuestEditorToolkit.cpp').read_text()
opening = '                    FString Error; auto* Catalog =' + toolkit.split('                    FString Error; auto* Catalog =', 1)[1].split('                    return FReply::Handled();', 1)[0]
graph = (editor / 'Private/ReditusQuestGraph.cpp').read_text()
refresh = 'void UReditusQuestGraphNode::RefreshPins()' + graph.split('void UReditusQuestGraphNode::RefreshPins()', 1)[1].split('void UReditusQuestGraphNode::PostEditChangeProperty(', 1)[0]
compat = compat.replace('// ACTUAL_MUTABLE_CATALOG_LOOKUP', lookup).replace('// ACTUAL_OPEN_CATALOG', opening).replace('// ACTUAL_REFRESH_PINS', refresh)

for name, text in [('quest_graph_policy_test', (tests / 'quest_graph_policy_test.cpp').read_text()),
                   ('quest_catalog_test', catalog), ('quest_editor_ue427_compat_test', compat)]:
    with tempfile.TemporaryDirectory(prefix='reditus-quest-editor-') as temporary:
        cpp = Path(temporary) / (name + '.cpp')
        binary = Path(temporary) / name
        cpp.write_text(text)
        subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++14', '-Wall', '-Wextra', '-Werror', '-pedantic',
                        '-fsanitize=undefined', '-fno-sanitize-recover=all',
                        '-I', str(runtime / 'Public'), '-I', str(editor / 'Private'), str(cpp), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print('PASS: ' + name)

# Check module boundaries and reflected header layout in all shipped source.
for path in (root / 'Plugins/ReditusQuestNPC/Source').rglob('*.h'):
    includes = [line.strip() for line in path.read_text().splitlines() if line.startswith('#include')]
    generated = [line for line in includes if '.generated.h' in line]
    if generated:
        assert len(generated) == 1 and generated[0] == includes[-1], path
headers = {path.name for path in (root / 'Plugins/ReditusQuestNPC/Source').rglob('*.h')}
for path in (root / 'Plugins/ReditusQuestNPC/Source').rglob('*.cpp'):
    if path.stem + '.h' in headers:
        includes = [line.strip() for line in path.read_text().splitlines() if line.startswith('#include')]
        assert includes[0] == '#include "' + path.stem + '.h"', path
asset_header = (runtime / 'Public/ReditusQuestAsset.h').read_text()
assert '#if WITH_EDITORONLY_DATA' in asset_header and 'UPROPERTY() UEdGraph* EditorGraph' in asset_header
runtime_build = (runtime / 'ReditusQuestNPC.Build.cs').read_text()
assert all('"' + module + '"' not in runtime_build for module in ['UnrealEd', 'GraphEditor', 'AssetTools', 'PropertyEditor'])
assert 'ReditusQuestCatalog::BuildDefinition(*Row, ID, D, Error)' in source
component = (runtime / 'Private/ReditusQuestNPCComponent.cpp').read_text()
selection = component.split('UDialogue* UReditusQuestNPCComponent::ChooseDialogue(', 1)[1].split('void UReditusQuestNPCComponent::HandleInteracted(', 1)[0]
assert selection.index('return Rule.Dialogue;') < selection.index('switch (Snapshot.State)') < selection.index('if (!Selected) Selected = ReditusQuestCatalog::Conversation')
assert 'return Selected ? Selected : Definition.FallbackDialogue;' in selection
print('PASS: editor-only graph boundary, matching/generated header order, shared runtime adapter and explicit NPC dialogue priority.')
print('Unreal build/UHT, native automation, editor rendering/undo and live Blueprint recruitment still require UE4.27.')
