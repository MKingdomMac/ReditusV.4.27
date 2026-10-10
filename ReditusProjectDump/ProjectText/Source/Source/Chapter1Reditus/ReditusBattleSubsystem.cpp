#include "ReditusBattleSubsystem.h"

#include "GameFramework/Actor.h"
#include "ReditusTurnEligibilityComponent.h"
#include "ReditusTurnManager.h"

void UReditusBattleSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    TurnManager = NewObject<UReditusTurnManager>(this);

    if (TurnManager)
    {
        TurnManager->OnTurnTimelineChanged.AddDynamic(
            this,
            &UReditusBattleSubsystem::HandleTurnTimelineChanged
        );

        TurnManager->OnCurrentTurnChanged.AddDynamic(
            this,
            &UReditusBattleSubsystem::HandleCurrentTurnChanged
        );
    }
}

void UReditusBattleSubsystem::Deinitialize()
{
    if (TurnManager)
    {
        TurnManager->OnTurnTimelineChanged.RemoveAll(this);
        TurnManager->OnCurrentTurnChanged.RemoveAll(this);
    }

    RegisteredLegacyBattleController = nullptr;
    TurnManager = nullptr;

    Super::Deinitialize();
}

void UReditusBattleSubsystem::RegisterLegacyBattleController(
    AActor* BattleControllerActor)
{
    if (!IsValid(BattleControllerActor))
    {
        return;
    }

    RegisteredLegacyBattleController = BattleControllerActor;
}

void UReditusBattleSubsystem::UnregisterLegacyBattleController(
    AActor* BattleControllerActor)
{
    if (RegisteredLegacyBattleController == BattleControllerActor)
    {
        RegisteredLegacyBattleController = nullptr;
    }
}

void UReditusBattleSubsystem::SyncLegacyCurrentRound(
    AActor* CurrentUnit,
    const TArray<AActor*>& ReadyToAttackUnits,
    int32 CurrentRoundIndex,
    bool bRemoveOneCurrentOccurrence)
{
    if (!TurnManager)
    {
        return;
    }

    TurnManager->SyncLegacyCurrentRound(
        CurrentUnit,
        ReadyToAttackUnits,
        CurrentRoundIndex,
        bRemoveOneCurrentOccurrence
    );
}

void UReditusBattleSubsystem::SyncLegacyTwoRounds(
    AActor* CurrentUnit,
    const TArray<AActor*>& CurrentRoundUnits,
    const TArray<AActor*>& NextRoundUnits,
    int32 CurrentRoundIndex,
    bool bRemoveOneCurrentOccurrence)
{
    if (!TurnManager)
    {
        return;
    }

    TurnManager->SyncLegacyTwoRounds(
        CurrentUnit,
        CurrentRoundUnits,
        NextRoundUnits,
        CurrentRoundIndex,
        bRemoveOneCurrentOccurrence
    );
}

FReditusDamageResult UReditusBattleSubsystem::CalculateDamage(
    const FReditusDamageRequest& Request) const
{
    return UReditusDamageCalculator::CalculateDamage(Request);
}

FReditusTurnTimelineSnapshot
UReditusBattleSubsystem::GetTurnTimelineSnapshot() const
{
    return TurnManager
        ? TurnManager->GetTimelineSnapshot()
        : FReditusTurnTimelineSnapshot();
}

bool UReditusBattleSubsystem::DelayNextTurn(
    AActor* Unit,
    int32 Positions,
    FName SourceID)
{
    return TurnManager && TurnManager->DelayNextTurn(Unit, Positions, SourceID);
}

bool UReditusBattleSubsystem::AdvanceNextTurn(
    AActor* Unit,
    int32 Positions,
    FName SourceID)
{
    return TurnManager && TurnManager->AdvanceNextTurn(Unit, Positions, SourceID);
}

bool UReditusBattleSubsystem::SwapUpcomingTurns(
    AActor* UnitA,
    AActor* UnitB,
    FName SourceID)
{
    return TurnManager && TurnManager->SwapUpcomingTurns(UnitA, UnitB, SourceID);
}

bool UReditusBattleSubsystem::RedeployUnit(
    AActor* OutgoingUnit,
    AActor* IncomingUnit,
    FName SourceID)
{
    return TurnManager && TurnManager->RedeployUnit(
        OutgoingUnit,
        IncomingUnit,
        SourceID
    );
}

bool UReditusBattleSubsystem::ForceNextTurnToEndOfRound(
    AActor* Unit,
    FName SourceID)
{
    return TurnManager && TurnManager->ForceNextTurnToEndOfRound(Unit, SourceID);
}

bool UReditusBattleSubsystem::ApplyTimedAttackTurnDelay(
    AActor* TargetUnit,
    int32 Positions)
{
    if (!TurnManager || !IsValid(TargetUnit) || Positions <= 0)
    {
        return false;
    }

    return TurnManager->DelayNextTurn(
        TargetUnit,
        Positions,
        TEXT("TimedAttack")
    );
}

FReditusTurnEligibility
UReditusBattleSubsystem::GetUnitTurnEligibility(AActor* Unit) const
{
    FReditusTurnEligibility Eligibility;

    if (!IsValid(Unit))
    {
        Eligibility.bCanAct = false;
        Eligibility.BlockReason = EReditusTurnBlockReason::CustomStatus;
        Eligibility.BlockingSourceID = TEXT("InvalidUnit");
        Eligibility.DisplayText = NSLOCTEXT(
            "ReditusTurn",
            "InvalidUnit",
            "UNABLE TO ACT"
        );
        return Eligibility;
    }

    if (UReditusTurnEligibilityComponent* Component =
        Unit->FindComponentByClass<UReditusTurnEligibilityComponent>())
    {
        return Component->GetTurnEligibility();
    }

    // During migration, units without the component behave exactly as before.
    Eligibility.bCanAct = true;
    Eligibility.BlockReason = EReditusTurnBlockReason::None;
    return Eligibility;
}

EReditusBeginTurnResult UReditusBattleSubsystem::TryBeginNextTurn(
    AActor*& OutUnit,
    FReditusTurnEligibility& OutEligibility)
{
    OutUnit = nullptr;
    OutEligibility = FReditusTurnEligibility();

    if (!TurnManager)
    {
        return EReditusBeginTurnResult::NoTurnAvailable;
    }

    AActor* NextUnit = TurnManager->PeekNextUnit();
    if (!IsValid(NextUnit))
    {
        return EReditusBeginTurnResult::NoTurnAvailable;
    }

    OutUnit = NextUnit;
    OutEligibility = GetUnitTurnEligibility(NextUnit);

    if (!OutEligibility.bCanAct)
    {
        TurnManager->ConsumeBlockedUpcomingTurn(NextUnit, OutEligibility);
        OnUnitTurnBlocked.Broadcast(NextUnit, OutEligibility);
        return EReditusBeginTurnResult::TurnBlocked;
    }

    TurnManager->NotifyTurnStarted(NextUnit, true);
    return EReditusBeginTurnResult::TurnStarted;
}

bool UReditusBattleSubsystem::EndCurrentTurn(AActor* Unit)
{
    return TurnManager && TurnManager->NotifyTurnEnded(Unit);
}

void UReditusBattleSubsystem::HandleTurnTimelineChanged(
    FReditusTurnTimelineUpdate Update)
{
    OnTurnTimelineChanged.Broadcast(Update);
}

void UReditusBattleSubsystem::HandleCurrentTurnChanged(
    AActor* PreviousUnit,
    AActor* NewUnit)
{
    OnCurrentTurnChanged.Broadcast(PreviousUnit, NewUnit);
}
