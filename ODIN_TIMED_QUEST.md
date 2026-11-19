# Odin’s race to Gamacia

Odin receives the mission in the existing briefing. The countdown starts when he gets control in `PrologueMap`. Reaching Gamacia in time gives him the quest’s item and records **Completed**. The deadline records **Failed** if time runs out first.

## 1. Create the quest in the existing graph editor

In the Content Browser, create **Reditus → Reditus Quest Graph** and name it `Q_OdinReachGamacia`. Keep the default **Start Quest → Stage → Ready for Turn-in** connections.

| Where | Field | Value |
| --- | --- | --- |
| Stage | Description | Reach Gamacia before his reinforcements arrive. |
| Stage → Objectives → [0] | Kind | Reach Location |
| Stage → Objectives → [0] | Objective ID | `ReachGamacia` |
| Stage → Objectives → [0] | Target ID | `OdinPrologueArrival` |
| Stage → Objectives → [0] | Required Count | 1 |
| Stage → Objectives → [0] | Optional | Unchecked |
| Stage → Timer | Mode | Deadline |
| Stage → Timer | Duration Seconds | Your time limit in seconds. Use 30 for a quick test. |
| Stage → Timer | Count Up | Unchecked |
| Stage → Timer | Show On Screen | Checked |
| Stage → Timer | Label | Reinforcements arrive in |
| Quest Settings → Settings | Title | Reach Gamacia |
| Quest Settings → Settings | Is Main Quest | Checked to list Reach Gamacia under Main Quests. |
| Quest Settings → Settings → Rewards → Items | Item Row | Pick the actual item in your existing inventory table. |
| Quest Settings → Settings → Rewards → Items | Amount | 1 |

Leave **Turn In Items** empty. Use the game’s existing item tables, such as `DT_UsableItems`, `DT_MiscItems`, `DT_KeyItems`, or `DT_Equipment`. Keep gold and experience at zero for an item-only reward.

Click **Compile & Register** and **Save**. **Open Catalogue** should show `Q_OdinReachGamacia` in `DT_QuestCatalog`.

## 2. Start the clock when Odin gets control

In the existing `PrologueMap` Blueprint event that finishes initialization and gives Odin gameplay control, add **Start Quest** with **Get Quest System** as the Target. Set **Quest** to a **Make Data Table Row Handle** with **Data Table** `DT_QuestCatalog` and **Row Name** `Q_OdinReachGamacia`. Keep **Ignore Prerequisites** unchecked.

Place this after any quest save restore. The existing DeusSilentii scene finish event takes the player to `PrologueMap` whether the briefing plays through or is skipped. Starting the quest at the gameplay handoff gives Odin the full timer. Repeated calls preserve the current quest state and remaining time.

In play, you should see the quest added and its countdown when Odin can move. Play one of your existing Level Sequences while the quest is active and check that the displayed time stays fixed during playback and pauses for its dialogue. It should resume when the scene finishes. Quest graph sequences use their **Pause Quest Timers** setting, which defaults to checked.

## 3. Finish at the Gamacia trigger

Use your existing arrival trigger Blueprint, or create an Actor Blueprint with a Box Collision component at the destination. Add **Reditus Quest Objective** to it. In that component’s **Inline Definition**, select **Quest** `DT_QuestCatalog / Q_OdinReachGamacia` and set **Objective ID** to `ReachGamacia`. Leave **Stage ID** blank for this single-stage quest. Check **Turn In Quest on Arrival**.

On the box’s **On Component Begin Overlap**, compare **Other Actor** to **Get Player Pawn**, index 0. Connect the Branch’s **True** output to **Report Objective Reached** on your Reditus Quest Objective component. Set **Unique Event ID** to `OdinPrologueArrival`. If the trigger also starts an arrival sequence, report the objective before starting playback.

On a fresh run, reach the box with time left. The clock should disappear, the item should increase by one, and the quest should show **Completed**. Walk out and back in to check the item stays at one. On another fresh run, let the clock expire before entering. The quest should show **Failed** and the reward count should stay the same.

## 4. Use the outcome in your later dialogue

In your later dialogue tree, add **Reditus Quest State** to the branch conditions. In each branch, set **Quest Row Override** to `DT_QuestCatalog / Q_OdinReachGamacia`.

| Later line | Required State |
| --- | --- |
| Odin arrived before the reinforcements | Completed |
| The reinforcements arrived first | Failed |

Leave **Required Stage ID** blank. These conditions read the quest state, which is included in the existing Reditus quest save data.
