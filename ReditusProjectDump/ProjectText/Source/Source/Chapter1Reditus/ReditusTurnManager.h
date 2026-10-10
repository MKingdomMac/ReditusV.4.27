#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ReditusTurnTypes.h"
#include "ReditusTurnManager.generated.h"

class AActor;

/**
 * Authoritative turn timeline for one active battle world.
 *
 * This class owns ORDER only. It does not calculate damage, play UI, or decide
 * which statuses exist. The BattleSubsystem coordinates those systems.
 */
UCLASS(BlueprintType)
class CHAPTER1REDITUS_API UReditusTurnManager : public UObject
{
    GENERATED_BODY()

public:

    UPROPERTY(BlueprintAssignable, Category = "Reditus|Battle|Turn")
    FReditusTurnTimelineChangedSignature OnTurnTimelineChanged;

    UPROPERTY(BlueprintAssignable, Category = "Reditus|Battle|Turn")
    FReditusCurrentTurnChangedSignature OnCurrentTurnChanged;

    // ---------------------------------------------------------------------
    // READ
    // ---------------------------------------------------------------------

    UFUNCTION(BlueprintPure, Category = "Reditus|Battle|Turn")
    FReditusTurnTimelineSnapshot GetTimelineSnapshot() const;

    UFUNCTION(BlueprintPure, Category = "Reditus|Battle|Turn")
    AActor* GetCurrentUnit() const { return CurrentUnit; }

    UFUNCTION(BlueprintPure, Category = "Reditus|Battle|Turn")
    AActor* PeekNextUnit() const;

    UFUNCTION(BlueprintPure, Category = "Reditus|Battle|Turn")
    int32 FindNextEntryIndexForUnit(AActor* Unit) const;

    UFUNCTION(BlueprintPure, Category = "Reditus|Battle|Turn")
    TArray<FReditusTurnEntry> GetEntriesForRound(int32 RoundIndex) const;

    // ---------------------------------------------------------------------
    // LEGACY SYNCHRONIZATION
    // ---------------------------------------------------------------------

    /**
     * Mirrors the existing BP_BattleController readyToAttackUnits array into
     * the new authoritative timeline without deleting the old graph.
     *
     * Entry IDs are reconciled where possible so UI icons can animate instead
     * of being destroyed/recreated every refresh.
     */
    UFUNCTION(BlueprintCallable, Category = "Reditus|Battle|Turn|Legacy")
    void SyncLegacyCurrentRound(
        AActor* InCurrentUnit,
        const TArray<AActor*>& ReadyToAttackUnits,
        int32 InCurrentRoundIndex,
        bool bRemoveOneCurrentOccurrence
    );

    /**
     * Same as above, but allows the UI to show current + next round at once.
     */
    UFUNCTION(BlueprintCallable, Category = "Reditus|Battle|Turn|Legacy")
    void SyncLegacyTwoRounds(
        AActor* InCurrentUnit,
        const TArray<AActor*>& CurrentRoundUnits,
        const TArray<AActor*>& NextRoundUnits,
        int32 InCurrentRoundIndex,
        bool bRemoveOneCurrentOccurrence
    );

    // ---------------------------------------------------------------------
    // TURN LIFECYCLE
    // ---------------------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "Reditus|Battle|Turn")
    bool NotifyTurnStarted(AActor* Unit, bool bConsumeMatchingUpcomingEntry);

    UFUNCTION(BlueprintCallable, Category = "Reditus|Battle|Turn")
    bool NotifyTurnEnded(AActor* Unit);

    // ---------------------------------------------------------------------
    // TURN MANIPULATION
    // ---------------------------------------------------------------------

    /** Move the target's NEXT unresolved turn later, but never outside its round. */
    UFUNCTION(BlueprintCallable, Category = "Reditus|Battle|Turn|Manipulation")
    bool DelayNextTurn(AActor* Unit, int32 Positions, FName SourceID);

    /** Move the target's NEXT unresolved turn earlier, but never outside its round. */
    UFUNCTION(BlueprintCallable, Category = "Reditus|Battle|Turn|Manipulation")
    bool AdvanceNextTurn(AActor* Unit, int32 Positions, FName SourceID);

    /** Swap two upcoming action entries. */
    UFUNCTION(BlueprintCallable, Category = "Reditus|Battle|Turn|Manipulation")
    bool SwapUpcomingTurns(AActor* UnitA, AActor* UnitB, FName SourceID);

    /**
     * Party deployment/replacement helper.
     *
     * If IncomingUnit already exists on the future timeline, outgoing/incoming
     * future ownership is swapped. Otherwise all future OutgoingUnit slots are
     * replaced by IncomingUnit while preserving EntryID/position.
     */
    UFUNCTION(BlueprintCallable, Category = "Reditus|Battle|Turn|Manipulation")
    bool RedeployUnit(AActor* OutgoingUnit, AActor* IncomingUnit, FName SourceID);

    /** Leghold-style effect: move the next unresolved action to the end of its round. */
    UFUNCTION(BlueprintCallable, Category = "Reditus|Battle|Turn|Manipulation")
    bool ForceNextTurnToEndOfRound(AActor* Unit, FName SourceID);

    UFUNCTION(BlueprintCallable, Category = "Reditus|Battle|Turn|Manipulation")
    bool InsertExtraTurn(
        AActor* Unit,
        int32 InsertIndex,
        int32 RoundIndex,
        FName SourceID
    );

    /** Death/flee/removal. This is not a normal turn-skip mechanic. */
    UFUNCTION(BlueprintCallable, Category = "Reditus|Battle|Turn|Manipulation")
    int32 RemoveAllUpcomingTurnsForUnit(AActor* Unit, FName SourceID);

    /**
     * Reserved for Stagger/Sleep/Petrify/etc. when a future slot reaches the
     * front and the unit is unable to act.
     */
    UFUNCTION(BlueprintCallable, Category = "Reditus|Battle|Turn|Eligibility")
    bool ConsumeBlockedUpcomingTurn(
        AActor* Unit,
        const FReditusTurnEligibility& Eligibility
    );

    /** Same rule when the legacy flow has already promoted the unit to CurrentUnit. */
    UFUNCTION(BlueprintCallable, Category = "Reditus|Battle|Turn|Eligibility")
    bool ConsumeBlockedCurrentTurn(
        AActor* Unit,
        const FReditusTurnEligibility& Eligibility
    );

private:

    UPROPERTY(Transient)
    TArray<FReditusTurnEntry> UpcomingEntries;

    UPROPERTY(Transient)
    AActor* CurrentUnit = nullptr;

    UPROPERTY(Transient)
    int32 CurrentRoundIndex = 0;

    UPROPERTY(Transient)
    int32 Revision = 0;

    UPROPERTY(Transient)
    int32 NextEntryID = 1;

    FReditusTurnTimelineSnapshot CaptureSnapshot() const;

    void EmitTimelineUpdate(
        const FReditusTurnTimelineSnapshot& OldSnapshot,
        const FReditusTurnChangeInfo& ChangeInfo
    );

    bool HasStateChangedFromSnapshot(
        const FReditusTurnTimelineSnapshot& Snapshot
    ) const;

    int32 FindFirstIndexInRound(int32 RoundIndex) const;
    int32 FindLastIndexInRound(int32 RoundIndex) const;

    void BuildReconciledTimeline(
        const TArray<AActor*>& CurrentRoundUnits,
        const TArray<AActor*>& NextRoundUnits,
        int32 InCurrentRoundIndex,
        AActor* InCurrentUnit,
        bool bRemoveOneCurrentOccurrence
    );

    FReditusTurnEntry MakeNewEntry(
        AActor* Unit,
        int32 RoundIndex,
        bool bIsExtraTurn,
        FName SourceID
    );
};
