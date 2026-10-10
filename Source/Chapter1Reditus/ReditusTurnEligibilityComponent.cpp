#include "ReditusTurnEligibilityComponent.h"

UReditusTurnEligibilityComponent::UReditusTurnEligibilityComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

bool UReditusTurnEligibilityComponent::AddOrUpdateTurnBlocker(
    FName SourceID,
    EReditusTurnBlockReason Reason,
    FText DisplayText,
    int32 Priority)
{
    if (SourceID.IsNone() || Reason == EReditusTurnBlockReason::None)
    {
        return false;
    }

    for (FReditusTurnBlocker& Blocker : ActiveBlockers)
    {
        if (Blocker.SourceID == SourceID)
        {
            Blocker.Reason = Reason;
            Blocker.DisplayText = DisplayText;
            Blocker.Priority = Priority;
            BroadcastEligibilityChanged();
            return true;
        }
    }

    FReditusTurnBlocker NewBlocker;
    NewBlocker.SourceID = SourceID;
    NewBlocker.Reason = Reason;
    NewBlocker.DisplayText = DisplayText;
    NewBlocker.Priority = Priority;
    ActiveBlockers.Add(NewBlocker);

    BroadcastEligibilityChanged();
    return true;
}

bool UReditusTurnEligibilityComponent::RemoveTurnBlocker(FName SourceID)
{
    if (SourceID.IsNone())
    {
        return false;
    }

    const int32 Removed = ActiveBlockers.RemoveAll(
        [SourceID](const FReditusTurnBlocker& Blocker)
        {
            return Blocker.SourceID == SourceID;
        }
    );

    if (Removed > 0)
    {
        BroadcastEligibilityChanged();
        return true;
    }

    return false;
}

void UReditusTurnEligibilityComponent::ClearAllTurnBlockers()
{
    if (ActiveBlockers.Num() <= 0)
    {
        return;
    }

    ActiveBlockers.Reset();
    BroadcastEligibilityChanged();
}

bool UReditusTurnEligibilityComponent::HasTurnBlocker(FName SourceID) const
{
    if (SourceID.IsNone())
    {
        return false;
    }

    for (const FReditusTurnBlocker& Blocker : ActiveBlockers)
    {
        if (Blocker.SourceID == SourceID)
        {
            return true;
        }
    }

    return false;
}

FReditusTurnEligibility UReditusTurnEligibilityComponent::GetTurnEligibility() const
{
    FReditusTurnEligibility Eligibility;

    if (ActiveBlockers.Num() <= 0)
    {
        Eligibility.bCanAct = true;
        Eligibility.BlockReason = EReditusTurnBlockReason::None;
        return Eligibility;
    }

    const FReditusTurnBlocker* WinningBlocker = &ActiveBlockers[0];

    for (const FReditusTurnBlocker& Blocker : ActiveBlockers)
    {
        if (Blocker.Priority > WinningBlocker->Priority)
        {
            WinningBlocker = &Blocker;
        }
    }

    Eligibility.bCanAct = false;
    Eligibility.BlockReason = WinningBlocker->Reason;
    Eligibility.BlockingSourceID = WinningBlocker->SourceID;
    Eligibility.DisplayText = WinningBlocker->DisplayText.IsEmpty()
        ? GetDefaultDisplayText(WinningBlocker->Reason)
        : WinningBlocker->DisplayText;

    return Eligibility;
}

FText UReditusTurnEligibilityComponent::GetDefaultDisplayText(
    EReditusTurnBlockReason Reason)
{
    switch (Reason)
    {
    case EReditusTurnBlockReason::Staggered:
        return NSLOCTEXT("ReditusTurn", "Staggered", "STAGGERED");

    case EReditusTurnBlockReason::Sleeping:
        return NSLOCTEXT("ReditusTurn", "Sleeping", "ASLEEP");

    case EReditusTurnBlockReason::Petrified:
        return NSLOCTEXT("ReditusTurn", "Petrified", "PETRIFIED");

    case EReditusTurnBlockReason::Stunned:
        return NSLOCTEXT("ReditusTurn", "Stunned", "STUNNED");

    case EReditusTurnBlockReason::Paralyzed:
        return NSLOCTEXT("ReditusTurn", "Paralyzed", "PARALYZED");

    case EReditusTurnBlockReason::Frozen:
        return NSLOCTEXT("ReditusTurn", "Frozen", "FROZEN");

    case EReditusTurnBlockReason::CustomStatus:
        return NSLOCTEXT("ReditusTurn", "UnableToAct", "UNABLE TO ACT");

    case EReditusTurnBlockReason::None:
    default:
        return FText::GetEmpty();
    }
}

void UReditusTurnEligibilityComponent::BroadcastEligibilityChanged()
{
    OnTurnEligibilityChanged.Broadcast(GetTurnEligibility());
}
