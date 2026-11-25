#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataTable.h"
#include "ReditusEnemyCombatAIComponent.generated.h"

class AActor;

UENUM(BlueprintType)
enum class EReditusEnemyAISkillCondition : uint8
{
    RequireStatus UMETA(DisplayName="Require Status"),
    SkipWhileStatusActive UMETA(DisplayName="Skip While Status Is Active"),
    RequireLowHP UMETA(DisplayName="Require HP At or Below Percent")
};

UENUM(BlueprintType)
enum class EReditusEnemyAISubject : uint8
{
    Self,
    LivingAllies UMETA(DisplayName="Living Allies (Including Self)"),
    OtherLivingAllies,
    LivingOpponents,
    KnockedOutAllies,
    KnockedOutOpponents,
    SwallowedAllies,
    SwallowedOpponents
};

UENUM(BlueprintType)
enum class EReditusEnemyAIComparison : uint8
{
    AtLeast,
    AtMost,
    Exactly
};

UENUM(BlueprintType)
enum class EReditusEnemyAITestType : uint8
{
    StatusPresent,
    StatusMissing,
    Staggered,
    NotStaggered,
    HPPercent UMETA(DisplayName="HP Percent"),
    MPPoints UMETA(DisplayName="MP Points"),
    MPPercent UMETA(DisplayName="MP Percent"),
    StaminaPoints,
    StaminaPercent,
    AmpPoints UMETA(DisplayName="Available AMP Points"),
    OverdrivePercent,
    ATBGaugePercent UMETA(DisplayName="ATB Gauge Percent"),
    CompletedATBCycles UMETA(DisplayName="Completed ATB Cycles"),
    NumericProperty UMETA(DisplayName="Custom Numeric Property"),
    ATBReady UMETA(DisplayName="ATB Ready"),
    ATBActionPending UMETA(DisplayName="ATB Preparing or Queued")
};

UENUM(BlueprintType)
enum class EReditusEnemyAIRuleEffect : uint8
{
    RequireMatch UMETA(DisplayName="Use Only When Conditions Match"),
    SkipOnMatch UMETA(DisplayName="Skip When Conditions Match"),
    AdjustWeight UMETA(DisplayName="Change Selection Weight When Conditions Match")
};

UENUM(BlueprintType)
enum class EReditusEnemyAIAmpPolicy : uint8
{
    UseProfile,
    Disabled,
    Enabled
};

UENUM(BlueprintType)
enum class EReditusEnemyAIAmpAction : uint8
{
    RegularAttack,
    Skill
};

USTRUCT(BlueprintType)
struct REDITUSTABLEBUFFS_API FReditusEnemyAIUnitTest
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Condition")
    EReditusEnemyAITestType Type = EReditusEnemyAITestType::HPPercent;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Condition")
    EReditusEnemyAIComparison Comparison = EReditusEnemyAIComparison::AtMost;

    /** Percent tests use 0 to 100. Point and cycle tests use raw values. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Condition")
    float Value = 50.f;

    /** Optional internal Blueprint Boolean name for a status test. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Status")
    FName StatusFlag = NAME_None;

    /** Optional active native DT_Buffs row. A true flag or this buff counts as present. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Status")
    FName StatusBuffID = NAME_None;

    /** Internal Blueprint property name for a custom integer or floating-point test. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Condition")
    FName PropertyName = NAME_None;
};

USTRUCT(BlueprintType)
struct REDITUSTABLEBUFFS_API FReditusEnemyAIUnitCheck
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Conditions")
    EReditusEnemyAISubject Subject = EReditusEnemyAISubject::Self;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Conditions")
    EReditusEnemyAIComparison CountComparison = EReditusEnemyAIComparison::AtLeast;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Conditions", meta=(ClampMin="0"))
    int32 Count = 1;

    /** Each counted unit must pass every test. An empty list counts the entire subject group. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Conditions", meta=(TitleProperty="Type"))
    TArray<FReditusEnemyAIUnitTest> AllTests;
};

USTRUCT(BlueprintType)
struct REDITUSTABLEBUFFS_API FReditusEnemyAIConditionGroup
{
    GENERATED_BODY()

    /** Every check in this situation must pass. An empty group never matches. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Conditions", meta=(TitleProperty="Subject"))
    TArray<FReditusEnemyAIUnitCheck> AllChecks;
};

USTRUCT(BlueprintType)
struct REDITUSTABLEBUFFS_API FReditusEnemyAISkillRule
{
    GENERATED_BODY()

    /** The existing DT_Skills row name for a table skill. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Skill")
    FName SkillID = NAME_None;

    /** The existing skill Blueprint class for a class-based skill. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Skill")
    TSubclassOf<AActor> SkillClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Condition")
    EReditusEnemyAISkillCondition Condition = EReditusEnemyAISkillCondition::RequireStatus;

    /** Internal name of a Boolean on the enemy, such as IsProtected or Is DOT. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Condition")
    FName StatusFlag = NAME_None;

    /** Optional active DT_Buffs row on the enemy. A flag or this buff can satisfy the status. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Condition")
    FName StatusBuffID = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Condition", meta=(ClampMin="0.0", ClampMax="100.0"))
    float HPPercent = 30.f;

    /** Optional alternatives. When populated, uses this list and Condition Effect for this rule. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Condition Groups")
    TArray<FReditusEnemyAIConditionGroup> AnyOf;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Condition Groups")
    EReditusEnemyAIRuleEffect ConditionEffect = EReditusEnemyAIRuleEffect::RequireMatch;

    /** Applied by Adjust Weight. Eligibility and MP checks still apply. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Condition Groups", meta=(ClampMin="0.0", ClampMax="100.0"))
    float WeightMultiplier = 2.f;
};

USTRUCT(BlueprintType)
struct REDITUSTABLEBUFFS_API FReditusEnemyAIAmpRule
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AMP")
    EReditusEnemyAIAmpAction Action = EReditusEnemyAIAmpAction::RegularAttack;

    /** For Skill rules. Leave both skill fields empty to match every skill. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AMP")
    FName SkillID = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AMP")
    TSubclassOf<AActor> SkillClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AMP", meta=(ClampMin="1"))
    int32 Points = 1;

    /** Allow the rule to use available AMP when it has fewer points than requested. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AMP")
    bool bSpendUpToAvailable = false;

    /** An empty list allows the rule whenever its action and AMP amount match. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AMP")
    TArray<FReditusEnemyAIConditionGroup> AnyOf;
};

USTRUCT(BlueprintType)
struct REDITUSTABLEBUFFS_API FReditusEnemyAIProfile : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy AI")
    TArray<FReditusEnemyAISkillRule> SkillRules;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy AI|AMP")
    bool bEnableAMP = false;

    /** The first eligible AMP rule supplies the amount for this action. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy AI|AMP", meta=(TitleProperty="Action"))
    TArray<FReditusEnemyAIAmpRule> AmpRules;
};

UCLASS(ClassGroup=(Reditus), meta=(BlueprintSpawnableComponent, DisplayName="Reditus Enemy Combat AI"))
class REDITUSTABLEBUFFS_API UReditusEnemyCombatAIComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UReditusEnemyCombatAIComponent();

    /** Optional class-specific profile. Takes priority over the enemy definition's AI Profile ID. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy AI")
    FDataTableRowHandle AIProfile;

    /** Rules set directly on this class. Matching profile and class rules all apply. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy AI")
    TArray<FReditusEnemyAISkillRule> LocalSkillRules;

    /** Per-class override of the selected profile's opt-in. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy AI|AMP")
    EReditusEnemyAIAmpPolicy AmpPolicy = EReditusEnemyAIAmpPolicy::UseProfile;

    /** Matching local rules take priority for that move, including when they are saving AMP. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy AI|AMP", meta=(TitleProperty="Action"))
    TArray<FReditusEnemyAIAmpRule> LocalAmpRules;

    /** DT_EnemyAIProfiles, used to resolve DT_Enemies' AI Profile ID. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy AI|Data Tables")
    UDataTable* ProfileTable = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy AI|Data Tables")
    UDataTable* EnemyDefinitions = nullptr;

    /** The existing Blueprint call forwards here for both attacks and single-target skills. */
    static int32 ResolveTargetIndex(AActor* BattleController, AActor* Selector, int32 LegacyIndex);

    /** Compatibility for older callers that stored currentSkill on the enemy actor. */
    UFUNCTION(BlueprintCallable, Category="Reditus|Enemy AI")
    static bool TryChooseProfiledClassSkill(AActor* Enemy);

    /** When a rule handled selection, returns true and outputs the chosen class, or None for a regular attack. */
    UFUNCTION(BlueprintCallable, Category="Reditus|Enemy AI")
    static bool TrySelectProfiledClassSkill(AActor* Enemy, TSubclassOf<AActor>& SelectedSkill);

    /** Filters the existing ID-based selector's candidates before its affordability check and roll. */
    UFUNCTION(BlueprintPure, Category="Reditus|Enemy AI")
    static float FilterEnemySkillIDWeight(AActor* Enemy, FName SkillID, float OriginalWeight);

    /** Read by the ATB component when a regular attack or skill is submitted. */
    UFUNCTION(BlueprintPure, Category="Reditus|Enemy AI")
    int32 ResolveEnemyAmpForAction(bool bRegularAttack, FName SkillID, TSubclassOf<AActor> SkillClass) const;

private:
    static bool SelectClassSkill(AActor* Enemy, const UClass* SkillType, TSubclassOf<AActor>& SelectedSkill);
    const uint8* FindEnemyDefinition() const;
    const FReditusEnemyAIProfile* FindProfile() const;
    bool MeetsSkillCondition(const FReditusEnemyAISkillRule& Rule) const;
    float GetRuleAdjustedWeight(const FReditusEnemyAIProfile* Profile, FName SkillID, const UClass* SkillClass,
        float OriginalWeight, bool& bMatchedRule) const;
};
