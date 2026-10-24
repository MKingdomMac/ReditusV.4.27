#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "UObject/SoftObjectPath.h"
#include "Templates/SubclassOf.h"
#include "ReditusUnitStats.h"
#include "ReditusEnemyDefinition.generated.h"

class UPixel2DAnimInstance;
class UPaperZDAnimInstance;

/*
 * STATIC enemy definition data.
 * Runtime values such as current HP/stamina/phase/affinity/skills belong
 * on runtime units/components and are not stored in the DataTable.
 */

UENUM(BlueprintType)
enum class EReditusEnemyClass : uint8
{
	Robotic		UMETA(DisplayName = "Robotic"),
	Beast		UMETA(DisplayName = "Beast"),
	Insect		UMETA(DisplayName = "Insect"),
	Human		UMETA(DisplayName = "Human"),
	Bird		UMETA(DisplayName = "Bird"),
	Demon		UMETA(DisplayName = "Demon"),
	Elemental	UMETA(DisplayName = "Elemental"),
	Fabricius	UMETA(DisplayName = "Fabricius"),
	NoClass		UMETA(DisplayName = "No Class")
};

UENUM(BlueprintType)
enum class EReditusElementAffinity : uint8
{
	Phys		UMETA(DisplayName = "Phys"),
	Water		UMETA(DisplayName = "Water"),
	Earth		UMETA(DisplayName = "Earth"),
	Fire		UMETA(DisplayName = "Fire"),
	Wind		UMETA(DisplayName = "Wind"),
	Dark		UMETA(DisplayName = "Dark"),
	Holy		UMETA(DisplayName = "Holy"),
	Spirit		UMETA(DisplayName = "Spirit"),
	Lightning	UMETA(DisplayName = "Lightning"),
	All			UMETA(DisplayName = "All")
};

/**
 * Human-readable elemental relationship.
 *
 * The DataTable stores these labels instead of remembering raw numbers.
 * C++ converts them to actual combat behavior.
 */
UENUM(BlueprintType)
enum class EReditusElementResponse : uint8
{
	Normal	UMETA(DisplayName = "Normal"),
	Weak	UMETA(DisplayName = "Weak"),
	Resist	UMETA(DisplayName = "Resist"),
	Null	UMETA(DisplayName = "Null"),
	Absorb	UMETA(DisplayName = "Absorb")
};

UENUM(BlueprintType)
enum class EReditusRobotTemperament : uint8
{
	NotApplicable	UMETA(DisplayName = "Not Applicable"),
	Stoic			UMETA(DisplayName = "Stoic"),
	Aggressive		UMETA(DisplayName = "Aggressive"),
	Playful			UMETA(DisplayName = "Playful"),
	Arrogant		UMETA(DisplayName = "Arrogant"),
	Curious			UMETA(DisplayName = "Curious")
};

UENUM(BlueprintType)
enum class EReditusAnimationSystem : uint8
{
	Existing	UMETA(DisplayName = "Existing / Unspecified"),
	Pixel2D		UMETA(DisplayName = "Pixel2D"),
	PaperZD		UMETA(DisplayName = "PaperZD")
};

UENUM(BlueprintType)
enum class EReditusEnemyDropType : uint8
{
	UsableItem	UMETA(DisplayName = "Usable Item"),
	MiscItem	UMETA(DisplayName = "Misc Item"),
	Equipment	UMETA(DisplayName = "Equipment"),
	KeyItem		UMETA(DisplayName = "Key Item"),
	CraftItem	UMETA(DisplayName = "Craft Item")
};

UENUM(BlueprintType)
enum class EReditusEnemyPhaseTrigger : uint8
{
	None				 UMETA(DisplayName = "None / Manual"),
	AfterStagger		 UMETA(DisplayName = "After Stagger"),
	StaggerCountAtLeast UMETA(DisplayName = "Stagger Count At Least"),
	HPBelowPercent		 UMETA(DisplayName = "HP Below Percent"),
	TurnNumberAtLeast	 UMETA(DisplayName = "Turn Number At Least"),
	Custom				 UMETA(DisplayName = "Custom")
};

UENUM(BlueprintType)
enum class EReditusEnemyPhaseSkillMode : uint8
{
	KeepCurrent	UMETA(DisplayName = "Keep Current Skills"),
	Replace		UMETA(DisplayName = "Replace Skills"),
	Add			UMETA(DisplayName = "Add Skills")
};

USTRUCT(BlueprintType)
struct CHAPTER1REDITUS_API FReditusEnemySkillEntry
{
    GENERATED_BODY()

public:
    // DT_Skills row ID. Named entries may use legacy or generic skill execution.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skill",
        meta = (ToolTip = "DT_Skills row name. None is a normal attack only when LegacySkillClass is also empty."))
    FName SkillID = NAME_None;

    // Retained for older readers and existing serialized enemy rows.
    // The new ID path takes its class from DT_Skills.LegacySkill.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skill",
        meta = (ToolTip = "Compatibility class path. The new SkillID path uses LegacySkill from the selected DT_Skills row."))
    FSoftClassPath LegacySkillClass;

    // Relative weight, not a percentage. Non-positive weights are not selected.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skill",
        meta = (ClampMin = "0.0", ToolTip = "Relative selection weight. The new selector excludes values at or below zero."))
    float Weight = 1.0f;
};

USTRUCT(BlueprintType)
struct CHAPTER1REDITUS_API FReditusEnemyDropEntry
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop")
	FName ItemID = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop")
	FSoftClassPath LegacyItemClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop")
	EReditusEnemyDropType DropType = EReditusEnemyDropType::MiscItem;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DropChance = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop", meta = (ClampMin = "0"))
	int32 MinQuantity = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop", meta = (ClampMin = "0"))
	int32 MaxQuantity = 1;
};

USTRUCT(BlueprintType)
struct CHAPTER1REDITUS_API FReditusEnemyPhase
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phase")
	FName PhaseID = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phase")
	EReditusEnemyPhaseTrigger Trigger = EReditusEnemyPhaseTrigger::None;

	// Stagger count, HP %, or turn number depending on Trigger.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phase")
	float TriggerValue = 0.0f;

	/**
	 * If non-empty, replaces the current elemental responses on phase entry.
	 * The author selects Weak / Normal / Resist / Null / Absorb.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phase|Affinity")
	TMap<EReditusElementAffinity, EReditusElementResponse> ElementAffinityOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phase|Skills")
	EReditusEnemyPhaseSkillMode SkillMode = EReditusEnemyPhaseSkillMode::KeepCurrent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phase|Skills")
	TArray<FReditusEnemySkillEntry> Skills;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phase|AI")
	FName AIProfileID = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phase|AI")
	FSoftClassPath SpecialBehaviorClass;
};

USTRUCT(BlueprintType)
struct CHAPTER1REDITUS_API FReditusEnemyDefinition : public FTableRowBase
{
	GENERATED_BODY()

public:

	// ============================================================
	// IDENTITY
	// ============================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity", meta = (MultiLine = "true"))
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity",
		meta = (MetaClass="/Script/Engine.Actor", BlueprintBaseOnly="true", AllowAbstract="false"))
	FSoftClassPath LegacyEnemyActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity",
		meta = (AllowedClasses="/Script/Engine.Texture2D", ExactClass="true", DisplayThumbnail="true"))
	FSoftObjectPath Thumbnail;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity",
		meta = (AllowedClasses="/Script/Paper2D.PaperSprite", ExactClass="true", DisplayThumbnail="true"))
	FSoftObjectPath SpriteThumbnail;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	TArray<FName> Tags;

	// ============================================================
	// LEVEL / STATS
	// ============================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level", meta = (ClampMin = "0"))
	int32 MaxLevel = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	FReditusUnitStats InitialStats;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	FReditusUnitStats FirstLevelStats;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	FReditusUnitStats LastLevelStats;

	// ============================================================
	// CLASSIFICATION / ELEMENTAL RESPONSE
	// ============================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affinity")
	EReditusEnemyClass EnemyClass = EReditusEnemyClass::NoClass;

	/**
	 * Base/start-of-battle elemental relationship.
	 *
	 * Examples:
	 * Fire = Weak
	 * Water = Resist
	 * Earth = Normal
	 * Wind = Null
	 * Lightning = Absorb
	 *
	 * Missing entries are treated as Normal by ReditusElementDamageRules.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affinity")
	TMap<EReditusElementAffinity, EReditusElementResponse> ElementAffinity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affinity")
	EReditusElementAffinity AttackElement = EReditusElementAffinity::Phys;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affinity")
	TArray<FSoftClassPath> LegacyStatusImmunityClasses;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affinity")
	TArray<FName> StatusImmunityIDs;

	// ============================================================
	// COMBAT
	// ============================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0"))
	int32 MaxStamina = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0.0"))
	float MoveSpeed = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	bool bIsRangedUnit = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	bool bCanEnterOverdrive = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	bool bCanUseAMP = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	FName ProjectileID = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat",
		meta = (MetaClass="/Script/Engine.Actor", BlueprintBaseOnly="true", AllowAbstract="false"))
	FSoftClassPath LegacyProjectileClass;

	// ============================================================
	// BASE SKILLS / AI
	// ============================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skills")
	TArray<FReditusEnemySkillEntry> Skills;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	FName AIProfileID = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	FSoftClassPath SpecialBehaviorClass;

	// ============================================================
	// PHASES
	// ============================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phases")
	TArray<FReditusEnemyPhase> Phases;

	// ============================================================
	// REWARDS / LOOT
	// ============================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rewards", meta = (ClampMin = "0"))
	int32 ExpReward = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rewards", meta = (ClampMin = "0"))
	int32 GoldReward = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rewards")
	TArray<FReditusEnemyDropEntry> Drops;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rewards")
	FName LootTableID = NAME_None;

	// ============================================================
	// DIFFICULTY
	// ============================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty")
	bool bIgnoreDifficultyScaling = false;

	// ============================================================
	// RECRUITMENT / ROBOTS
	// ============================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recruitment")
	FName RecruitedUnitID = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recruitment",
		meta = (MetaClass="/Script/Engine.Actor", BlueprintBaseOnly="true", AllowAbstract="false"))
	FSoftClassPath LegacyRecruitedUnitClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recruitment")
	EReditusRobotTemperament RobotTemperament = EReditusRobotTemperament::NotApplicable;

	// ============================================================
	// ANIMATION
	// ============================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	EReditusAnimationSystem AnimationSystem = EReditusAnimationSystem::Existing;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	bool bUsingPaperZD = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	FName AnimationProfileID = NAME_None;

	// Match the typed animation Class References in S_PartyMemberDefinitions.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation",
		meta = (EditCondition = "AnimationSystem == EReditusAnimationSystem::Pixel2D",
			EditConditionHides, AllowAbstract = "false"))
	TSubclassOf<UPixel2DAnimInstance> Pixel2DAnimClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation",
		meta = (EditCondition = "AnimationSystem == EReditusAnimationSystem::PaperZD",
			EditConditionHides, AllowAbstract = "false"))
	TSubclassOf<UPaperZDAnimInstance> PaperZDAnimClass;

	// Keep the original name/type for saved-row and Blueprint compatibility.
	// No EditAnywhere: the row editor uses the two typed fields above instead.
	// The apply helper accepts this old value only if its animation type matches.
	UPROPERTY(BlueprintReadWrite, Category = "Animation|Compatibility",
		meta = (ToolTip = "Compatibility animation path retained from existing rows. Use Pixel2DAnimClass or PaperZDAnimClass for new edits."))
	FSoftClassPath LegacyBattleAnimationBlueprintClass;

	// Vertical visual offset applied to the Pixel2D component during initialization.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation",
		meta = (DisplayName = "Pixel2D Z Offset", Units = "cm",
			ToolTip = "Vertical offset from the Pixel2D component's authored relative Z. Positive raises the visual; negative lowers it. Apply during enemy initialization."))
	float Pixel2DZOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile Sockets")
	FName ProjectileSocketName1 = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile Sockets")
	FName ProjectileSocketName2 = NAME_None;
};
