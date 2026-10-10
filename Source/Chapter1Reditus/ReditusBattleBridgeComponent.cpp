#include "ReditusBattleBridgeComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "ReditusBattleSubsystem.h"

UReditusBattleBridgeComponent::UReditusBattleBridgeComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UReditusBattleBridgeComponent::BeginPlay()
{
    Super::BeginPlay();

    if (UReditusBattleSubsystem* BattleSubsystem = GetBattleSubsystem())
    {
        BattleSubsystem->RegisterLegacyBattleController(GetOwner());

        BattleSubsystem->OnTurnTimelineChanged.AddDynamic(
            this,
            &UReditusBattleBridgeComponent::HandleTimelineChanged
        );

        BattleSubsystem->OnCurrentTurnChanged.AddDynamic(
            this,
            &UReditusBattleBridgeComponent::HandleCurrentTurnChanged
        );

        BattleSubsystem->OnUnitTurnBlocked.AddDynamic(
            this,
            &UReditusBattleBridgeComponent::HandleUnitTurnBlocked
        );
    }
}

void UReditusBattleBridgeComponent::EndPlay(
    const EEndPlayReason::Type EndPlayReason)
{
    if (UReditusBattleSubsystem* BattleSubsystem = GetBattleSubsystem())
    {
        BattleSubsystem->OnTurnTimelineChanged.RemoveAll(this);
        BattleSubsystem->OnCurrentTurnChanged.RemoveAll(this);
        BattleSubsystem->OnUnitTurnBlocked.RemoveAll(this);
        BattleSubsystem->UnregisterLegacyBattleController(GetOwner());
    }

    Super::EndPlay(EndPlayReason);
}

UReditusBattleSubsystem*
UReditusBattleBridgeComponent::GetBattleSubsystem() const
{
    UWorld* World = GetWorld();
    return World ? World->GetSubsystem<UReditusBattleSubsystem>() : nullptr;
}

FReditusTurnTimelineSnapshot
UReditusBattleBridgeComponent::GetTurnTimelineSnapshot() const
{
    if (UReditusBattleSubsystem* BattleSubsystem = GetBattleSubsystem())
    {
        return BattleSubsystem->GetTurnTimelineSnapshot();
    }

    return FReditusTurnTimelineSnapshot();
}

void UReditusBattleBridgeComponent::SyncLegacyCurrentRound(
    AActor* CurrentUnit,
    const TArray<AActor*>& ReadyToAttackUnits,
    int32 CurrentRoundIndex,
    bool bRemoveOneCurrentOccurrence)
{
    if (UReditusBattleSubsystem* BattleSubsystem = GetBattleSubsystem())
    {
        BattleSubsystem->SyncLegacyCurrentRound(
            CurrentUnit,
            ReadyToAttackUnits,
            CurrentRoundIndex,
            bRemoveOneCurrentOccurrence
        );
    }
}

void UReditusBattleBridgeComponent::SyncLegacyTwoRounds(
    AActor* CurrentUnit,
    const TArray<AActor*>& CurrentRoundUnits,
    const TArray<AActor*>& NextRoundUnits,
    int32 CurrentRoundIndex,
    bool bRemoveOneCurrentOccurrence)
{
    if (UReditusBattleSubsystem* BattleSubsystem = GetBattleSubsystem())
    {
        BattleSubsystem->SyncLegacyTwoRounds(
            CurrentUnit,
            CurrentRoundUnits,
            NextRoundUnits,
            CurrentRoundIndex,
            bRemoveOneCurrentOccurrence
        );
    }
}

FReditusDamageResult UReditusBattleBridgeComponent::CalculateDamage(
    const FReditusDamageRequest& Request) const
{
    if (UReditusBattleSubsystem* BattleSubsystem = GetBattleSubsystem())
    {
        return BattleSubsystem->CalculateDamage(Request);
    }

    return FReditusDamageResult();
}

bool UReditusBattleBridgeComponent::DelayNextTurn(
    AActor* Unit,
    int32 Positions,
    FName SourceID)
{
    if (UReditusBattleSubsystem* BattleSubsystem = GetBattleSubsystem())
    {
        return BattleSubsystem->DelayNextTurn(Unit, Positions, SourceID);
    }

    return false;
}

bool UReditusBattleBridgeComponent::AdvanceNextTurn(
    AActor* Unit,
    int32 Positions,
    FName SourceID)
{
    if (UReditusBattleSubsystem* BattleSubsystem = GetBattleSubsystem())
    {
        return BattleSubsystem->AdvanceNextTurn(Unit, Positions, SourceID);
    }

    return false;
}

bool UReditusBattleBridgeComponent::SwapUpcomingTurns(
    AActor* UnitA,
    AActor* UnitB,
    FName SourceID)
{
    if (UReditusBattleSubsystem* BattleSubsystem = GetBattleSubsystem())
    {
        return BattleSubsystem->SwapUpcomingTurns(UnitA, UnitB, SourceID);
    }

    return false;
}

bool UReditusBattleBridgeComponent::RedeployUnit(
    AActor* OutgoingUnit,
    AActor* IncomingUnit,
    FName SourceID)
{
    if (UReditusBattleSubsystem* BattleSubsystem = GetBattleSubsystem())
    {
        return BattleSubsystem->RedeployUnit(
            OutgoingUnit,
            IncomingUnit,
            SourceID
        );
    }

    return false;
}

bool UReditusBattleBridgeComponent::ForceNextTurnToEndOfRound(
    AActor* Unit,
    FName SourceID)
{
    if (UReditusBattleSubsystem* BattleSubsystem = GetBattleSubsystem())
    {
        return BattleSubsystem->ForceNextTurnToEndOfRound(Unit, SourceID);
    }

    return false;
}

bool UReditusBattleBridgeComponent::ApplyTimedAttackTurnDelay(
    AActor* TargetUnit,
    int32 Positions)
{
    if (UReditusBattleSubsystem* BattleSubsystem = GetBattleSubsystem())
    {
        return BattleSubsystem->ApplyTimedAttackTurnDelay(
            TargetUnit,
            Positions
        );
    }

    return false;
}

FReditusTurnEligibility
UReditusBattleBridgeComponent::EvaluateUnitTurnEligibility(AActor* Unit) const
{
    if (UReditusBattleSubsystem* BattleSubsystem = GetBattleSubsystem())
    {
        return BattleSubsystem->GetUnitTurnEligibility(Unit);
    }

    return FReditusTurnEligibility();
}

void UReditusBattleBridgeComponent::HandleTimelineChanged(
    FReditusTurnTimelineUpdate Update)
{
    OnTurnTimelineChanged.Broadcast(Update);
}

void UReditusBattleBridgeComponent::HandleCurrentTurnChanged(
    AActor* PreviousUnit,
    AActor* NewUnit)
{
    OnCurrentTurnChanged.Broadcast(PreviousUnit, NewUnit);
}

void UReditusBattleBridgeComponent::HandleUnitTurnBlocked(
    AActor* Unit,
    FReditusTurnEligibility Eligibility)
{
    OnUnitTurnBlocked.Broadcast(Unit, Eligibility);
}
