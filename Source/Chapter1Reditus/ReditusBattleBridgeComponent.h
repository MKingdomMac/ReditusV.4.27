#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ReditusTurnTypes.h"
#include "ReditusDamageCalculator.h"
#include "ReditusBattleBridgeComponent.generated.h"

class AActor;
class UReditusBattleSubsystem;

/**
 * Compatibility bridge added to the EXISTING placed BP_BattleController.
 *
 * It lets old Blueprint graphs feed the new subsystem one function at a time.
 * No reparenting and no deletion of BP_BattleController is required.
 */
UCLASS(ClassGroup=(Reditus), meta=(BlueprintSpawnableComponent))
class CHAPTER1REDITUS_API UReditusBattleBridgeComponent : public UActorComponent
{
    GENERATED_BODY()

public:

    UReditusBattleBridgeComponent();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    UPROPERTY(BlueprintAssignable, Category = "Reditus|Battle|Turn")
    FReditusTurnTimelineChangedSignature OnTurnTimelineChanged;

    UPROPERTY(BlueprintAssignable, Category = "Reditus|Battle|Turn")
    FReditusCurrentTurnChangedSignature OnCurrentTurnChanged;

    UPROPERTY(BlueprintAssignable, Category = "Reditus|Battle|Turn")
    FReditusUnitTurnBlockedSignature OnUnitTurnBlocked;

    UFUNCTION(BlueprintPure, Category = "Reditus|Battle")
    UReditusBattleSubsystem* GetBattleSubsystem() const;

    UFUNCTION(BlueprintPure, Category = "Reditus|Battle|Turn")
    FReditusTurnTimelineSnapshot GetTurnTimelineSnapshot() const;

    // ---------------------------------------------------------------------
    // SAFE LEGACY SYNC POINTS
    // ---------------------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "Reditus|Battle|Legacy")
    void SyncLegacyCurrentRound(
        AActor* CurrentUnit,
        const TArray<AActor*>& ReadyToAttackUnits,
        int32 CurrentRoundIndex,
        bool bRemoveOneCurrentOccurrence
    );

    UFUNCTION(BlueprintCallable, Category = "Reditus|Battle|Legacy")
    void SyncLegacyTwoRounds(
        AActor* CurrentUnit,
        const TArray<AActor*>& CurrentRoundUnits,
        const TArray<AActor*>& NextRoundUnits,
        int32 CurrentRoundIndex,
        bool bRemoveOneCurrentOccurrence
    );

    // ---------------------------------------------------------------------
    // DAMAGE BRIDGE
    // ---------------------------------------------------------------------

    /**
     * Safe migration seam for the existing BP_BattleController CalculateDamage.
     * Build FReditusDamageRequest in Blueprint, call this, then map the result
     * back to the old output pins while old callers remain unchanged.
     */
    UFUNCTION(BlueprintCallable, Category = "Reditus|Battle|Damage")
    FReditusDamageResult CalculateDamage(
        const FReditusDamageRequest& Request
    ) const;

    // ---------------------------------------------------------------------
    // GAMEPLAY HELPERS USED BY EXISTING BLUEPRINTS
    // ---------------------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "Reditus|Battle|Turn")
    bool DelayNextTurn(AActor* Unit, int32 Positions, FName SourceID);

    UFUNCTION(BlueprintCallable, Category = "Reditus|Battle|Turn")
    bool AdvanceNextTurn(AActor* Unit, int32 Positions, FName SourceID);

    UFUNCTION(BlueprintCallable, Category = "Reditus|Battle|Turn")
    bool SwapUpcomingTurns(AActor* UnitA, AActor* UnitB, FName SourceID);

    UFUNCTION(BlueprintCallable, Category = "Reditus|Battle|Turn")
    bool RedeployUnit(AActor* OutgoingUnit, AActor* IncomingUnit, FName SourceID);

    UFUNCTION(BlueprintCallable, Category = "Reditus|Battle|Turn")
    bool ForceNextTurnToEndOfRound(AActor* Unit, FName SourceID);

    /** Successful timed input: moves target's next action back by Positions. */
    UFUNCTION(BlueprintCallable, Category = "Reditus|Battle|Turn|TimedAttack")
    bool ApplyTimedAttackTurnDelay(AActor* TargetUnit, int32 Positions);

    UFUNCTION(BlueprintPure, Category = "Reditus|Battle|Turn|Eligibility")
    FReditusTurnEligibility EvaluateUnitTurnEligibility(AActor* Unit) const;

private:

    UFUNCTION()
    void HandleTimelineChanged(FReditusTurnTimelineUpdate Update);

    UFUNCTION()
    void HandleCurrentTurnChanged(AActor* PreviousUnit, AActor* NewUnit);

    UFUNCTION()
    void HandleUnitTurnBlocked(
        AActor* Unit,
        FReditusTurnEligibility Eligibility
    );
};
