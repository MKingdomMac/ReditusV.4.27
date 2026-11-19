#include "ReditusQuestSubsystem.h"
#include "ReditusQuestCatalog.h"
#include "ReditusQuestUI.h"
#include "ReditusQuestClock.h"
#include "ReditusQuestMarkerPolicy.h"
#include "ReditusQuestItemRouting.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include <algorithm>

DEFINE_LOG_CATEGORY_STATIC(LogReditusQuests, Log, All);

namespace
{
    std::string Narrow(const FString& Value) { return std::string(TCHAR_TO_UTF8(*Value)); }
    FString Wide(const std::string& Value) { return FString(UTF8_TO_TCHAR(Value.c_str())); }
    std::string Identifier(FName Value) { return Narrow(Value.ToString().ToLower()); }
    EReditusQuestState State(ReditusQuestRuntime::State Value)
    {
        using S = ReditusQuestRuntime::State;
        switch (Value)
        {
        case S::Available: return EReditusQuestState::Available;
        case S::Active: return EReditusQuestState::Active;
        case S::Ready: return EReditusQuestState::ReadyToDeliver;
        case S::Completed: return EReditusQuestState::Completed;
        case S::Failed: return EReditusQuestState::Failed;
        default: return EReditusQuestState::Unavailable;
        }
    }
    ReditusQuestRuntime::State State(EReditusQuestState Value)
    {
        using S = ReditusQuestRuntime::State;
        switch (Value)
        {
        case EReditusQuestState::Active: return S::Active;
        case EReditusQuestState::ReadyToDeliver: return S::Ready;
        case EReditusQuestState::Completed: return S::Completed;
        case EReditusQuestState::Failed: return S::Failed;
        default: return S::Unavailable;
        }
    }
    const FReditusQuestDefinition* Definition(FDataTableRowHandle Row)
    {
        return ReditusQuestCatalog::Definition(Row);
    }
    bool ValidItemRow(FDataTableRowHandle Row)
    { EReditusQuestItemKind Kind; return ReditusQuestItemRouting::ResolveKind(Row, Kind); }
    bool ValidItems(const TArray<FReditusQuestItemAmount>& Items)
    {
        TSet<FString> IDs;
        for (const auto& Item : Items)
        {
            EReditusQuestItemKind Kind;
            if (!ReditusQuestItemRouting::ResolveKind(Item.ItemRow, Kind) || Item.Amount <= 0) return false;
            const FString InventoryID = (FString::FromInt(int32(Kind)) + TEXT(":") + Item.ItemRow.RowName.ToString()).ToLower();
            if (IDs.Contains(InventoryID)) return false;
            IDs.Add(InventoryID);
        }
        return true;
    }
    bool SameItems(const TArray<FReditusQuestItemAmount>& A, const TArray<FReditusQuestItemAmount>& B)
    {
        if (A.Num() != B.Num()) return false;
        for (int32 I = 0; I < A.Num(); ++I)
            if (A[I].ItemRow.DataTable != B[I].ItemRow.DataTable || A[I].ItemRow.RowName != B[I].ItemRow.RowName || A[I].Amount != B[I].Amount) return false;
        return true;
    }
}

FString UReditusQuestSubsystem::Key(FDataTableRowHandle Quest)
{
    if (Quest.DataTable && Quest.DataTable->GetRowStruct() == FReditusQuestCatalogRow::StaticStruct())
        Quest = ReditusQuestCatalog::Canonical(Quest);
    return Quest.DataTable && !Quest.RowName.IsNone()
        ? (Quest.DataTable->GetPathName() + TEXT(":") + Quest.RowName.ToString()).ToLower() : FString();
}

UReditusQuestSubsystem* UReditusQuestSubsystem::GetQuestSystem(const UObject* WorldContextObject)
{
    if (!IsValid(WorldContextObject)) return nullptr;
    auto* Instance = UGameplayStatics::GetGameInstance(WorldContextObject);
    return Instance ? Instance->GetSubsystem<UReditusQuestSubsystem>() : nullptr;
}

bool UReditusQuestSubsystem::EnsureDefinition(FDataTableRowHandle Quest, FString& Error, TSet<FString>& Visiting)
{
    const FString ID = Key(Quest);
    if (ID.IsEmpty()) { Error = TEXT("Select a valid compiled quest catalogue row or DT_Quests row."); return false; }
    if (Runtime.FindDefinition(Narrow(ID))) return true;
    const auto* Row = Definition(Quest);
    if (!Row) { Error = TEXT("Quest row is missing or uses the wrong row structure: ") + ID; return false; }
    if (Visiting.Contains(ID)) { Error = TEXT("Cyclic quest prerequisites: ") + ID; return false; }
    if (Row->Rewards.Gold < 0 || Row->Rewards.Experience < 0
        || !ValidItems(Row->Rewards.Items) || !ValidItems(Row->TurnInItems))
    { Error = TEXT("Invalid reward/cost counts or duplicate item IDs: ") + ID; return false; }
    Visiting.Add(ID);
    for (const auto& Required : Row->RequiredCompletedQuests)
        if (!EnsureDefinition(Required, Error, Visiting)) { Visiting.Remove(ID); return false; }
    ReditusQuestRuntime::Definition D;
    if (!ReditusQuestCatalog::BuildDefinition(*Row, ID, D, Error)) { Visiting.Remove(ID); return false; }
    std::string Why;
    const bool Valid = Runtime.AddDefinition(D, Why);
    Visiting.Remove(ID);
    if (!Valid) { Error = ID + TEXT(": ") + Wide(Why); return false; }
    QuestRows.Add(ID, ReditusQuestCatalog::Canonical(Quest)); return true;
}

bool UReditusQuestSubsystem::Prepare(FDataTableRowHandle Quest)
{
    FString Error; TSet<FString> Visiting;
    if (EnsureDefinition(Quest, Error, Visiting)) return true;
    UE_LOG(LogReditusQuests, Warning, TEXT("%s"), *Error); return false;
}

bool UReditusQuestSubsystem::StartInternal(FDataTableRowHandle Quest, bool IgnorePrerequisites)
{
    if (!Prepare(Quest) || !Runtime.Start(Narrow(Key(Quest)), IgnorePrerequisites)) return false;
    AddedNotices.AddUnique(Quest); return true;
}

bool UReditusQuestSubsystem::StartQuest(FDataTableRowHandle Quest, bool IgnorePrerequisites)
{
    if (bImporting || !StartInternal(Quest, IgnorePrerequisites)) return false;
    RefreshQuestInventory(UGameplayStatics::GetPlayerController(GetGameInstance(), 0));
    FlushChanges(); return true;
}

bool UReditusQuestSubsystem::ReportQuestEvent(EReditusQuestObjectiveKind Kind, FName TargetID, FName UniqueEventID, int32 Amount)
{
    if (bImporting || TargetID.IsNone() || UniqueEventID.IsNone()
        || Kind == EReditusQuestObjectiveKind::TimerElapsed || Kind == EReditusQuestObjectiveKind::Choice
        || Kind == EReditusQuestObjectiveKind::StepAction
        || uint8(Kind) > uint8(EReditusQuestObjectiveKind::StepAction)) return false;
    const bool Changed = Runtime.Signal(static_cast<ReditusQuestRuntime::Kind>(Kind),
        Identifier(TargetID), Amount, Identifier(UniqueEventID));
    FlushChanges(); return Changed;
}

bool UReditusQuestSubsystem::GetQuestItemCount(APlayerController* Player, FDataTableRowHandle ItemRow,
    EReditusQuestItemKind Kind, int32& Count) const
{
    Count = 0;
    (void)Kind; // Retained input for existing Blueprint pins; the row is authoritative.
    EReditusQuestItemKind ResolvedKind;
    if (!IsValid(Player) || !ValidItemRow(ItemRow) || !ReditusQuestItemRouting::ResolveKind(ItemRow, ResolvedKind)) return false;
    FName PropertyName;
    switch (ResolvedKind)
    {
    case EReditusQuestItemKind::Usable: PropertyName = TEXT("usableItems"); break;
    case EReditusQuestItemKind::Misc: PropertyName = TEXT("miscItems"); break;
    case EReditusQuestItemKind::KeyItem: PropertyName = TEXT("KeyItems"); break;
    case EReditusQuestItemKind::Equipment: PropertyName = TEXT("equipment"); break;
    default: return false;
    }
    auto* Map = FindFProperty<FMapProperty>(Player->GetClass(), PropertyName);
    auto* ItemKey = Map ? CastField<FNameProperty>(Map->KeyProp) : nullptr;
    auto* Quantity = Map ? CastField<FIntProperty>(Map->ValueProp) : nullptr;
    if (!Map || !ItemKey || !Quantity) return false;
    FScriptMapHelper Helper(Map, Map->ContainerPtrToValuePtr<void>(Player));
    for (int32 I = 0; I < Helper.GetMaxIndex(); ++I)
        if (Helper.IsValidIndex(I) && ItemKey->GetPropertyValue(Helper.GetKeyPtr(I)) == ItemRow.RowName)
        { Count = FMath::Max(0, Quantity->GetPropertyValue(Helper.GetValuePtr(I))); break; }
    return true;
}

bool UReditusQuestSubsystem::RefreshQuestInventory(APlayerController* Player)
{
    if (bImporting || !IsValid(Player)) return false;
    bool Any = false;
    const auto Records = Runtime.GetRecords();
    for (const auto& Pair : Records)
    {
        const FString ID = Wide(Pair.first);
        if (Deliveries.Contains(ID)) continue; // Inventory handler can remove turn-in costs before confirming.
        const auto* Handle = QuestRows.Find(ID);
        const auto* Row = Handle ? Definition(*Handle) : nullptr;
        if (!Row || (Pair.second.Status != ReditusQuestRuntime::State::Active
            && Pair.second.Status != ReditusQuestRuntime::State::Ready)) continue;
        for (const auto& S : Row->Stages)
            if (Identifier(S.StageID) == Pair.second.StageId)
                for (const auto& T : S.Objectives)
                {
                    const auto* Current = Runtime.FindRecord(Pair.first);
                    if (!Current || Current->StageId != Pair.second.StageId) break;
                    int32 Amount = 0;
                    if (T.Kind == EReditusQuestObjectiveKind::CollectItem && GetQuestItemCount(Player, T.ItemRow, T.ItemKind, Amount))
                        Any = Runtime.SetInventory(Pair.first, Identifier(T.ObjectiveID), Amount) || Any;
                }
    }
    FlushChanges(); return Any;
}

bool UReditusQuestSubsystem::ReadQuest(FDataTableRowHandle Quest, FReditusQuestSnapshot& Snapshot)
{
    Snapshot = FReditusQuestSnapshot();
    if (!Prepare(Quest)) return false;
    const auto* Row = Definition(Quest);
    const auto* Record = Runtime.FindRecord(Narrow(Key(Quest)));
    Snapshot.State = State(Runtime.GetState(Narrow(Key(Quest))));
    Snapshot.IsMainQuest = Row->IsMainQuest; Snapshot.Goal = Row->Stages.Num();
    if (Record)
    {
        Snapshot.StageID = FName(*Wide(Record->StageId));
        for (int32 I = 0; I < Row->Stages.Num(); ++I)
            if (Row->Stages[I].StageID == Snapshot.StageID) { Snapshot.Progress = I; break; }
        if (Snapshot.State == EReditusQuestState::ReadyToDeliver || Snapshot.State == EReditusQuestState::Completed)
            Snapshot.Progress = Snapshot.Goal;
    }
    return true;
}

bool UReditusQuestSubsystem::GetQuestNPCMarkerState(FDataTableRowHandle Quest, FReditusQuestMarkerState& Marker)
{
    Marker = FReditusQuestMarkerState();
    FReditusQuestSnapshot Snapshot;
    if (!Quest.DataTable || Quest.RowName.IsNone() || !ReadQuest(Quest, Snapshot)) return false;
    const auto* Row = Definition(Quest);
    if (!Row) return false;
    using namespace ReditusQuestMarkerPolicy;
    Rules Visibility;
    Visibility.Available = Row->Markers.ShowWhenAvailable;
    Visibility.Active = Row->Markers.ShowWhileActive;
    Visibility.Ready = Row->Markers.ShowWhenReadyToTurnIn;
    Override StageRule = Override::Inherit;
    for (const auto& Stage : Row->Stages)
        if (Stage.StageID == Snapshot.StageID)
        { StageRule = static_cast<Override>(Stage.NPCMarkerVisibility); break; }
    Marker.Visible = Visible(static_cast<ReditusQuestMarkerPolicy::State>(Snapshot.State), Visibility, StageRule);
    Marker.MinimapVisible = Marker.Visible && Row->Markers.ShowOnMinimap;
    Marker.OverheadVisible = Marker.Visible && Row->Markers.ShowOverhead;
    Marker.IsMainQuest = Row->IsMainQuest;
    Marker.Title = Row->Title;
    return true;
}

bool UReditusQuestSubsystem::GetQuestDetails(FDataTableRowHandle Quest, FReditusQuestDefinition& OutDefinition,
    FReditusQuestSnapshot& Snapshot, TArray<FReditusQuestObjectiveStatus>& Objectives)
{
    OutDefinition = FReditusQuestDefinition(); Objectives.Reset();
    if (!ReadQuest(Quest, Snapshot)) return false;
    OutDefinition = *Definition(Quest);
    const auto Relevant = Runtime.RelevantStages(Narrow(Key(Quest)));
    for (const auto& S : OutDefinition.Stages)
        if (Relevant.count(Identifier(S.StageID))) for (const auto& T : S.Objectives)
        {
            FReditusQuestObjectiveStatus Status;
            Status.StageID = S.StageID; Status.Objective = T;
            Status.Count = Runtime.GetCount(Narrow(Key(Quest)), Identifier(S.StageID), Identifier(T.ObjectiveID));
            Status.Complete = Status.Count >= T.RequiredCount;
            Status.CurrentStage = S.StageID == Snapshot.StageID;
            Objectives.Add(Status);
        }
    return true;
}

TArray<FDataTableRowHandle> UReditusQuestSubsystem::GetStartedQuests() const
{
    TArray<FDataTableRowHandle> Result;
    for (const auto& Pair : Runtime.GetRecords()) if (const auto* Row = QuestRows.Find(Wide(Pair.first))) Result.Add(*Row);
    return Result;
}

void UReditusQuestSubsystem::FinishInternal(FDataTableRowHandle Quest)
{
    const std::string ID = Narrow(Key(Quest));
    if (!Runtime.Finish(ID)) return;
    CompletedNotices.AddUnique(Quest);
    Runtime.Acknowledge(ID); // The external delivery has already succeeded, or the delivery was empty.
    if (const auto* Row = Definition(Quest))
    {
        for (const auto& Next : Row->StartQuestsOnCompletion) StartInternal(Next, false);
    }
}

bool UReditusQuestSubsystem::TurnInQuest(FDataTableRowHandle Quest, APlayerController* Player)
{
    const bool AllowBuiltIn = bAllowBuiltInFetchDelivery;
    TGuardValue<bool> ResetAllowance(bAllowBuiltInFetchDelivery, false); // Never inherit this permission in a custom handler.
    if (bImporting || !Prepare(Quest) || Deliveries.Contains(Key(Quest))) return false;
    const auto* Row = Definition(Quest);
    const auto* Record = Runtime.FindRecord(Narrow(Key(Quest)));
    if (!Record || Record->Status != ReditusQuestRuntime::State::Ready) return false;
    // A stale displayed Ready state cannot hand in items that are no longer owned.
    for (const auto& S : Row->Stages)
        if (Identifier(S.StageID) == Record->StageId)
            for (const auto& T : S.Objectives)
            {
                int32 Count = 0;
                if (!T.Optional && T.Kind == EReditusQuestObjectiveKind::CollectItem
                    && (!GetQuestItemCount(Player, T.ItemRow, T.ItemKind, Count) || Count < T.RequiredCount))
                { RefreshQuestInventory(Player); return false; }
            }
    for (const auto& Cost : Row->TurnInItems)
    {
        int32 Count = 0;
        if (!GetQuestItemCount(Player, Cost.ItemRow, Cost.Kind, Count) || Count < Cost.Amount) return false;
    }
    const bool Empty = Row->TurnInItems.Num() == 0 && Row->Rewards.Items.Num() == 0
        && Row->Rewards.Gold == 0 && Row->Rewards.Experience == 0;
    if (Empty) { FinishInternal(Quest); FlushChanges(); return true; }
    if (!OnQuestDeliveryRequested.IsBound() && !AllowBuiltIn)
    {
        UE_LOG(LogReditusQuests, Warning, TEXT("Bind one central OnQuestDeliveryRequested handler for costs/rewards before turning in %s."), *Key(Quest));
        return false;
    }
    FReditusQuestDelivery Delivery;
    Delivery.Quest = Quest; Delivery.ReceiptID = FGuid::NewGuid();
    Delivery.ItemsToRemove = Row->TurnInItems; Delivery.Rewards = Row->Rewards;
    if (!ReditusQuestItemRouting::NormalizeItems(Delivery.ItemsToRemove)
        || !ReditusQuestItemRouting::NormalizeItems(Delivery.Rewards.Items)) return false;
    Deliveries.Add(Key(Quest), Delivery);
    if (OnQuestDeliveryRequested.IsBound()) { OnQuestDeliveryRequested.Broadcast(Delivery); return true; }
    FString Error;
    if (ApplyBuiltInFetchDelivery(Player, Delivery, Error)) return true;
    CancelQuestDelivery(Quest, Delivery.ReceiptID);
    UE_LOG(LogReditusQuests, Warning, TEXT("Fetch hand-in failed for %s: %s"), *Key(Quest), *Error);
    return false;
}

bool UReditusQuestSubsystem::ConfirmQuestDelivery(FDataTableRowHandle Quest, FGuid ReceiptID)
{
    const auto* Delivery = Deliveries.Find(Key(Quest));
    const auto* Record = Runtime.FindRecord(Narrow(Key(Quest)));
    if (bImporting || !Delivery || Delivery->ReceiptID != ReceiptID
        || !Record || (Delivery->Kind == EReditusQuestDeliveryKind::TurnIn && Record->Status != ReditusQuestRuntime::State::Ready)) return false;
    const FReditusQuestDelivery Committed = *Delivery;
    if (Committed.Kind == EReditusQuestDeliveryKind::StepAction)
    {
        if (!Runtime.ApplyAction(Narrow(Key(Quest)), Identifier(Committed.StageID), Identifier(Committed.ActionID))) return false;
        Deliveries.Remove(Key(Quest)); FlushChanges(); return true;
    }
    Deliveries.Remove(Key(Quest)); FinishInternal(Quest); FlushChanges(); return true;
}

bool UReditusQuestSubsystem::CancelQuestDelivery(FDataTableRowHandle Quest, FGuid ReceiptID)
{
    const auto* Delivery = Deliveries.Find(Key(Quest));
    if (bImporting || !Delivery || Delivery->ReceiptID != ReceiptID) return false;
    Deliveries.Remove(Key(Quest));
    RefreshQuestInventory(UGameplayStatics::GetPlayerController(GetGameInstance(), 0)); return true;
}

TArray<FReditusQuestDelivery> UReditusQuestSubsystem::GetPendingDeliveries() const
{
    TArray<FReditusQuestDelivery> Result;
    Deliveries.GenerateValueArray(Result); return Result;
}

void UReditusQuestSubsystem::FlushChanges()
{
    if (bFlushing || bImporting) return;
    TGuardValue<bool> Guard(bFlushing, true);
    for (;;)
    {
        const auto Lifecycle = Runtime.TakeStageEvents();
        const auto Entered = Runtime.TakeEnteredStages();
        for (const auto& Event : Lifecycle)
        {
            const auto* Handle = QuestRows.Find(Wide(Event.Key));
            const auto* Row = Handle ? Definition(*Handle) : nullptr;
            if (!Row) continue;
            if (Event.Type == ReditusQuestRuntime::StageEventKind::QuestComplete)
                for (const auto& Sequence : Row->OnCompleteSequences) PlayQuestLevelSequence(Sequence);
            else for (const auto& Stage : Row->Stages) if (Identifier(Stage.StageID) == Event.StageId)
            {
                const auto& Sequences = Event.Type == ReditusQuestRuntime::StageEventKind::Enter
                    ? Stage.OnEnterSequences : Stage.OnCompleteSequences;
                for (const auto& Sequence : Sequences) PlayQuestLevelSequence(Sequence);
            }
        }
        const auto Expired = Runtime.TakeTimeouts();
        for (const auto& Pair : Expired)
        {
            const auto* Handle = QuestRows.Find(Wide(Pair.first));
            const auto* Row = Handle ? Definition(*Handle) : nullptr;
            if (Row) for (const auto& Stage : Row->Stages)
                if (Identifier(Stage.StageID) == Pair.second)
                {
                    for (const auto& Sequence : Stage.Timer.OnTimeoutSequences) PlayQuestLevelSequence(Sequence);
                    for (const auto& Next : Stage.Timer.StartQuestsOnTimeout) StartInternal(Next, false);
                }
        }
        // Also evaluate newly started quests/current inventory stages before notifying UI.
        RefreshQuestInventory(UGameplayStatics::GetPlayerController(GetGameInstance(), 0));
        const auto Tasks = Runtime.TakeObjectiveMilestones();
        for (const auto& Pair : Tasks)
        {
            const auto* Handle = QuestRows.Find(Wide(Pair.first));
            const auto* Row = Handle ? Definition(*Handle) : nullptr;
            if (Row) for (const auto& Stage : Row->Stages)
                for (const auto& Task : Stage.Objectives)
                    if (ReditusQuestRuntime::Counter(Identifier(Stage.StageID), Identifier(Task.ObjectiveID)) == Pair.second)
                        for (const auto& Next : Task.StartQuestsOnCompletion) StartInternal(Next, false);
        }
        const auto Reached = Runtime.TakeMilestones();
        for (const auto& Pair : Reached)
        {
            const auto* Handle = QuestRows.Find(Wide(Pair.first));
            const auto* Row = Handle ? Definition(*Handle) : nullptr;
            if (Row) for (const auto& Stage : Row->Stages)
                if (Identifier(Stage.StageID) == Pair.second)
                {
                    for (const auto& Next : Stage.StartQuestsOnCompletion) StartInternal(Next, false);
                }
        }
        const auto Changed = Runtime.TakeChanged();
        if (Lifecycle.empty() && Entered.empty() && Expired.empty() && Tasks.empty() && Reached.empty() && Changed.empty()) break;
        for (const auto& ID : Changed)
            if (const auto* Handle = QuestRows.Find(Wide(ID)))
            {
                const auto Row = *Handle; // Delegates can register definitions and reallocate QuestRows.
                const bool Added = AddedNotices.Remove(Row) > 0;
                const bool Completed = CompletedNotices.Remove(Row) > 0;
                const auto* Record = Runtime.FindRecord(ID);
                const bool Failed = Record && Record->Status == ReditusQuestRuntime::State::Failed;
                OnQuestChanged.Broadcast(Row);
                NotifyQuest(Row, Failed ? EReditusQuestNoticeKind::Failed : Completed ? EReditusQuestNoticeKind::Completed
                    : Added ? EReditusQuestNoticeKind::Added : EReditusQuestNoticeKind::Updated);
            }
    }
}

FReditusQuestSaveData UReditusQuestSubsystem::ExportQuestState() const
{
    FReditusQuestSaveData Data;
    for (const auto& NPC : WorldProgress.GetRemoved())
    {
        FReditusQuestRemovedNPC Saved; Saved.NPCID = FName(*Wide(NPC.first));
        Saved.Reason = static_cast<EReditusNPCRemovalReason>(NPC.second); Data.RemovedNPCs.Add(Saved);
    }
    for (const auto& Character : WorldProgress.GetRecruitments()) Data.RecordedRecruitments.Add(FName(*Wide(Character)));
    for (const auto& Pair : Runtime.GetRecords())
    {
        const auto* Handle = QuestRows.Find(Wide(Pair.first));
        if (!Handle) continue;
        const auto& R = Pair.second;
        FReditusQuestSavedRecord Saved;
        Saved.Quest = *Handle; Saved.DefinitionVersion = R.Version;
        Saved.State = State(R.Status); Saved.StageID = FName(*Wide(R.StageId));
        Saved.TimerRemainingSeconds = float(R.TimerRemaining);
        for (const auto& Stage : R.Path) Saved.VisitedStages.Add(FName(*Wide(Stage)));
        for (const auto& Choice : R.Choices)
        {
            FReditusQuestSavedChoice C; C.ChoiceID = FName(*Wide(Choice.first));
            C.OptionID = FName(*Wide(Choice.second.Option)); C.Value = Choice.second.Value; Saved.Choices.Add(C);
        }
        for (const auto& Action : R.Actions) Saved.AppliedStepActions.Add(FName(*Wide(Action)));
        for (const auto& Counter : R.Counts)
        {
            FString StageID, ObjectiveID;
            Wide(Counter.first).Split(TEXT("/"), &StageID, &ObjectiveID);
            FReditusQuestSavedCounter C; C.StageID = FName(*StageID); C.ObjectiveID = FName(*ObjectiveID);
            C.Count = Counter.second; Saved.Counters.Add(C);
        }
        for (const auto& Event : R.Events) Saved.SeenEventIDs.Add(FName(*Wide(Event)));
        for (const auto& Stage : R.Milestones) Saved.ReachedStages.Add(FName(*Wide(Stage)));
        for (const auto& Task : R.ObjectiveMilestones) Saved.ReachedObjectives.Add(FName(*Wide(Task)));
        if (const auto* Delivery = Deliveries.Find(Wide(Pair.first)))
        { Saved.HasPendingDelivery = true; Saved.PendingDelivery = *Delivery; }
        Data.Quests.Add(Saved);
    }
    return Data;
}

bool UReditusQuestSubsystem::ImportQuestState(const FReditusQuestSaveData& Data, FText& Error)
{
    Error = FText::GetEmpty();
    if (bFlushing || bImporting || bApplyingNPCAction || PendingNPCBattles.Num() > 0 || HasNPCStoryWork() || SequencePlayer || SequenceQueue.Num() > 0 || (Data.FormatVersion != 1 && Data.FormatVersion != 2))
    { Error = FText::FromString(TEXT("Quest save is busy or its format version is unsupported.")); return false; }
    TGuardValue<bool> Guard(bImporting, true);
    std::vector<ReditusQuestRuntime::Record> Records;
    TMap<FString, FReditusQuestDelivery> Pending;
    for (const auto& Saved : Data.Quests)
    {
        FString Why; TSet<FString> Visiting;
        if (!EnsureDefinition(Saved.Quest, Why, Visiting)) { Error = FText::FromString(Why); return false; }
        const FString ID = Key(Saved.Quest);
        ReditusQuestRuntime::Record R; R.Key = Narrow(ID); R.Version = Saved.DefinitionVersion;
        R.TimerRemaining = Saved.TimerRemainingSeconds;
        R.Status = State(Saved.State); R.StageId = Saved.StageID.IsNone() ? std::string() : Identifier(Saved.StageID);
        for (FName Stage : Saved.VisitedStages)
        {
            if (Stage.IsNone()) { Error = FText::FromString(TEXT("Empty visited stage ID.")); return false; }
            R.Path.push_back(Identifier(Stage));
        }
        for (const auto& Choice : Saved.Choices)
        {
            ReditusQuestRuntime::Selection C; C.Option = Choice.OptionID.IsNone() ? std::string() : Identifier(Choice.OptionID); C.Value = Choice.Value;
            if (Choice.ChoiceID.IsNone() || !R.Choices.emplace(Identifier(Choice.ChoiceID), C).second)
            { Error = FText::FromString(TEXT("Empty or duplicate saved quest choice.")); return false; }
        }
        for (FName Action : Saved.AppliedStepActions)
            if (Action.IsNone() || !R.Actions.insert(Identifier(Action)).second)
            { Error = FText::FromString(TEXT("Empty or duplicate step action receipt.")); return false; }
        for (const auto& Counter : Saved.Counters)
        {
            const auto CounterID = ReditusQuestRuntime::Counter(Identifier(Counter.StageID), Identifier(Counter.ObjectiveID));
            if (!R.Counts.emplace(CounterID, Counter.Count).second)
            { Error = FText::FromString(TEXT("Duplicate objective counter in quest save.")); return false; }
        }
        for (FName Event : Saved.SeenEventIDs)
        {
            if (Event.IsNone()) { Error = FText::FromString(TEXT("Empty quest event ID in save.")); return false; }
            R.Events.insert(Identifier(Event));
        }
        for (FName Stage : Saved.ReachedStages) R.Milestones.insert(Identifier(Stage));
        for (FName Task : Saved.ReachedObjectives) R.ObjectiveMilestones.insert(Identifier(Task));
        if (Saved.HasPendingDelivery)
        {
            auto D = Saved.PendingDelivery; const auto* Authored = Definition(Saved.Quest);
            if (D.Kind != EReditusQuestDeliveryKind::TurnIn || !D.StageID.IsNone() || !D.ActionID.IsNone()
                || Saved.State != EReditusQuestState::ReadyToDeliver || !D.ReceiptID.IsValid()
                || Key(D.Quest) != ID || !SameItems(D.ItemsToRemove, Authored->TurnInItems)
                || !SameItems(D.Rewards.Items, Authored->Rewards.Items)
                || D.Rewards.Gold != Authored->Rewards.Gold || D.Rewards.Experience != Authored->Rewards.Experience)
            { Error = FText::FromString(TEXT("Invalid or changed quest delivery receipt.")); return false; }
            if (!ReditusQuestItemRouting::NormalizeItems(D.ItemsToRemove)
                || !ReditusQuestItemRouting::NormalizeItems(D.Rewards.Items))
            { Error = FText::FromString(TEXT("Quest delivery has an unrecognized item table.")); return false; }
            Pending.Add(ID, D);
        }
        Records.push_back(R);
    }
    auto WorldCandidate = WorldProgress;
    std::vector<ReditusQuestWorld::RemovedNPC> Removed;
    std::vector<std::string> Recruited;
    for (const auto& NPC : Data.RemovedNPCs)
    {
        ReditusQuestWorld::RemovedNPC Entry;
        Entry.Id = NPC.NPCID.IsNone() ? std::string() : Identifier(NPC.NPCID);
        Entry.Reason = static_cast<ReditusQuestWorld::Removal>(NPC.Reason); Removed.push_back(Entry);
    }
    for (FName Character : Data.RecordedRecruitments)
        Recruited.push_back(Character.IsNone() ? std::string() : Identifier(Character));
    std::string WorldError;
    if (!WorldCandidate.Import(Removed, Recruited, WorldError))
    { Error = FText::FromString(Wide(WorldError)); return false; }
    auto Candidate = Runtime; std::string Why;
    if (!Candidate.Import(Records, Why)) { Error = FText::FromString(Wide(Why)); return false; }
    Runtime = MoveTemp(Candidate); WorldProgress = MoveTemp(WorldCandidate); Deliveries = MoveTemp(Pending);
    AddedNotices.Empty(); CompletedNotices.Empty(); DefeatEventIDs.Empty();
    if (NotificationView.IsValid()) NotificationView->ClearNotices();
    // No start/completion events, quest chains or reward callbacks are replayed by loading.
    bImporting = false;
    OnQuestStateRestored.Broadcast(); return true;
}

void UReditusQuestSubsystem::ResetQuestStateForNewGame()
{
    if (bImporting || bFlushing || bApplyingNPCAction) return;
    TGuardValue<bool> Guard(bImporting, true);
    CancelNPCStoryWork(); SequenceQueue.Empty(); EndSequence(false);
    Runtime.Reset(); WorldProgress.Reset(); PendingNPCBattles.Empty(); Deliveries.Empty(); AddedNotices.Empty(); CompletedNotices.Empty(); DefeatEventIDs.Empty();
    if (NotificationView.IsValid()) NotificationView->ClearNotices();
    bImporting = false; OnQuestStateRestored.Broadcast();
}

bool UReditusQuestSubsystem::WriteQuestStateToSave(USaveGame* SaveObject)
{
    if (!CanSaveQuestState()) return false; // Do not save a half-applied delivery.
    auto* Field = IsValid(SaveObject) ? FindFProperty<FStructProperty>(SaveObject->GetClass(), TEXT("QuestState")) : nullptr;
    if (!Field || Field->Struct != FReditusQuestSaveData::StaticStruct())
    { UE_LOG(LogReditusQuests, Warning, TEXT("Save object needs a QuestState variable of type Reditus Quest Save Data.")); return false; }
    const FReditusQuestSaveData Data = ExportQuestState();
    Field->CopyCompleteValue(Field->ContainerPtrToValuePtr<void>(SaveObject), &Data); return true;
}

bool UReditusQuestSubsystem::ReadQuestStateFromSave(USaveGame* SaveObject, FText& Error)
{
    auto* Field = IsValid(SaveObject) ? FindFProperty<FStructProperty>(SaveObject->GetClass(), TEXT("QuestState")) : nullptr;
    if (!Field || Field->Struct != FReditusQuestSaveData::StaticStruct())
    { Error = FText::FromString(TEXT("Save object needs a QuestState variable of type Reditus Quest Save Data.")); return false; }
    const FReditusQuestSaveData Data = *Field->ContainerPtrToValuePtr<FReditusQuestSaveData>(SaveObject);
    return ImportQuestState(Data, Error);
}

bool UReditusQuestSubsystem::RetryFailedQuest(FDataTableRowHandle Quest)
{
    if (bImporting || !Prepare(Quest) || !Runtime.RetryFailed(Narrow(Key(Quest)))) return false;
    AddedNotices.AddUnique(Quest); FlushChanges(); return true;
}

bool UReditusQuestSubsystem::IsTickable() const
{
    if (IsTemplate() || bSubsystemStopped) return false;
    const UWorld* World = GetTickableGameObjectWorld();
    return World && World->IsGameWorld();
}
UWorld* UReditusQuestSubsystem::GetTickableGameObjectWorld() const
{ const auto* Instance = Cast<UGameInstance>(GetOuter()); return Instance ? Instance->GetWorld() : nullptr; }
TStatId UReditusQuestSubsystem::GetStatId() const
{ RETURN_QUICK_DECLARE_CYCLE_STAT(UReditusQuestSubsystem, STATGROUP_Tickables); }

void UReditusQuestSubsystem::Tick(float DeltaTime)
{
    if (bImporting) return;
    UWorld* World = GetTickableGameObjectWorld();
    if (!World || World->IsPaused()) return;
    for (auto It = PendingNPCBattles.CreateIterator(); It; ++It)
        if (It.Value().RequestWorld.Get() != World) It.RemoveCurrent();
    PumpSequences();
    InventoryRefreshSeconds -= DeltaTime;
    if (InventoryRefreshSeconds <= 0.f)
    {
        InventoryRefreshSeconds = 0.25f;
        for (auto It = DefeatEventIDs.CreateIterator(); It; ++It)
            if (!It.Key().IsValid()) It.RemoveCurrent();
        RefreshQuestInventory(UGameplayStatics::GetPlayerController(GetGameInstance(), 0));
        for (int32 I = JournalBindings.Num() - 1; I >= 0; --I)
            if (!JournalBindings[I] || !JournalBindings[I]->Journal.IsValid())
            {
                if (JournalBindings[I])
                {
                    OnQuestChanged.RemoveDynamic(JournalBindings[I], &UReditusQuestJournalBinding::Changed);
                    OnQuestStateRestored.RemoveDynamic(JournalBindings[I], &UReditusQuestJournalBinding::Restored);
                }
                JournalBindings.RemoveAt(I);
            }
    }
    const auto& Records = Runtime.GetRecords();
    const bool HasActiveTimer = std::any_of(Records.begin(), Records.end(), [](const auto& Pair)
    { return Pair.second.Status == ReditusQuestRuntime::State::Active && Pair.second.TimerRemaining > 0; });
    if (HasActiveTimer && !AreQuestTimersPaused())
    {
        std::set<std::string> PendingDeliveryQuests;
        for (const auto& Delivery : Deliveries) PendingDeliveryQuests.insert(Narrow(Delivery.Key));
        if (Runtime.Tick(World->GetDeltaSeconds(), PendingDeliveryQuests)) FlushChanges();
    }
    if ((!NotificationView.IsValid() || NotificationView->GetWorld() != World) && GetQuestTimers().Num() > 0)
    {
        if (auto* PC = UGameplayStatics::GetPlayerController(GetGameInstance(), 0))
        {
            auto* View = CreateWidget<UReditusQuestNotificationsWidget>(PC);
            if (View) { View->AddToViewport(80); NotificationView = View; }
        }
    }
}

void UReditusQuestSubsystem::Deinitialize()
{
    bSubsystemStopped = true;
    CancelNPCStoryWork(); PendingNPCBattles.Empty();
    SequenceQueue.Empty();
    // Unbind first: teardown must not grant objective progress or start new quests.
    OnQuestSequenceFinished.Clear();
    EndSequence(false);
    if (NotificationView.IsValid()) NotificationView->RemoveFromParent();
    NotificationView.Reset(); JournalBindings.Empty();
    Super::Deinitialize();
}

TArray<FReditusQuestTimerStatus> UReditusQuestSubsystem::GetQuestTimers() const
{
    TArray<FReditusQuestTimerStatus> Result;
    bool TimersPaused = false;
    for (const auto& Pair : Runtime.GetRecords())
    {
        if (Pair.second.Status != ReditusQuestRuntime::State::Active) continue;
        const auto* Handle = QuestRows.Find(Wide(Pair.first));
        const auto* Row = Handle ? Definition(*Handle) : nullptr;
        if (!Row) continue;
        for (const auto& Stage : Row->Stages)
            if (Identifier(Stage.StageID) == Pair.second.StageId && Stage.Timer.Mode != EReditusQuestTimerMode::None
                && Stage.Timer.ShowOnScreen && Pair.second.TimerRemaining > 0)
            {
                FReditusQuestTimerStatus Status;
                Status.Quest = *Handle; Status.StageID = Stage.StageID;
                Status.Label = Stage.Timer.Label.IsEmpty() ? Row->Title : Stage.Timer.Label;
                Status.RemainingSeconds = float(Pair.second.TimerRemaining);
                Status.ElapsedSeconds = FMath::Max(0.f, Stage.Timer.DurationSeconds - Status.RemainingSeconds);
                Status.CountUp = Stage.Timer.CountUp;
                if (Result.Num() == 0) TimersPaused = AreQuestTimersPaused();
                Status.Paused = TimersPaused || Deliveries.Contains(Wide(Pair.first));
                const auto Time = ReditusQuestClock::Display(Status.CountUp ? Status.ElapsedSeconds : Status.RemainingSeconds, Status.CountUp);
                Status.DisplayTime = UTF8_TO_TCHAR(Time.c_str()); Result.Add(Status);
            }
    }
    return Result;
}

void UReditusQuestSubsystem::NotifyQuest(FDataTableRowHandle Quest, EReditusQuestNoticeKind Kind)
{
    const auto* Row = Definition(Quest); if (!Row) return;
    FReditusQuestNotification Notice; Notice.Quest = Quest; Notice.Kind = Kind; Notice.Title = Row->Title;
    const auto* Record = Runtime.FindRecord(Narrow(Key(Quest)));
    if (Record) for (const auto& Stage : Row->Stages)
        if (Identifier(Stage.StageID) == Record->StageId) Notice.Detail = Stage.Description;
    if (Kind == EReditusQuestNoticeKind::Completed && (Row->Rewards.Gold > 0
        || Row->Rewards.Experience > 0 || Row->Rewards.Items.Num() > 0))
        Notice.Detail = GetQuestRewardSummary(Quest);
    OnQuestNotification.Broadcast(Notice);
    if (!ShowQuestNotifications) return;
    auto* PC = UGameplayStatics::GetPlayerController(GetGameInstance(), 0);
    if (!PC) return;
    if (!NotificationView.IsValid() || NotificationView->GetWorld() != PC->GetWorld())
    {
        auto* View = CreateWidget<UReditusQuestNotificationsWidget>(PC);
        if (!View) return;
        View->AddToViewport(80); NotificationView = View;
    }
    NotificationView->Enqueue(Notice);
}


bool UReditusQuestSubsystem::ReportEnemyDefeated(AActor* Enemy, FName EnemyID)
{
    if (!IsValid(Enemy) || EnemyID.IsNone()) return false;
    const TWeakObjectPtr<AActor> ActorKey(Enemy);
    if (DefeatEventIDs.Contains(ActorKey)) return false;
    const FName Event(*FGuid::NewGuid().ToString());
    DefeatEventIDs.Add(ActorKey, Event);
    return ReportQuestEvent(EReditusQuestObjectiveKind::DefeatEnemy, EnemyID, Event, 1);
}
