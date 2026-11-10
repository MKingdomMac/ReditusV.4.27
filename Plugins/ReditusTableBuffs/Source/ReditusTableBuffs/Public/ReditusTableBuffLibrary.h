#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ReditusBuffVisualLibrary.h"
#include "UObject/SoftObjectPtr.h"
#include "ReditusTableBuffLibrary.generated.h"

class AActor;
class FProperty;
class UActorComponent;
class UParticleSystem;
class USoundBase;

UENUM(BlueprintType)
enum class EReditusTableBuffEffect : uint8
{
    Unwired,
    PhysicalDefense,
    GaugeRate,
    PreparationRate,
    PeriodicDamage,
    LegacyBlueprint,
    PeriodicHealing,
    PeriodicMP,
    AggroFocus
};

UENUM(BlueprintType)
enum class EReditusLegacyBuffDuration : uint8
{
    ClassDefault,
    OwnerCycles,
    UntilCured
};

/** Authoring notes imported from the design CSV. These are not runtime rules. */
USTRUCT(BlueprintType)
struct REDITUSTABLEBUFFS_API FReditusBuffCatalogueDesign
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category="Catalogue")
    bool bDesignOnly = false;

    UPROPERTY(EditAnywhere, Category="Catalogue")
    FName Category;

    UPROPERTY(EditAnywhere, Category="Catalogue")
    FText ProposedBehavior;

    UPROPERTY(EditAnywhere, Category="Timing")
    FName DurationMode;

    UPROPERTY(EditAnywhere, Category="Timing")
    float Duration = 0.0f;

    UPROPERTY(EditAnywhere, Category="Timing")
    FName ReapplyPolicy;

    UPROPERTY(EditAnywhere, Category="Timing")
    int32 MaxStacks = 1;

    UPROPERTY(EditAnywhere, Category="Timing")
    FName ConflictGroup;

    UPROPERTY(EditAnywhere, Category="Application")
    float BaseChancePercent = 100.0f;

    UPROPERTY(EditAnywhere, Category="Application")
    FName ResistanceStatusKey;

    UPROPERTY(EditAnywhere, Category="Application")
    FName AllowedSources;

    UPROPERTY(EditAnywhere, Category="Application")
    FName AllowedTargets;

    UPROPERTY(EditAnywhere, Category="Modifiers")
    float DefenseMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, Category="Modifiers")
    float ATBRateMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, Category="Modifiers")
    float AggroGainMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, Category="Modifiers")
    int32 FlatAggroOnApply = 0;

    UPROPERTY(EditAnywhere, Category="Presentation")
    FText Symbol;

    UPROPERTY(EditAnywhere, Category="Cleanup")
    bool bRemoveOnKO = true;

    UPROPERTY(EditAnywhere, Category="Cleanup")
    bool bRemoveOnBattleEnd = true;

    UPROPERTY(EditAnywhere, Category="Integration")
    FString LegacyBuffClass;

    UPROPERTY(EditAnywhere, Category="Integration")
    FString ImplementationRoute;

    UPROPERTY(VisibleAnywhere, Category="Integration")
    FString ImplementationStatus;
};

/** Optional Cascade alternatives. Each phase uses its matching Visuals transform. */
USTRUCT(BlueprintType)
struct REDITUSTABLEBUFFS_API FReditusTableBuffCascadeVisuals
{
    GENERATED_BODY()

    /** A finite burst on a successful application or refresh. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Buff Visuals")
    UParticleSystem* OnApply = nullptr;

    /** Loop managed by the table for effects whose lifetime it owns. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Buff Visuals")
    UParticleSystem* WhileActive = nullptr;

    /** A finite burst on normal expiry of a table-managed effect. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Buff Visuals")
    UParticleSystem* OnEnd = nullptr;
};

USTRUCT(BlueprintType)
struct REDITUSTABLEBUFFS_API FReditusTableBuffRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Buff")
    FText DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Buff")
    FText EffectSummary;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Buff")
    EReditusTableBuffEffect RuntimeEffect = EReditusTableBuffEffect::Unwired;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Buff", meta=(ClampMin="0.1", ClampMax="10.0"))
    float PhysicalDefenseMultiplier = 1.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Buff", meta=(ClampMin="0.1", ClampMax="10.0"))
    float RateMultiplier = 1.f;

    /** Percent of the recipient's maximum HP dealt at each eligible Cycle End. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Buff", meta=(ClampMin="0.1", ClampMax="100.0"))
    float MaxHPDamagePercent = 0.f;

    /** Percent of the recipient's max HP or max MP restored each eligible Cycle End. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Buff", meta=(ClampMin="0.1", ClampMax="100.0"))
    float RecoveryPercent = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Buff", meta=(ClampMin="0.0", ClampMax="100.0"))
    float ApplicationChancePercent = 100.f;

    /** Full future cycles of the recipient. Recasting refreshes this value. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Buff", meta=(ClampMin="1", ClampMax="99"))
    int32 OwnerCycles = 3;

    /** Reuse an existing BP_BuffBase child and its existing combat behavior. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Legacy Buff")
    TSoftClassPtr<AActor> LegacyEffectClass;

    /** Optional canonical class to check for immunity (for example, base Blind for a Blind variant). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Legacy Buff")
    TSoftClassPtr<AActor> LegacyImmunityClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Legacy Buff")
    EReditusLegacyBuffDuration LegacyDuration = EReditusLegacyBuffDuration::ClassDefault;

    /** Negative uses the existing Blueprint class's success chance. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Legacy Buff", meta=(ClampMin="-1.0", ClampMax="100.0"))
    float LegacyChancePercent = -1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Buff")
    FReditusBuffVisualRow Visuals;

    /** Optional one-shot sound on each successful application or refresh. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Buff")
    USoundBase* OnApplySound = nullptr;

    UPROPERTY(EditAnywhere, Category="Catalogue", meta=(DisplayName="Catalogue Design"))
    FReditusBuffCatalogueDesign Catalogue;

    /** Optional legacy particles. If both assets are set for a phase, Niagara takes precedence. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Buff", meta=(DisplayName="Cascade Visuals"))
    FReditusTableBuffCascadeVisuals CascadeVisuals;

    /** Blueprint component created for the Aggro Focus row; the table owns its lifetime. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aggro Focus")
    TSoftClassPtr<UActorComponent> AggroFocusBuffClass;
};

UCLASS()
class REDITUSTABLEBUFFS_API UReditusTableBuffLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Apply the selected row to the caster. Call once when the skill successfully resolves. */
    UFUNCTION(BlueprintCallable, Category="Reditus|Table Buffs")
    static bool ApplyBuffByID(AActor* Caster, UDataTable* BuffTable, FName BuffID);

    /** Pass the skill's resolved recipients. For hostile skills, pass only targets that passed the skill's hit and resistance checks. Returns the number applied or refreshed. */
    UFUNCTION(BlueprintCallable, Category="Reditus|Table Buffs", meta=(DisplayName="Apply Buff by ID to Targets"))
    static int32 ApplyBuffByIDToTargets(AActor* Caster, const TArray<AActor*>& EligibleTargets, UDataTable* BuffTable, FName BuffID);

    /** Store the row ID from the player skill selection on the spawned skill actor. */
    UFUNCTION(BlueprintCallable, Category="Reditus|Table Buffs")
    static void SetTableSkillID(AActor* Skill, FName SkillID);

    static const FProperty* FindSkillBuffRowsField(const UScriptStruct* Struct);

    /** Returns true when the skill explicitly uses table buffs, even if application was resisted. */
    UFUNCTION(BlueprintCallable, Category="Reditus|Table Buffs", meta=(DisplayName="Apply Skill Buff Rows"))
    static bool ApplySkillBuffRows(AActor* Skill, UDataTable* SkillTable, AActor* EligibleTarget);

    /** Insert on the player index in enemy aggro selectors. Falls back to the old index. */
    UFUNCTION(BlueprintPure, Category="Reditus|Table Buffs")
    static int32 ResolveAggroFocusIndex(AActor* BattleController, AActor* Selector, int32 LegacyIndex);

    /** The active Aggro Focus recipient used by normal enemy aggro selection, or None. */
    UFUNCTION(BlueprintPure, Category="Reditus|Table Buffs")
    static AActor* GetAggroFocusTarget(AActor* BattleController);

    /** Read the live base value; the table buff never writes the unit's defense. */
    UFUNCTION(BlueprintPure, Category="Reditus|Table Buffs")
    static int32 GetBuffedDefense(AActor* Target, int32 BaseDefense);
};
