#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ReditusTurnTypes.h"
#include "ReditusDamageCalculator.h"
#include "ReditusBattleSubsystem.generated.h"

class AActor;
class UReditusTurnManager;

/**
 * New battle backend.
 *
 * IMPORTANT MIGRATION RULE:
 * BP_BattleController remains the placed level-facing Actor for now.
 * This subsystem is built BEHIND it and responsibilities move here gradually.
 */
UCLASS(BlueprintType)
class CHAPTER1REDITUS_API UReditusBattleSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UPROPERTY(BlueprintAssignable, Category = "Reditus|Battle|Turn")
    FReditusTurnTimelineChangedSignature OnTurnTimelineChanged;

    UPROPERTY(BlueprintAssignable, Category = "Reditus|Battle|Turn")
    FReditusCurrentTurnChangedSignature OnCurrentTurnChanged;

    UPROPERTY(BlueprintAssignable, Category = "Reditus|Battle|Turn")
    FReditusUnitTurnBlockedSignature OnUnitTurnBlocked;

    UFUNCTION(BlueprintPure, Category = "Reditus|Battle")
    UReditusTurnManager* GetTurnManager() const { return TurnManager; }

    // ---------------------------------------------------------------------
    // LEGACY BP_BattleController BRIDGE
    // ---------------------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "Reditus|Battle|Legacy")
    void RegisterLegacyBattleController(AActor* BattleControllerActor);

    UFUNCTION(BlueprintCallable, Category = "Reditus|Battle|Legacy")
    void UnregisterLegacyBattleController(AActor* BattleControllerActor);

    UFUNCTION(BlueprintPure, Category = "Reditus|Battle|Legacy")
    AActor* GetRegisteredLegacyBattleController() const
    {
        return RegisteredLegacyBattleController;
    }

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
    // DAMAGE
    // ---------------------------------------------------------------------

    /**
     * Central battle-facing damage entry point.
     *
     * BP_BattleController should migrate toward calling THIS through the
     * ReditusBattleBridgeComponent instead of owning the damage math itself.
     */
    UFUNCTION(BlueprintCallable, Category = "Reditus|Battle|Damage")
    FReditusDamageResult CalculateDamage(
        const FReditusDamageRequest& Request
    ) const;

    // ---------------------------------------------------------------------
    // TIMELINE READ / MUTATION
    // ---------------------------------------------------------------------

    UFUNCTION(BlueprintPure, Category = "Reditus|Battle|Turn")
    FReditusTurnTimelineSnapshot GetTurnTimelineSnapshot() const;

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

    /** Standard Reditus timed-hit timeline reward. Default usage = Positions 1. */
    UFUNCTION(BlueprintCallable, Category = "Reditus|Battle|Turn|TimedAttack")
    bool ApplyTimedAttackTurnDelay(AActor* TargetUnit, int32 Positions);

    // ---------------------------------------------------------------------
    // TURN ELIGIBILITY
    // ---------------------------------------------------------------------

    UFUNCTION(BlueprintPure, Category = "Reditus|Battle|Turn|Eligibility")
    FReditusTurnEligibility GetUnitTurnEligibility(AActor* Unit) const;

    /**
     * Future replacement for the old turn-start branch.
     * If the next unit is blocked by Stagger/Sleep/Petrify/etc., its turn is
     * consumed and TurnBlocked is returned. Ordinary delay never reaches here.
     */
    UFUNCTION(BlueprintCallable, Category = "Reditus|Battle|Turn")
    EReditusBeginTurnResult TryBeginNextTurn(
        AActor*& OutUnit,
        FReditusTurnEligibility& OutEligibility
    );

    UFUNCTION(BlueprintCallable, Category = "Reditus|Battle|Turn")
    bool EndCurrentTurn(AActor* Unit);

private:

    UPROPERTY(Transient)
    UReditusTurnManager* TurnManager = nullptr;

    UPROPERTY(Transient)
    AActor* RegisteredLegacyBattleController = nullptr;

    UFUNCTION()
    void HandleTurnTimelineChanged(FReditusTurnTimelineUpdate Update);

    UFUNCTION()
    void HandleCurrentTurnChanged(AActor* PreviousUnit, AActor* NewUnit);
};
