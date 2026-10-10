#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ReditusTurnTypes.h"
#include "ReditusTurnEligibilityComponent.generated.h"

/**
 * Runtime collection of reasons this unit cannot currently act.
 *
 * Add this to BP_UnitBase. Existing stagger/status Blueprints can add/remove
 * blockers without the TurnManager knowing what those statuses are.
 */
UCLASS(ClassGroup=(Reditus), meta=(BlueprintSpawnableComponent))
class CHAPTER1REDITUS_API UReditusTurnEligibilityComponent : public UActorComponent
{
    GENERATED_BODY()

public:

    UReditusTurnEligibilityComponent();

    UPROPERTY(BlueprintAssignable, Category = "Reditus|Battle|Turn")
    FReditusTurnEligibilityChangedSignature OnTurnEligibilityChanged;

    UFUNCTION(BlueprintCallable, Category = "Reditus|Battle|Turn|Eligibility")
    bool AddOrUpdateTurnBlocker(
        FName SourceID,
        EReditusTurnBlockReason Reason,
        FText DisplayText,
        int32 Priority
    );

    UFUNCTION(BlueprintCallable, Category = "Reditus|Battle|Turn|Eligibility")
    bool RemoveTurnBlocker(FName SourceID);

    UFUNCTION(BlueprintCallable, Category = "Reditus|Battle|Turn|Eligibility")
    void ClearAllTurnBlockers();

    UFUNCTION(BlueprintPure, Category = "Reditus|Battle|Turn|Eligibility")
    bool HasTurnBlocker(FName SourceID) const;

    UFUNCTION(BlueprintPure, Category = "Reditus|Battle|Turn|Eligibility")
    FReditusTurnEligibility GetTurnEligibility() const;

    UFUNCTION(BlueprintPure, Category = "Reditus|Battle|Turn|Eligibility")
    TArray<FReditusTurnBlocker> GetActiveTurnBlockers() const
    {
        return ActiveBlockers;
    }

private:

    UPROPERTY(Transient)
    TArray<FReditusTurnBlocker> ActiveBlockers;

    static FText GetDefaultDisplayText(EReditusTurnBlockReason Reason);
    void BroadcastEligibilityChanged();
};
