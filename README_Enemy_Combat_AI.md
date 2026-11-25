# Reditus Enemy Combat AI for Unreal Engine 4.27

The C++ component lets each class-based or DataTable enemy check the current battle before choosing among its existing skills. You can require a situation, skip a skill in that situation, or change its selection weight. A situation can combine conditions on the caster, its allies, or the player party. You can also opt an enemy in to AMP for individual attacks and skills.

## Install or update

1. Close Unreal. Extract this ZIP beside `Chapter1Reditus.uproject`.
2. Open PowerShell or Git Bash in that folder and apply the patch for your current source.

   | Current project | Command |
   |---|---|
   | `ReditusTableBuffs` with stagger targeting | `git apply --ignore-space-change Reditus_Enemy_Combat_AI_UE427.patch` |
   | The first Enemy Combat AI patch is already applied | `git apply --ignore-space-change Reditus_Enemy_Combat_AI_UE427_Upgrade.patch` |
   | The expanded patch and AMP particle fix are already applied | `git apply --ignore-space-change Reditus_Enemy_Combat_AI_UE427_AmpPolicyFix.patch` |
   | The expanded patch still needs one of the older fixes | Apply any missing fixes in this order. `BuildFix`, `ClassSkillFix`, `AmpParticleFix`, then `AmpPolicyFix`. Use `git apply --ignore-space-change` for each. |

3. Rebuild `Chapter1ReditusEditor` in **Development Editor \| Win64** and reopen Unreal.
4. On a first install or after applying the class skill fix, stop Play and enter `Reditus.InstallEnemyCombatAI` in **Window > Developer Tools > Output Log**. Save `BP_EnemyUnitBase` and the profile table. This connects the selectors and adds any missing example rows.
5. Open `DT_EnemyAIProfiles` in the same folder as `DT_Enemies`. If you assigned a custom **Profile Table** to the component, open that table instead.

The patches are based on the exported `ReditusTableBuffs` source. Git applies matching changes and preserves unrelated edits. The editor command checks the existing `GetRandomSkill` and `GetRandomSkillByID` graphs before it connects them. If either step reports a mismatch, the current source file or Blueprint function export identifies what needs adapting.

## Open a worked example

| Row | Where to look |
|---|---|
| `Example_ProtectSelf` | Expand **Skill Rules**. The first rule allows `ProtectSelfBuff` when **both** self HP and stamina are at or below 50%, **or** when at least two opponents are alive and available. The second skips it while `IsProtected` or the `Protect` buff is active. The third triples its weight at or below 25% HP if eligible. |
| `Example_SelfHeal` | Choose an existing self-heal in **Skill Class** or **Skill ID**. Expand **Any Of → All Checks → All Tests** to see `Self`, `HP Percent`, `At Most`, `40`. |
| `Example_StaggerFinisher` | Choose an existing single-target finishing skill. The group requires a staggered opponent and zero opponents with active `AggroFocus` (Rage Bait). This uses the normal hostile single-target priority to take advantage of the stagger. |
| `Example_RegularAttackAMP` | Expand **Amp Rules** to see one rule for **Regular Attack** with **Points** set to `1`. **Enable AMP** is checked on this example profile. |

For a class-based enemy, select the inherited **EnemyCombatAI** component in its Blueprint and set **AI Profile**. For example, `BP_LizardMan` can use `LizardMan` or the more conditional `Example_ProtectSelf`. Every NPC spawning that class uses its profile. For an enemy spawned by row, enter the profile row name in **AI Profile ID** in `DT_Enemies`. The component reads the enemy's initialized `Enemy ID` and **Profile Table** assignment. You can also add **Local Skill Rules** on a class.

## Compose a rule

1. Keep the move in the enemy's existing `Skills` or `Enemy Skill Weights by ID` list with a positive weight. On the profile rule, choose its **Skill Class** or **Skill ID**. Fill both to match both selection paths.
2. Choose **Use Only When Conditions Match**, **Skip When Conditions Match**, or **Change Selection Weight When Conditions Match**.
3. Add a situation under **Any Of**. Add one more entry for each alternative. A match in any one of those entries is enough.
4. Inside a situation, each entry in **All Checks** is required. Set **Subject**, **Count Comparison**, and **Count**. Within that check, one unit must pass all **All Tests** to count. Leave **All Tests** empty to count the whole subject group.

For example, detecting an injured, poisoned ally takes one **Other Living Allies** check with **Count At Least 1** and two tests. The tests are `Status Present` with `Is DOT`, and `HP Percent At Most 40`. You can add a separate **Self** check for `MP Points At Least 8` to the same situation. A second situation could allow the spell whenever two allies are KO'd, if that spell also revives.

With **Any Of** empty, the original simple-rule fields apply. All rules that match a skill apply together. A veto or unmet requirement removes the skill. Matching weight changes multiply together. An adjustment of 3 triples the weight in the existing roll, while 0.5 halves it. An eligible move still needs enough MP. Use **Mana Required** on a legacy skill class or **MP Cost** in `DT_Skills` to set the charge made by the existing ATB execution.

## Choose AMP per enemy and per move

1. Turn on **Enable AMP** on the profile you assign to that enemy. A class can also set its inherited component's **Amp Policy** to **Enabled** and add **Local Amp Rules**, or set the class policy to **Disabled**. **Use Profile** follows the assigned profile.
2. Add an entry to **Amp Rules** or **Local Amp Rules**. Set **Action** to **Regular Attack** or **Skill**. For a specific skill, select its **Skill Class** or **Skill ID**. Both skill fields left empty on a Skill rule match every skill.
3. Set **Points**, for example `2`. Leave **Spend Up To Available** unchecked to require the full amount when choosing the move. Check it to allow a smaller amount when the enemy has some AMP. Rules can use the same **Any Of** condition groups. If a move matches any local rules, the first eligible local rule supplies AMP. When those rules are waiting, the move uses zero AMP. Moves with no matching local rule use the assigned profile.

To save up to a cap of **3**, set **Points** to **3** and leave **Spend Up To Available** unchecked. For the class-spawned Gamacia Knight, a local three-point regular-attack rule holds AMP even when `Example_RegularAttackAMP` is assigned. On a table-spawned enemy, assign an AI profile with three-point rules for the moves you want to amp.

The enemy AI component supplies the action's `AmpCommitted` amount when the unit submits its move. **Disabled**, an unmet rule, or an action other than an attack or skill gives that enemy action zero AMP. ATB charges approved points at dispatch and refunds them if validation rejects the move. If AMP drops below the chosen amount before dispatch, the move goes ahead at its ordinary strength. **Easy Difficulty** and `DT_Enemies` **Can Use AMP** also govern enemy eligibility.

When an enemy spends AMP from a rule, the existing Nebula particle plays on its sprite at action start. The effect uses the one-, two-, or three-point variant and is cleared when the action ends or is cancelled.

For example, choose one enemy and set **Regular Attack → 1 AMP**. On a different enemy, add **Skill → 2 AMP** for a specific existing finisher, with one situation requiring a staggered opponent and zero opponents with active Rage Bait. Both use the normal combat execution. The DataTable skill still has to meet its MP cost.

## Reditus combinations you can build

The Reditus combat exports show separate stamina and HP, ATB preparation states, per-unit AMP and Overdrive totals, legacy status flags, and active table buffs. Point these rules at skills that already perform the effect you want.

| Situation | Settings to combine |
|---|---|
| Heal multiple wounded allies | **Living Allies (Including Self)**, **Count At Least 2**, **HP Percent At Most 40**. Pair it with an existing group heal. Add a separate **Self MP Points At Least** check if the enemy should keep an MP reserve. |
| Revive an ally | **Knocked Out Allies**, **Count At Least 1**. Choose a skill whose existing targeting accepts KO'd allies. |
| Cleanse under pressure | **Other Living Allies**, **Count At Least 1**, `Is DOT` present and **HP Percent At Most 40** in the same check. Or check the caster for `Is Blind?` and favor a cleanse if the enemy relies on physical hits. |
| Apply poison or Regen selectively | Choose Boar Bomb or your poison skill when a living opponent is missing `Is DOT`. Weight Regen higher when the caster is hurt and `IsRegen` or the `Regen` row is missing. |
| Pressure a player near a stamina break | Increase an existing stamina-damaging skill's weight when a **Living Opponents** has **Stamina Points At Most 1** and **Not Staggered**. Use `Example_StaggerFinisher` for an expensive follow-up against an already-staggered unit. |
| React to an incoming ATB action | Require a **Living Opponents** in **ATB Preparing or Queued** to consider a skill that already interrupts pending actions. Use **ATB Gauge Percent At Least 85** or **ATB Ready** to favor a defensive skill under pressure. |
| Disrupt AMP | Consider an existing AMP Block skill if an opponent has **Available AMP Points At Least 2** and **Status Missing** for `AmpBlock` on the same unit. Count two qualifying opponents for a group skill. |
| React to a nearly full Overdrive gauge | Raise the weight of a protective move if an opponent has **Overdrive Percent At Least 90** and the caster is missing `Protect`. Those are two checks in one situation. |
| Respond to Swallow | Use **Swallowed Opponents** to require at least one captured player, or **Exactly 0** to keep a Swallow skill for when nobody is captured. Regular living scopes count units available to fight. |
| Change the skill mix later in battle | Require **Completed ATB Cycles At Least 3** and **HP Percent At Most 35** on the caster, or use one of those conditions as an independent alternative. For class spawns, you can bind the profile once to the enemy class. For table spawns, you can assign different profiles to `DeathBoar` and `DeathBoarTutorial`. |

Use **Status Flag** for an internal Blueprint Boolean such as `IsProtected`, `Is DOT`, `IsRegen`, `Is Blind?`, `IsExhausted`, `IsTaunted`, `Is Confused`, or `IsCharmed??`. Use **Status Buff ID** for an active native row such as `Protect`, `AggroFocus`, or `AmpBlock`. **Staggered** uses Reditus's stamina-break check. Built-in numeric tests include HP, MP, stamina, AMP, Overdrive, and ATB. **Custom Numeric Property** reads an existing number on the unit by its internal name, such as `speed`.

The profile evaluates eligibility when an enemy chooses a skill. The skill's targeting settings and Reditus's normal target selectors still determine the recipient. Rage Bait has priority over stagger for ordinary hostile single-target selection. An AMP or Overdrive condition reads the gauge. Regular attack and Skill AMP rules can allocate AMP to that one action. Permanent weakness changes are authored in the existing enemy phase data and require the battle's phase hookup.

## Verify in a battle

Test `Example_SelfHeal` with an affordable self-heal at 41% and 40% HP. Then test it at 40% without enough MP. For the Protect example, test outnumbering and low stamina separately, then enable Protect to check the veto. For the finisher, test a staggered party member before and after placing Rage Bait on a healthy one. Assign the AMP example to one enemy, verify the available points drop by one on its regular attack, and interrupt an action before it starts to check that its AMP is still available.

The offline tests exercise the actual rule and seed methods with Unreal stand-ins. Run the Windows UE 4.27 build and an in-editor battle to verify the live assets.
