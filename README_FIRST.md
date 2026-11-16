# Reditus reusable quest NPC — UE4.27

Version **1.1.3** carries forward the reusable NPC and quest bridge and revises the dialogue UI: floating text on a fully transparent background, original portrait bounds, centered translucent choices, fine gold ornaments and a teal selection ribbon. Advance, Auto, Log and Hold Skip appear only in Level Sequence dialogue. The current DialoguePlugin widget remains the dialogue/playback controller. Portraits retain their existing fixed Designer bounds independently of the text area; they are not forced into the previous 152-pixel box.

## 1.1.3 — install the dialogue UI update

This build correction addresses the reported UE4.27 C2039 errors by reading SizeBox’s reflected WidthOverride/HeightOverride fields instead of calling unavailable getters. It also renames the local canvas-slot variables that shadow UWidget::Slot and trigger C4458. The layout and dialogue behavior remain as in 1.1.2.

This revision corrects the controller path used to detect sequence dialogue, preserves portrait height independently of the text layout, and prevents clicks from advancing before the original continue control is ready. It retains the transparent presentation and centered choices from 1.1.1.

1. Close Unreal Editor.
2. Merge this package's **Plugins** folder into your project's existing **Plugins** folder, replacing the included files. Keep the existing DialoguePlugin folder: the package supplies only two of its source files, not a replacement for the entire original plugin.
3. Build **Development Editor / Win64** in Visual Studio with Unreal closed, then reopen the project. Do not use Hot Reload for this update.
4. Interact with the same working test NPC. You should see floating dialogue and a gold speaker ornament, with the bottom control strip hidden. A response choice opens the translucent panel in the center. During a Level Sequence, the small control strip appears at the bottom.

Keep your generated NPC Blueprints, table rows and dialogue assets. You do not need to run the setup command again for this update. If your project files do not discover the new source files, regenerate them from the .uproject before building.

The UI applies automatically to supported DialogueUserWidget instances, including the new NPC and existing uses of DemoDialogueWidget. The runtime preflight checks the widget fields/functions found in your supplied StoryIntegration export; a different widget that does not match keeps its existing layout.

This is runtime C++ presentation. The DemoDialogueWidget Designer asset keeps its original tree; run the map and interact to see the new layout.

| Action | Keyboard | Gamepad / pointer |
| --- | --- | --- |
| Advance / confirm a selected response | Your existing Enter/Space binding | Your existing A binding; click the dialogue or a response |
| Toggle Auto | F6 | Y; click Auto |
| Open/close conversation log | F7 | X; click Log |
| Hold Skip | Left or Right Shift | Hold RT; hold the Skip button |
| Close the log | Escape or F7 | B or X; click Close |
| Scroll the log | Up/Down or mouse wheel | D-pad Up/Down or left stick |

The Auto/Log/Skip buttons and their added shortcuts are available only in Level Sequence dialogue. Normal NPCs keep the original confirm and response-navigation bindings; click the floating dialogue to advance without a footer. The sequence context is read from the current node and the exported `JrpgController → battleController → CurrentLevelSequence` player, with the controller’s `Current Sequence ` actor as the exploration fallback. Playing and dialogue-paused players count; a retained stopped player does not. Leaving sequence dialogue clears Auto and held Skip.

Auto and Skip never choose a response, including when only one response is available. Auto pauses for the choice and resumes reading after the player's selection. Held Skip requires a release and fresh press after a choice. Auto waits for the existing Sound2D voice to finish, and uses a reading delay. Skip advances eligible dialogue lines faster; neither control fast-forwards a Level Sequence or bypasses its blocking state.

Portraits reuse the original image widget, render transform and fixed Designer bounds, captured before reparenting. Scale-to-fit keeps the aspect ratio. If the source uses a stretched layout that cannot be measured before Slate construction, the fallback fits its brush inside 256 UI units without enlarging it. The large portrait in the reference screenshot is not a target size. This fallback and final portrait placement still need an in-game visual check.

The log contains displayed text from the current widget session, up to 120 entries. It is not a new save-game database. Opening it pauses the new Auto/Skip controls and blocks confirming hidden choices. It becomes available at a ready dialogue/choice point; it does not pause an already running gameplay sequence or replace the original widget's timers.

For a quick presentation fallback, enter this in Output Log **before opening a new conversation**:

```text
Reditus.DialogueStyle 0
```

Use `Reditus.DialogueStyle 1` to enable the new presentation for subsequent conversations. This setting does not remove the current open widget; close it normally first.

### What changed in DialoguePlugin

Two native source files now contain a small optional initialization hook: DialogueUserWidget.h and DialogueUserWidget.cpp. ReditusQuestNPC registers the presentation through that hook before Slate builds the widget. Original copies are included under Backups/DialoguePlugin/Source/DialoguePlugin.

The original widget remains the object referenced by your controller. Its text and portrait controls are used in the new layout; the remaining legacy controls stay constructed in a hidden layer so reply construction, variable substitution and existing selection/click handlers continue to run. New choice cards call the original GamepadReply handler only after a player click. Automatic playback calls only the existing continue handler, with no choice index.

The quest NPC, objective component, bridge, quest policies, editor setup/export and both DialoguePlugin initialization-hook files are byte-identical to the recovered v1.1.0 package. Fonts, colors, ornaments and layout are in ReditusDialoguePresentationWidget.cpp. The native module adds a private MovieScene dependency to read live sequence-player status.

### First project checks

Test `TestNPC` with its working Fallback Dialogue `AmbushDialogue1`: the dialogue is transparent, the portrait has its expected size, no control strip appears, and click/Enter/A still advance. Test a multi-response choice and close/reopen the same NPC. Then test one existing sequence dialogue and one recruitment dialogue. In sequence dialogue, hold Skip at a choice and verify it stops; after selecting a response, Skip must stay stopped until released and pressed again. Confirm Auto pauses at a choice and the chosen branch runs once. After a sequence ends, talk to a normal NPC and confirm no controls or automatic playback carry over.

For the existing quest system, verify a quest-giver marker, start the quest, complete one objective and turn it in; check that the reward is delivered once and quest state survives the project's normal save/load. These are integration checks to run in the project, not claimed results from this workspace.

The native UI has not been rendered or compiled by Unreal in this workspace. Its first cold build and in-game screenshot are required integration checks. If styling is not applied, search Output Log for **LogReditusDialogueUI**; the log distinguishes an applied presentation from a widget-contract fallback.

## 1.0.2 dialogue export correction

Your actual `StoryIntegration.json` shows that `DemoDialogueWidget.Removed` has a `RecruitmentEvents` enum parameter. The NPC now supports that exact callback shape as well as the earlier zero-argument shape. It checks parameter type, count, array size and memory layout before binding, and removes only its own listener when the conversation ends. The widget and controller continue to own the recruitment result; closing the NPC conversation does not apply it a second time.

To update an installed plugin, close Unreal, copy this package's `Plugins/ReditusQuestNPC` over the installed folder, then build Development Editor / Win64 again. Keep your generated Content assets and table rows. No setup-command rerun is required for this correction.

The component remains usable for plain talking NPCs, quest-givers and quest participants. Quest state still belongs to the current controller; see `NPC_AND_DIALOGUE_PLAN.md` for the reviewed division of responsibilities.

## 1.0.1 build correction

If you already installed 1.0, close Unreal and copy this package's `Plugins/ReditusQuestNPC` over the installed folder, replacing its source files. Then build Development Editor / Win64 again. Your generated Content assets do not need to be deleted or recreated.

This corrects `FMapProperty::KeyProp` in the quest bridge and calls `GetAllGraphs` on the `UBlueprint` instance in the editor exporter. The earlier "Key cannot be used before it is initialized" error was a consequence of the incorrect map-property member name. The MSB3073 exit-code-6 entry reports that the build failed; it is not a separate source error.

## 1. Install

1. Close Unreal Editor.
2. Merge the package's `Plugins` folder into your project's `Plugins` folder, including the two updated DialoguePlugin source files. Keep the rest of your existing DialoguePlugin and MinimapPlugin installed.
3. In Visual Studio, build **Chapter1ReditusEditor**, **Development Editor / Win64** with Unreal closed. If Visual Studio has not discovered the new plugin, regenerate project files from `Chapter1Reditus.uproject`, then reopen the solution and build.
4. Reopen Unreal. Open **Window → Developer Tools → Output Log** and enter this in its console command field:

   ```text
   Reditus.CreateQuestNPC
   ```

5. Look in **Content/Reditus/Story**. You should see:

   | Asset | Purpose |
   | --- | --- |
   | `BP_QuestNPC_New` | Reusable flipbook NPC; inherits your `BP_InteractionDetector`. |
   | `DT_QuestNPCs` | NPC appearance, dialogue selection and NPC marker rules. |
   | `BP_QuestObjective_New` | A marker you place at a quest destination. |
   | `DT_QuestObjectives` | Destination quest and progress range. |

6. Use **Save All** after the new assets compile successfully.

No PowerShell script or project-header patch is needed. The command creates these four separate assets. Repeating it preserves their authored settings. It refuses to overwrite an unrelated asset at these paths.

## 2. First test: one conversation

Start without a quest so the first test isolates the NPC connection.

1. Open `DT_QuestNPCs`. Add a row called `TestNPC`.
2. Set **Display Name**, **Idle Flipbook**, and **Fallback Dialogue** to an existing working DialoguePlugin dialogue asset. Leave **Quest** empty.
3. Place `BP_QuestNPC_New` in your exploration map.
4. Select the placed actor's **QuestNPC** component. Under **Definition Row**, choose `DT_QuestNPCs` and row `TestNPC`.
5. Run the map. Approach and use your normal interaction key. The existing dialogue widget should open. Close it and interact again.

The NPC uses the inherited interaction sphere, prompt and controller interaction route. No new input binding is required. Adjust the **QuestSprite** component's position, rotation and scale in the new Blueprint if your flipbook needs it. Optional North/East/West flipbooks control facing while talking; Idle is also used for south-facing. Idle resumes when the conversation closes.

The tables start empty because this package cannot choose your project's NPC artwork or story dialogue for you. A table assigned with no row selected is an invalid definition; it does not silently use the inline settings. Alternatively, clear the table reference to use **Inline Definition** on the component.

## 3. Connect a quest

On the same NPC definition row, set **Quest** to one of your existing quest Blueprint classes. The component asks your current `BP_JRPGPlayerController.GetQuestStatus` for the state.

| Existing quest state | Dialogue slot | Default NPC map marker |
| --- | --- | --- |
| New / available | Available Dialogue | Visible |
| Active | Active Dialogue | Hidden; use a destination marker |
| Ready to deliver | Ready To Deliver Dialogue | Visible, slightly larger |
| Completed | Completed Dialogue | Hidden |
| Unavailable | Unavailable Dialogue | Hidden |

A missing state-specific dialogue uses Fallback Dialogue. The marker switches within the component's half-second refresh interval. Main quests use the quest class's existing **IsMainQuest** setting for a gold marker; side quests use a cyan marker and a different default icon. Marker Kind and Custom Marker Icon allow a visual override.

**Extra Rules** are optional. They are checked first, in array order. A rule can select a different conversation for a quest state and progress range, or refer to another quest. Minimum Progress is inclusive; Maximum Progress Exclusive is exclusive; `-1` means no upper limit. For example, 0–1 matches progress 0, and 1–2 matches progress 1. These are your existing integer quest progress values, not a second stage system.

## 4. DialoguePlugin integration

Continue authoring conversation text, replies, links, conditions and events in your existing **DialoguePlugin Dialogue assets**. The new DataTable chooses an asset; it does not duplicate the dialogue graph or replace the plugin's editor.

The NPC supplies both **InDialogue** and **NPCActor** to your existing `DemoDialogueWidget`. You can keep existing dialogue events and conditions. Events that specifically cast NPCActor to an old NPC subclass need to be adapted for this new class; battle and recruitment transitions should be tested individually.

Two additional native entries are available in the dialogue editor:

- **Reditus Quest State** condition: require a particular quest state.
- **Reditus Quest Action** event: Start Quest, Advance Quest or Turn In Quest.

Leave **Quest Override** empty to use the quest selected for this conversation. Set it explicitly for another quest. With Extra Rules, the selected rule's quest remains the conversation's context until it closes.

Put a Start Quest action on the branch the player actually accepts. Put Turn In Quest on the confirmed turn-in branch. Do not put these on a choice prompt, a decline branch, or the generic dialogue-closed notification. Do not add a new native action alongside an existing custom action that already performs the same operation.

Actions call your existing controller:

| New action | Existing controller call |
| --- | --- |
| Start Quest | `AddQuest` |
| Advance Quest | `UpdateQuest`, positive progress, manual turn-in retained |
| Turn In Quest | `DeliverQuest`, using existing rewards |

State guards reject starting an active quest and turning in an already completed quest. Advance leaves delivery to the turn-in action. For descendants of your existing `BP_ItemQuestBase`, Turn In checks `itemToCollect` and `steps`, verifies enough inventory, calls delivery, then removes the required items in the same order as your current quest NPC. It will not award a turn-in if the required inventory API or amount is missing.

Quest availability, prerequisites, rewards, persistence and the quest log remain controlled by your existing quest classes and controller. Recruitment still runs through your existing recruitment events; this package does not create another recruitment or inventory database. Closing a conversation does not start, advance or complete a quest by itself.

## 5. Mark a destination

1. Add a row to `DT_QuestObjectives` with the existing Quest, a Label, and the progress range when the destination should be marked.
2. Place `BP_QuestObjective_New` at the destination: an NPC, chest, cave entrance or other location.
3. On its **QuestObjective** component, set Definition Row to that table and row.

The marker appears only while the quest is Active and progress is in range. It disappears when progress leaves the range or the quest becomes ready/completed. Your existing event that reaches the destination, collects the item, wins the battle or selects the reply must still update quest progress. Merely overlapping this marker does not complete a quest.

These are standard MapIcon components, so they use the current minimap/full-map display and fog rules. Edge arrows are optional on objectives. This package does not add navigation paths or markers for actors in unloaded maps. NPCs and destination components each refresh on a half-second timer; there is no per-frame world actor search.

## 6. Dialogue UI and sequences

The presentation and Auto/Skip/log controls introduced in 1.1.0 are refined in 1.1.2, with the control strip limited to sequence dialogue. They wrap the current dialogue widget. Full Level Sequence skipping and extracting all gameplay logic from the old widget remain separate work.

The existing setup/export command still writes the current widget and controller graphs to:

```text
Saved/ReditusQuestNPC/StoryIntegration.json
```

To refresh just that read-only export, run `Reditus.ExportStoryIntegration`.

## Verification and limits

Offline C++14 checks compile the actual quest-bridge implementation against small typed reflection stand-ins and run its guards, enum mapping, progress rules, marker policy, item consumption and duplicate-turn-in cases with UndefinedBehaviorSanitizer. They also reject missing inventory functions, insufficient items, unexpected function inputs and unrelated quest-status enums.

An Unreal Engine installation is not available in the build workspace used to prepare this package. **The plugin has not been compiled by UHT/UnrealBuildTool or play-tested in your project.** The Visual Studio build and the one-NPC test above are the remaining integration checks. The offline tests do not prove Blueprint execution, delegate binding, rendering, input focus or save/load behavior.

If the first test fails, use the Output Log entry under `LogReditusQuestNPC` or `LogReditusQuestNPCEditor` to identify the missing asset, row, widget contract or interaction dispatcher. Test the new NPC on a copy of your map while the existing NPC remains available.

The generated assets depend on ReditusQuestNPC. Existing NPC/quest Blueprint assets, save structures, MinimapPlugin and combat source remain in place. The two DialoguePlugin native hook additions are described above; its conversation assets and playback graphs are preserved.

For developers, run the offline bridge check with Python 3 and a C++14 compiler:

```text
python3 Tests/run_checks.py
```
