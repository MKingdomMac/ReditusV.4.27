#include "ReditusTurnManager.h"

#include "GameFramework/Actor.h"

FReditusTurnTimelineSnapshot UReditusTurnManager::GetTimelineSnapshot() const
{
    return CaptureSnapshot();
}

FReditusTurnTimelineSnapshot UReditusTurnManager::CaptureSnapshot() const
{
    FReditusTurnTimelineSnapshot Snapshot;
    Snapshot.CurrentUnit = CurrentUnit;
    Snapshot.Entries = UpcomingEntries;
    Snapshot.CurrentRoundIndex = CurrentRoundIndex;
    Snapshot.Revision = Revision;
    return Snapshot;
}

AActor* UReditusTurnManager::PeekNextUnit() const
{
    return UpcomingEntries.Num() > 0 ? UpcomingEntries[0].Unit : nullptr;
}

int32 UReditusTurnManager::FindNextEntryIndexForUnit(AActor* Unit) const
{
    if (!IsValid(Unit))
    {
        return INDEX_NONE;
    }

    for (int32 Index = 0; Index < UpcomingEntries.Num(); ++Index)
    {
        if (UpcomingEntries[Index].Unit == Unit)
        {
            return Index;
        }
    }

    return INDEX_NONE;
}

TArray<FReditusTurnEntry> UReditusTurnManager::GetEntriesForRound(int32 RoundIndex) const
{
    TArray<FReditusTurnEntry> Result;

    for (const FReditusTurnEntry& Entry : UpcomingEntries)
    {
        if (Entry.RoundIndex == RoundIndex)
        {
            Result.Add(Entry);
        }
    }

    return Result;
}

void UReditusTurnManager::SyncLegacyCurrentRound(
    AActor* InCurrentUnit,
    const TArray<AActor*>& ReadyToAttackUnits,
    int32 InCurrentRoundIndex,
    bool bRemoveOneCurrentOccurrence)
{
    const FReditusTurnTimelineSnapshot OldSnapshot = CaptureSnapshot();
    const bool bWasEmpty = UpcomingEntries.Num() == 0 && CurrentUnit == nullptr;

    const TArray<AActor*> EmptyNextRound;
    BuildReconciledTimeline(
        ReadyToAttackUnits,
        EmptyNextRound,
        InCurrentRoundIndex,
        InCurrentUnit,
        bRemoveOneCurrentOccurrence
    );

    FReditusTurnChangeInfo Change;
    Change.Reason = bWasEmpty
        ? EReditusTurnChangeReason::InitialSync
        : EReditusTurnChangeReason::LegacySync;
    Change.AffectedUnit = InCurrentUnit;
    Change.SourceID = TEXT("LegacyBattleController");

    EmitTimelineUpdate(OldSnapshot, Change);
}

void UReditusTurnManager::SyncLegacyTwoRounds(
    AActor* InCurrentUnit,
    const TArray<AActor*>& CurrentRoundUnits,
    const TArray<AActor*>& NextRoundUnits,
    int32 InCurrentRoundIndex,
    bool bRemoveOneCurrentOccurrence)
{
    const FReditusTurnTimelineSnapshot OldSnapshot = CaptureSnapshot();
    const bool bWasEmpty = UpcomingEntries.Num() == 0 && CurrentUnit == nullptr;

    BuildReconciledTimeline(
        CurrentRoundUnits,
        NextRoundUnits,
        InCurrentRoundIndex,
        InCurrentUnit,
        bRemoveOneCurrentOccurrence
    );

    FReditusTurnChangeInfo Change;
    Change.Reason = bWasEmpty
        ? EReditusTurnChangeReason::InitialSync
        : EReditusTurnChangeReason::LegacySync;
    Change.AffectedUnit = InCurrentUnit;
    Change.SourceID = TEXT("LegacyBattleController");

    EmitTimelineUpdate(OldSnapshot, Change);
}

bool UReditusTurnManager::NotifyTurnStarted(
    AActor* Unit,
    bool bConsumeMatchingUpcomingEntry)
{
    if (!IsValid(Unit))
    {
        return false;
    }

    const FReditusTurnTimelineSnapshot OldSnapshot = CaptureSnapshot();
    AActor* PreviousUnit = CurrentUnit;

    int32 ConsumedIndex = INDEX_NONE;

    if (bConsumeMatchingUpcomingEntry)
    {
        ConsumedIndex = FindNextEntryIndexForUnit(Unit);
        if (ConsumedIndex != INDEX_NONE)
        {
            UpcomingEntries.RemoveAt(ConsumedIndex);
        }
    }

    CurrentUnit = Unit;

    FReditusTurnChangeInfo Change;
    Change.Reason = EReditusTurnChangeReason::TurnStarted;
    Change.AffectedUnit = Unit;
    Change.PreviousIndex = ConsumedIndex;
    Change.NewIndex = INDEX_NONE;

    EmitTimelineUpdate(OldSnapshot, Change);

    if (PreviousUnit != CurrentUnit)
    {
        OnCurrentTurnChanged.Broadcast(PreviousUnit, CurrentUnit);
    }

    return true;
}

bool UReditusTurnManager::NotifyTurnEnded(AActor* Unit)
{
    if (CurrentUnit != Unit)
    {
        return false;
    }

    const FReditusTurnTimelineSnapshot OldSnapshot = CaptureSnapshot();
    AActor* PreviousUnit = CurrentUnit;
    CurrentUnit = nullptr;

    FReditusTurnChangeInfo Change;
    Change.Reason = EReditusTurnChangeReason::TurnEnded;
    Change.AffectedUnit = Unit;

    EmitTimelineUpdate(OldSnapshot, Change);
    OnCurrentTurnChanged.Broadcast(PreviousUnit, nullptr);
    return true;
}

bool UReditusTurnManager::DelayNextTurn(
    AActor* Unit,
    int32 Positions,
    FName SourceID)
{
    if (!IsValid(Unit) || Positions <= 0)
    {
        return false;
    }

    const int32 OldIndex = FindNextEntryIndexForUnit(Unit);
    if (OldIndex == INDEX_NONE)
    {
        return false;
    }

    const int32 RoundIndex = UpcomingEntries[OldIndex].RoundIndex;
    const int32 LastIndex = FindLastIndexInRound(RoundIndex);
    if (LastIndex == INDEX_NONE)
    {
        return false;
    }

    const int32 NewIndex = FMath::Min(OldIndex + Positions, LastIndex);
    if (NewIndex == OldIndex)
    {
        return false;
    }

    const FReditusTurnTimelineSnapshot OldSnapshot = CaptureSnapshot();

    const FReditusTurnEntry Entry = UpcomingEntries[OldIndex];
    UpcomingEntries.RemoveAt(OldIndex);
    UpcomingEntries.Insert(Entry, NewIndex);

    FReditusTurnChangeInfo Change;
    Change.Reason = EReditusTurnChangeReason::Delay;
    Change.AffectedUnit = Unit;
    Change.PreviousIndex = OldIndex;
    Change.NewIndex = NewIndex;
    Change.Amount = NewIndex - OldIndex;
    Change.SourceID = SourceID;

    EmitTimelineUpdate(OldSnapshot, Change);
    return true;
}

bool UReditusTurnManager::AdvanceNextTurn(
    AActor* Unit,
    int32 Positions,
    FName SourceID)
{
    if (!IsValid(Unit) || Positions <= 0)
    {
        return false;
    }

    const int32 OldIndex = FindNextEntryIndexForUnit(Unit);
    if (OldIndex == INDEX_NONE)
    {
        return false;
    }

    const int32 RoundIndex = UpcomingEntries[OldIndex].RoundIndex;
    const int32 FirstIndex = FindFirstIndexInRound(RoundIndex);
    if (FirstIndex == INDEX_NONE)
    {
        return false;
    }

    const int32 NewIndex = FMath::Max(OldIndex - Positions, FirstIndex);
    if (NewIndex == OldIndex)
    {
        return false;
    }

    const FReditusTurnTimelineSnapshot OldSnapshot = CaptureSnapshot();

    const FReditusTurnEntry Entry = UpcomingEntries[OldIndex];
    UpcomingEntries.RemoveAt(OldIndex);
    UpcomingEntries.Insert(Entry, NewIndex);

    FReditusTurnChangeInfo Change;
    Change.Reason = EReditusTurnChangeReason::Advance;
    Change.AffectedUnit = Unit;
    Change.PreviousIndex = OldIndex;
    Change.NewIndex = NewIndex;
    Change.Amount = OldIndex - NewIndex;
    Change.SourceID = SourceID;

    EmitTimelineUpdate(OldSnapshot, Change);
    return true;
}

bool UReditusTurnManager::SwapUpcomingTurns(
    AActor* UnitA,
    AActor* UnitB,
    FName SourceID)
{
    if (!IsValid(UnitA) || !IsValid(UnitB) || UnitA == UnitB)
    {
        return false;
    }

    const int32 IndexA = FindNextEntryIndexForUnit(UnitA);
    const int32 IndexB = FindNextEntryIndexForUnit(UnitB);

    if (IndexA == INDEX_NONE || IndexB == INDEX_NONE)
    {
        return false;
    }

    const FReditusTurnTimelineSnapshot OldSnapshot = CaptureSnapshot();
    UpcomingEntries.Swap(IndexA, IndexB);

    FReditusTurnChangeInfo Change;
    Change.Reason = EReditusTurnChangeReason::Swap;
    Change.AffectedUnit = UnitA;
    Change.SecondaryUnit = UnitB;
    Change.PreviousIndex = IndexA;
    Change.NewIndex = IndexB;
    Change.SourceID = SourceID;

    EmitTimelineUpdate(OldSnapshot, Change);
    return true;
}

bool UReditusTurnManager::RedeployUnit(
    AActor* OutgoingUnit,
    AActor* IncomingUnit,
    FName SourceID)
{
    if (!IsValid(OutgoingUnit) || !IsValid(IncomingUnit) || OutgoingUnit == IncomingUnit)
    {
        return false;
    }

    bool bOutgoingExists = false;
    bool bIncomingExists = false;

    for (const FReditusTurnEntry& Entry : UpcomingEntries)
    {
        bOutgoingExists |= Entry.Unit == OutgoingUnit;
        bIncomingExists |= Entry.Unit == IncomingUnit;
    }

    if (!bOutgoingExists)
    {
        return false;
    }

    const FReditusTurnTimelineSnapshot OldSnapshot = CaptureSnapshot();

    if (bIncomingExists)
    {
        // Both already have future actions: swap ownership of every future slot.
        for (FReditusTurnEntry& Entry : UpcomingEntries)
        {
            if (Entry.Unit == OutgoingUnit)
            {
                Entry.Unit = IncomingUnit;
            }
            else if (Entry.Unit == IncomingUnit)
            {
                Entry.Unit = OutgoingUnit;
            }
        }
    }
    else
    {
        // Bench/reserve deployment: incoming takes all future slots of outgoing.
        for (FReditusTurnEntry& Entry : UpcomingEntries)
        {
            if (Entry.Unit == OutgoingUnit)
            {
                Entry.Unit = IncomingUnit;
            }
        }
    }

    FReditusTurnChangeInfo Change;
    Change.Reason = EReditusTurnChangeReason::Redeploy;
    Change.AffectedUnit = OutgoingUnit;
    Change.SecondaryUnit = IncomingUnit;
    Change.SourceID = SourceID;

    EmitTimelineUpdate(OldSnapshot, Change);
    return true;
}

bool UReditusTurnManager::ForceNextTurnToEndOfRound(
    AActor* Unit,
    FName SourceID)
{
    if (!IsValid(Unit))
    {
        return false;
    }

    const int32 OldIndex = FindNextEntryIndexForUnit(Unit);
    if (OldIndex == INDEX_NONE)
    {
        return false;
    }

    const int32 RoundIndex = UpcomingEntries[OldIndex].RoundIndex;
    const int32 NewIndex = FindLastIndexInRound(RoundIndex);

    if (NewIndex == INDEX_NONE || NewIndex == OldIndex)
    {
        return false;
    }

    const FReditusTurnTimelineSnapshot OldSnapshot = CaptureSnapshot();

    const FReditusTurnEntry Entry = UpcomingEntries[OldIndex];
    UpcomingEntries.RemoveAt(OldIndex);
    UpcomingEntries.Insert(Entry, NewIndex);

    FReditusTurnChangeInfo Change;
    Change.Reason = EReditusTurnChangeReason::ForceEndOfRound;
    Change.AffectedUnit = Unit;
    Change.PreviousIndex = OldIndex;
    Change.NewIndex = NewIndex;
    Change.Amount = NewIndex - OldIndex;
    Change.SourceID = SourceID;

    EmitTimelineUpdate(OldSnapshot, Change);
    return true;
}

bool UReditusTurnManager::InsertExtraTurn(
    AActor* Unit,
    int32 InsertIndex,
    int32 RoundIndex,
    FName SourceID)
{
    if (!IsValid(Unit))
    {
        return false;
    }

    const FReditusTurnTimelineSnapshot OldSnapshot = CaptureSnapshot();

    const int32 SafeIndex = FMath::Clamp(InsertIndex, 0, UpcomingEntries.Num());
    FReditusTurnEntry Entry = MakeNewEntry(Unit, RoundIndex, true, SourceID);
    UpcomingEntries.Insert(Entry, SafeIndex);

    FReditusTurnChangeInfo Change;
    Change.Reason = EReditusTurnChangeReason::ExtraTurnInserted;
    Change.AffectedUnit = Unit;
    Change.NewIndex = SafeIndex;
    Change.SourceID = SourceID;

    EmitTimelineUpdate(OldSnapshot, Change);
    return true;
}

int32 UReditusTurnManager::RemoveAllUpcomingTurnsForUnit(
    AActor* Unit,
    FName SourceID)
{
    if (!IsValid(Unit))
    {
        return 0;
    }

    const FReditusTurnTimelineSnapshot OldSnapshot = CaptureSnapshot();

    int32 RemovedCount = 0;
    for (int32 Index = UpcomingEntries.Num() - 1; Index >= 0; --Index)
    {
        if (UpcomingEntries[Index].Unit == Unit)
        {
            UpcomingEntries.RemoveAt(Index);
            ++RemovedCount;
        }
    }

    if (RemovedCount <= 0)
    {
        return 0;
    }

    FReditusTurnChangeInfo Change;
    Change.Reason = EReditusTurnChangeReason::UnitRemoved;
    Change.AffectedUnit = Unit;
    Change.Amount = RemovedCount;
    Change.SourceID = SourceID;

    EmitTimelineUpdate(OldSnapshot, Change);
    return RemovedCount;
}

bool UReditusTurnManager::ConsumeBlockedUpcomingTurn(
    AActor* Unit,
    const FReditusTurnEligibility& Eligibility)
{
    if (!IsValid(Unit) || Eligibility.bCanAct)
    {
        return false;
    }

    const int32 Index = FindNextEntryIndexForUnit(Unit);
    if (Index == INDEX_NONE)
    {
        return false;
    }

    const FReditusTurnTimelineSnapshot OldSnapshot = CaptureSnapshot();
    UpcomingEntries.RemoveAt(Index);

    FReditusTurnChangeInfo Change;
    Change.Reason = EReditusTurnChangeReason::TurnBlocked;
    Change.AffectedUnit = Unit;
    Change.PreviousIndex = Index;
    Change.SourceID = Eligibility.BlockingSourceID;

    EmitTimelineUpdate(OldSnapshot, Change);
    return true;
}

bool UReditusTurnManager::ConsumeBlockedCurrentTurn(
    AActor* Unit,
    const FReditusTurnEligibility& Eligibility)
{
    if (!IsValid(Unit) || Eligibility.bCanAct || CurrentUnit != Unit)
    {
        return false;
    }

    const FReditusTurnTimelineSnapshot OldSnapshot = CaptureSnapshot();
    AActor* PreviousUnit = CurrentUnit;
    CurrentUnit = nullptr;

    FReditusTurnChangeInfo Change;
    Change.Reason = EReditusTurnChangeReason::TurnBlocked;
    Change.AffectedUnit = Unit;
    Change.SourceID = Eligibility.BlockingSourceID;

    EmitTimelineUpdate(OldSnapshot, Change);
    OnCurrentTurnChanged.Broadcast(PreviousUnit, nullptr);
    return true;
}

bool UReditusTurnManager::HasStateChangedFromSnapshot(
    const FReditusTurnTimelineSnapshot& Snapshot) const
{
    if (Snapshot.CurrentUnit != CurrentUnit ||
        Snapshot.CurrentRoundIndex != CurrentRoundIndex ||
        Snapshot.Entries.Num() != UpcomingEntries.Num())
    {
        return true;
    }

    for (int32 Index = 0; Index < UpcomingEntries.Num(); ++Index)
    {
        const FReditusTurnEntry& A = Snapshot.Entries[Index];
        const FReditusTurnEntry& B = UpcomingEntries[Index];

        if (A.EntryID != B.EntryID ||
            A.Unit != B.Unit ||
            A.RoundIndex != B.RoundIndex ||
            A.bIsExtraTurn != B.bIsExtraTurn ||
            A.SourceID != B.SourceID)
        {
            return true;
        }
    }

    return false;
}

void UReditusTurnManager::EmitTimelineUpdate(
    const FReditusTurnTimelineSnapshot& OldSnapshot,
    const FReditusTurnChangeInfo& ChangeInfo)
{
    if (!HasStateChangedFromSnapshot(OldSnapshot))
    {
        return;
    }

    ++Revision;

    FReditusTurnTimelineUpdate Update;
    Update.OldTimeline = OldSnapshot;
    Update.NewTimeline = CaptureSnapshot();
    Update.Change = ChangeInfo;

    OnTurnTimelineChanged.Broadcast(Update);
}

int32 UReditusTurnManager::FindFirstIndexInRound(int32 RoundIndex) const
{
    for (int32 Index = 0; Index < UpcomingEntries.Num(); ++Index)
    {
        if (UpcomingEntries[Index].RoundIndex == RoundIndex)
        {
            return Index;
        }
    }

    return INDEX_NONE;
}

int32 UReditusTurnManager::FindLastIndexInRound(int32 RoundIndex) const
{
    for (int32 Index = UpcomingEntries.Num() - 1; Index >= 0; --Index)
    {
        if (UpcomingEntries[Index].RoundIndex == RoundIndex)
        {
            return Index;
        }
    }

    return INDEX_NONE;
}

void UReditusTurnManager::BuildReconciledTimeline(
    const TArray<AActor*>& CurrentRoundUnits,
    const TArray<AActor*>& NextRoundUnits,
    int32 InCurrentRoundIndex,
    AActor* InCurrentUnit,
    bool bRemoveOneCurrentOccurrence)
{
    const TArray<FReditusTurnEntry> PreviousEntries = UpcomingEntries;
    UpcomingEntries.Reset();

    CurrentUnit = InCurrentUnit;
    CurrentRoundIndex = InCurrentRoundIndex;

    TSet<int32> UsedPreviousEntryIDs;

    auto AddReconciledEntry = [this, &PreviousEntries, &UsedPreviousEntryIDs](
        AActor* Unit,
        int32 RoundIndex)
    {
        if (!IsValid(Unit))
        {
            return;
        }

        const FReditusTurnEntry* ReusableEntry = nullptr;

        for (const FReditusTurnEntry& Previous : PreviousEntries)
        {
            if (Previous.Unit == Unit &&
                Previous.RoundIndex == RoundIndex &&
                !UsedPreviousEntryIDs.Contains(Previous.EntryID))
            {
                ReusableEntry = &Previous;
                break;
            }
        }

        if (ReusableEntry)
        {
            FReditusTurnEntry Reused = *ReusableEntry;
            Reused.bIsExtraTurn = false;
            Reused.SourceID = NAME_None;
            UpcomingEntries.Add(Reused);
            UsedPreviousEntryIDs.Add(Reused.EntryID);
        }
        else
        {
            UpcomingEntries.Add(
                MakeNewEntry(Unit, RoundIndex, false, NAME_None)
            );
        }
    };

    bool bCurrentOccurrenceRemoved = false;

    for (AActor* Unit : CurrentRoundUnits)
    {
        if (bRemoveOneCurrentOccurrence &&
            !bCurrentOccurrenceRemoved &&
            Unit == InCurrentUnit)
        {
            bCurrentOccurrenceRemoved = true;
            continue;
        }

        AddReconciledEntry(Unit, InCurrentRoundIndex);
    }

    for (AActor* Unit : NextRoundUnits)
    {
        AddReconciledEntry(Unit, InCurrentRoundIndex + 1);
    }
}

FReditusTurnEntry UReditusTurnManager::MakeNewEntry(
    AActor* Unit,
    int32 RoundIndex,
    bool bIsExtraTurn,
    FName SourceID)
{
    FReditusTurnEntry Entry;
    Entry.EntryID = NextEntryID++;
    Entry.Unit = Unit;
    Entry.RoundIndex = RoundIndex;
    Entry.bIsExtraTurn = bIsExtraTurn;
    Entry.SourceID = SourceID;
    return Entry;
}
