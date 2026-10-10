# Run inside Unreal Engine 4.27 Editor with Python Editor Script Plugin and
# Editor Scripting Utilities enabled. This script only reads Blueprint defaults.
import csv
import os
import re
import unreal

SKILLS = [('DoubleHit', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/BP_DoubleHit.BP_DoubleHit'), ('FireBall', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/BP_FireBall.BP_FireBall'), ('FocusAttack', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/BP_FocusAttack.BP_FocusAttack'), ('FullHeal', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/BP_FullHeal.BP_FullHeal'), ('GreatHeal', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/BP_GreatHeal.BP_GreatHeal'), ('Lockpicking', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/BP_Lockpicking.BP_Lockpicking'), ('MeteorStorm', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/BP_MeteorStorm.BP_MeteorStorm'), ('Thunderbolt', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/BP_Thunderbolt.BP_Thunderbolt'), ('TrueStrike', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/BP_TrueStrike.BP_TrueStrike'), ('Amp', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/BuffSkills/BP_Amp.BP_Amp'), ('CombatRedeployment', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/BuffSkills/BP_CombatRedeployment.BP_CombatRedeployment'), ('ProtectOtherBuff', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/BuffSkills/BP_ProtectOtherBuff.BP_ProtectOtherBuff'), ('ProtectSelfBuff', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/BuffSkills/BP_ProtectSelfBuff.BP_ProtectSelfBuff'), ('BoarCharge', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/EnemySkills/BanditCave/BP_BoarCharge.BP_BoarCharge'), ('Gut', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/EnemySkills/BanditCave/BP_Gut.BP_Gut'), ('HeadBlast', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/EnemySkills/BanditCave/BP_HeadBlast.BP_HeadBlast'), ('Howl', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/EnemySkills/BanditCave/BP_Howl.BP_Howl'), ('Lick', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/EnemySkills/BanditCave/BP_Lick.BP_Lick'), ('LifeDrain', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/EnemySkills/BanditCave/BP_LifeDrain.BP_LifeDrain'), ('MacyDoubleSlash', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/EnemySkills/BanditCave/BP_MacyDoubleSlash.BP_MacyDoubleSlash'), ('SpinSlam', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/EnemySkills/BanditCave/BP_SpinSlam.BP_SpinSlam'), ('SpreadDart', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/EnemySkills/BanditCave/BP_SpreadDart.BP_SpreadDart'), ('WindVortex', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/EnemySkills/BanditCave/BP_WindVortex.BP_WindVortex'), ('Consume', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/EnemySkills/DeepHeremus/BP_Consume.BP_Consume'), ('SleepFang', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/EnemySkills/DeepHeremus/BP_SleepFang.BP_SleepFang'), ('StirUp', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/EnemySkills/DeepHeremus/BP_StirUp.BP_StirUp'), ('Swallow', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/EnemySkills/DeepHeremus/BP_Swallow.BP_Swallow'), ('BlackSpray', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/EnemySkills/Heremus/BP_BlackSpray.BP_BlackSpray'), ('BoarBomb', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/EnemySkills/Heremus/BP_BoarBomb.BP_BoarBomb'), ('BowShot', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/EnemySkills/Heremus/BP_BowShot.BP_BowShot'), ('ColdBlast', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/EnemySkills/Heremus/BP_ColdBlast.BP_ColdBlast'), ('ColdBlastPlayerUnitVersion', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/EnemySkills/Heremus/BP_ColdBlastPlayerUnitVersion.BP_ColdBlastPlayerUnitVersion'), ('HeadWaves', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/EnemySkills/Heremus/BP_HeadWaves.BP_HeadWaves'), ('RollyRollAttack', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/EnemySkills/Heremus/BP_RollyRollAttack.BP_RollyRollAttack'), ('ScourgeTaunt', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/EnemySkills/Heremus/BP_ScourgeTaunt.BP_ScourgeTaunt'), ('Spawn', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/EnemySkills/Heremus/BP_Spawn.BP_Spawn'), ('SharkAttack', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/EnemySkills/Quicksand/BP_SharkAttack.BP_SharkAttack'), ('BasicHeal', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/HealingSkills/BP_BasicHeal.BP_BasicHeal'), ('Heal2', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/HealingSkills/BP_Heal2.BP_Heal2'), ('Inoculate', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/HealingSkills/BP_Inoculate.BP_Inoculate'), ('Refresh', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/HealingSkills/BP_Refresh.BP_Refresh'), ('RestorativeAura', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/HealingSkills/BP_RestorativeAura.BP_RestorativeAura'), ('RestorativeAuraForEnemy', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/HealingSkills/BP_RestorativeAuraForEnemy.BP_RestorativeAuraForEnemy'), ('Analyze', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/HeroSkills/Airloft/BP_Analyze.BP_Analyze'), ('Hack', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/HeroSkills/Airloft/Bp_Hack.Bp_Hack'), ('SkillHitCounter', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/HeroSkills/Airloft/BP_SkillHitCounter.BP_SkillHitCounter'), ('PoisonDartPlayer', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/HeroSkills/ArmorSkills/BP_PoisonDartPlayer.BP_PoisonDartPlayer'), ('ReaverDart', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/HeroSkills/ArmorSkills/BP_ReaverDart.BP_ReaverDart'), ('BazookaSkill', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/HeroSkills/Augustus/Bp_BazookaSkill.Bp_BazookaSkill'), ('EmpGrenadeSkill', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/HeroSkills/Augustus/BP_EmpGrenadeSkill.BP_EmpGrenadeSkill'), ('EMPMaximize', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/HeroSkills/Augustus/BP_EMPMaximize.BP_EMPMaximize'), ('FleshBulletSkill', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/HeroSkills/Augustus/BP_FleshBulletSkill.BP_FleshBulletSkill'), ('FocusShot', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/HeroSkills/Augustus/BP_FocusShot.BP_FocusShot'), ('SaltBulletSkill', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/HeroSkills/Augustus/BP_SaltBulletSkill.BP_SaltBulletSkill'), ('PlayerTaunt', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/HeroSkills/BP_PlayerTaunt.BP_PlayerTaunt'), ('SpreadDartPlayerVer', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/HeroSkills/BP_SpreadDartPlayerVer.BP_SpreadDartPlayerVer'), ('SpreadShot', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/HeroSkills/Caeser/Bp_SpreadShot.Bp_SpreadShot'), ('TargetSpot', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/HeroSkills/Caeser/BP_TargetSpot.BP_TargetSpot'), ('Fire3', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/HeroSkills/Odin/Bp_Fire3.Bp_Fire3'), ('Ice3', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/HeroSkills/Odin/Bp_Ice3.Bp_Ice3'), ('Lit3', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/HeroSkills/Odin/Bp_Lit3.Bp_Lit3'), ('AirloftAoeFireOverdrive', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/Overdrives/BP_AirloftAoeFireOverdrive.BP_AirloftAoeFireOverdrive'), ('AugustusBlindRayOverdrive', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/Overdrives/BP_AugustusBlindRayOverdrive.BP_AugustusBlindRayOverdrive'), ('CaeserSlamOverdrive', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/Overdrives/BP_CaeserSlamOverdrive.BP_CaeserSlamOverdrive'), ('MacyOverdrive', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/Overdrives/BP_MacyOverdrive.BP_MacyOverdrive'), ('MacyOverdrivePlayerCharacter', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/Overdrives/BP_MacyOverdrivePlayerCharacter.BP_MacyOverdrivePlayerCharacter'), ('OdinUltimaOverdrive', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/Overdrives/BP_OdinUltimaOverdrive.BP_OdinUltimaOverdrive'), ('AirloftCaeserSlamOverdrive', '/Game/TurnBasedJRPGTemplate/Blueprints/Skills/Overdrives/ComboOverdrives/BP_AirloftCaeserSlamOverdrive.BP_AirloftCaeserSlamOverdrive')]

FIELDS = (
    ('DisplayName', 'name'),
    ('ShortDescription', 'shortDescription'),
    ('LongDescription', 'longDescription'),
    ('MPCost', 'manaRequired'),
)

def read_value(default_object, property_name):
    snake_case = re.sub(r'(?<!^)(?=[A-Z])', '_', property_name).lower()
    last_error = ''
    for candidate in (property_name, snake_case):
        try:
            value = default_object.get_editor_property(candidate)
            return ('' if value is None else str(value), '')
        except Exception as exc:
            last_error = str(exc)
    return ('', property_name + ': ' + last_error)

output_path = os.path.join(unreal.Paths.project_saved_dir(), 'Reditus_Skill_Defaults_Review.csv')
rows = []
for skill_id, asset_path in SKILLS:
    row = {'SkillRowName': skill_id, 'BlueprintAsset': asset_path, 'Error': ''}
    for column, _ in FIELDS:
        row[column] = ''
    try:
        skill_class = unreal.EditorAssetLibrary.load_blueprint_class(asset_path)
        if skill_class is None:
            raise RuntimeError('Blueprint class could not be loaded')
        default_object = unreal.get_default_object(skill_class)
        if default_object is None:
            raise RuntimeError('Class default object could not be loaded')
        errors = []
        for column, property_name in FIELDS:
            row[column], error = read_value(default_object, property_name)
            if error:
                errors.append(error)
        row['Error'] = '; '.join(errors)
    except Exception as exc:
        row['Error'] = str(exc)
    rows.append(row)

columns = ['SkillRowName', 'BlueprintAsset'] + [column for column, _ in FIELDS] + ['Error']
with open(output_path, 'w', newline='', encoding='utf-8-sig') as output:
    writer = csv.DictWriter(output, fieldnames=columns)
    writer.writeheader()
    writer.writerows(rows)

failures = sum(bool(row['Error']) for row in rows)
unreal.log('Exported {} skills ({} with errors) to {}'.format(len(rows), failures, output_path))
