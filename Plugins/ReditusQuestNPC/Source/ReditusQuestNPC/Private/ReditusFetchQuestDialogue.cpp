#include "ReditusFetchQuestDialogue.h"
#include "ReditusQuestCatalog.h"
#include "ReditusQuestNPCComponent.h"
#include "ReditusQuestSubsystem.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"

namespace
{
    const FReditusQuestDefinition* FetchDefinition(FDataTableRowHandle Quest)
    {
        const auto* Row = ReditusQuestCatalog::Definition(Quest);
        if (!Row || Row->Stages.Num() != 1) return nullptr;
        const auto& Stage = Row->Stages[0];
        if ((Stage.Timer.Mode != EReditusQuestTimerMode::None && Stage.Timer.Mode != EReditusQuestTimerMode::Deadline)
            || Stage.Actions.Num() || Stage.Choices.Num()
            || Stage.Routes.Num() || !Stage.NextStageID.IsNone()) return nullptr;
        bool HasRequiredObjective = false;
        for (const auto& Task : Stage.Objectives)
        {
            if (Task.Kind != EReditusQuestObjectiveKind::CollectItem
                && Task.Kind != EReditusQuestObjectiveKind::DefeatEnemy) return nullptr;
            if (!Task.Optional) HasRequiredObjective = true;
        }
        return HasRequiredObjective ? Row : nullptr;
    }
    bool OwnsFetchItems(UReditusQuestSubsystem* System, APlayerController* Player, const FReditusQuestDefinition& Row)
    {
        for (const auto& Task : Row.Stages[0].Objectives)
        {
            int32 Count = 0;
            if (!Task.Optional && Task.Kind == EReditusQuestObjectiveKind::CollectItem
                && (!System->GetQuestItemCount(Player, Task.ItemRow, Task.ItemKind, Count) || Count < Task.RequiredCount)) return false;
        }
        for (const auto& Cost : Row.TurnInItems)
        {
            int32 Count = 0;
            if (!System->GetQuestItemCount(Player, Cost.ItemRow, Cost.Kind, Count) || Count < Cost.Amount) return false;
        }
        return true;
    }
    bool CanHandInSimpleQuest(UReditusQuestSubsystem* System, APlayerController* Player,
        const FReditusQuestDefinition& Row, EReditusQuestState State)
    {
        if (!OwnsFetchItems(System, Player, Row)) return false;
        if (State == EReditusQuestState::ReadyToDeliver) return true;
        if (State != EReditusQuestState::Available && State != EReditusQuestState::Active) return false;
        // Preserve immediate hand-in for pre-owned fetch items. Defeats must be
        // confirmed by the quest runtime; an empty item requirement proves no kill.
        for (const auto& Task : Row.Stages[0].Objectives)
            if (!Task.Optional && Task.Kind != EReditusQuestObjectiveKind::CollectItem) return false;
        return true;
    }
    bool AllowHandInDecline(const FReditusQuestDefinition& Row)
    {
        // A completed hunt needs only acknowledgement. Keep the option to defer
        // an exchange when it requires the player's current inventory.
        if (Row.TurnInItems.Num() > 0) return true;
        bool HasRequiredDefeat = false;
        for (const auto& Task : Row.Stages[0].Objectives)
        {
            if (Task.Optional) continue;
            if (Task.Kind != EReditusQuestObjectiveKind::DefeatEnemy) return true;
            HasRequiredDefeat = true;
        }
        return !HasRequiredDefeat;
    }
    FText FetchLine(const FReditusFetchQuestText& Text, ReditusFetchQuestFlow::Line Line)
    {
        using L = ReditusFetchQuestFlow::Line;
        switch (Line)
        {
        case L::Offer: return Text.Offer;
        case L::Accept: return Text.AcceptChoice;
        case L::NotNow: return Text.NotNowChoice;
        case L::Reminder: return Text.Reminder;
        case L::HandIn: return Text.HandIn;
        case L::GiveItems: return Text.HandInChoice;
        case L::Declined: return Text.Declined;
        case L::Completed: return Text.Completed;
        case L::Unavailable: return NSLOCTEXT("ReditusFetch", "Unavailable", "I don't need your help just yet.");
        case L::Failed: return NSLOCTEXT("ReditusFetch", "Failed", "We can't finish that task now.");
        case L::Pending: return NSLOCTEXT("ReditusFetch", "Pending", "The exchange is being processed. Please speak to me again shortly.");
        case L::Error: return NSLOCTEXT("ReditusFetch", "ExchangeError", "I couldn't finish the exchange. Nothing has been confirmed; please try again.");
        default: return FText::GetEmpty();
        }
    }
}

ReditusFetchQuestFlow::Phase UReditusQuestNPCComponent::GetFetchQuestPhase(APlayerController* Player) const
{
    using namespace ReditusFetchQuestFlow;
    auto* System = UReditusQuestSubsystem::GetQuestSystem(this);
    const auto Quest = GetNPCQuestRow();
    const auto* Row = FetchDefinition(Quest);
    FReditusQuestSnapshot Snapshot;
    if (!System || !IsValid(Player) || !Row || !System->ReadQuest(Quest, Snapshot)) return Phase::Unavailable;
    bool Pending = false;
    for (const auto& Delivery : System->GetPendingDeliveries()) if (Delivery.Quest == Quest) Pending = true;
    return Select(static_cast<State>(Snapshot.State), CanHandInSimpleQuest(System, Player, *Row, Snapshot.State), Pending, bFetchActionFailed);
}

bool UReditusQuestNPCComponent::ApplyFetchQuestAction(APlayerController* Player, ReditusFetchQuestFlow::Action Action)
{
    const auto* Definition = ResolveDefinition();
    auto* System = UReditusQuestSubsystem::GetQuestSystem(this);
    const auto Quest = GetNPCQuestRow(); const auto* Row = FetchDefinition(Quest);
    if (bEndingPlay || !bTalking || !ActiveFetchDialogue || !Definition || !System || !IsValid(Player)
        || !Player->IsLocalController() || Player != ConversationPlayer.Get() || !Row
        || Definition->DialogueMode != EReditusQuestNPCDialogueMode::FetchQuestText) return false;
    if (Action != ReditusFetchQuestFlow::Action::Accept && Action != ReditusFetchQuestFlow::Action::HandIn) return false;
    bFetchActionFailed = false;
    System->RefreshQuestInventory(Player);
    FReditusQuestSnapshot Snapshot;
    if (!System->ReadQuest(Quest, Snapshot)) { bFetchActionFailed = true; return false; }
    if (Snapshot.State == EReditusQuestState::Completed) return false; // Never pay a completed quest again.
    for (const auto& Delivery : System->GetPendingDeliveries()) if (Delivery.Quest == Quest) return false;
    if (Action == ReditusFetchQuestFlow::Action::HandIn
        && !CanHandInSimpleQuest(System, Player, *Row, Snapshot.State)) return false;
    if (Snapshot.State == EReditusQuestState::Available)
    {
        if (!System->StartQuest(Quest)) { bFetchActionFailed = true; return false; }
        System->ReadQuest(Quest, Snapshot);
    }
    if (Action == ReditusFetchQuestFlow::Action::Accept)
        return Snapshot.State == EReditusQuestState::Active || Snapshot.State == EReditusQuestState::ReadyToDeliver;
    if (Action != ReditusFetchQuestFlow::Action::HandIn || Snapshot.State != EReditusQuestState::ReadyToDeliver)
    { bFetchActionFailed = true; return false; }
    const bool Accepted = System->TurnInFetchQuest(Quest, Player);
    bFetchActionFailed = !Accepted;
    return Accepted;
}

UDialogue* UReditusQuestNPCComponent::BuildFetchQuestDialogue(APlayerController* Player, const FReditusQuestNPCDefinition& Definition)
{
    auto* System = UReditusQuestSubsystem::GetQuestSystem(this);
    const auto* Row = FetchDefinition(Definition.QuestRow);
    FReditusQuestSnapshot Snapshot;
    if (!System || !Row || !System->ReadQuest(Definition.QuestRow, Snapshot))
    {
        UE_LOG(LogTemp, Warning, TEXT("Simple Quest Text needs a valid single-stage quest with Collect Item and/or Defeat Enemy objectives and at least one required objective. Timer Mode may be None or Deadline. Use Dialogue Assets for wait timers, branches, step actions or multi-step quests."));
        return nullptr;
    }
    bFetchActionFailed = false;
    UDialogue* Dialogue = NewObject<UDialogue>(this, NAME_None, RF_Transient);
    if (!Dialogue) return nullptr;
    Dialogue->Name = Definition.FetchQuestText.SpeakerName.IsEmpty()
        ? Definition.DisplayName : Definition.FetchQuestText.SpeakerName;
    for (const auto& Source : ReditusFetchQuestFlow::Template(GetFetchQuestPhase(Player), AllowHandInDecline(*Row)))
    {
        FDialogueNode Node;
        Node.id = Source.ID; Node.isPlayer = Source.Player;
        Node.NodeType = Source.Player ? EDialogueNodeType::DNT_None : Definition.FetchSpeakerType;
        Node.Text = FetchLine(Definition.FetchQuestText, Source.Text);
        Node.SpeakerName = Source.Player ? FString() : Dialogue->Name.ToString();
        Node.bDrawBubbleComment = false; Node.Coordinates = FVector2D((Source.ID % 4) * 320.f, (Source.ID / 4) * 180.f);
        for (int32 Link : Source.Links) Node.Links.Add(Link);
        if (Source.RequiredPhase >= 0)
        {
            auto* Condition = NewObject<UReditusFetchQuestCondition>(Dialogue);
            Condition->ExpectedPhase = Source.RequiredPhase; Node.Conditions.Add(Condition);
        }
        if (Source.OnEnter != ReditusFetchQuestFlow::Action::None)
        {
            auto* Event = NewObject<UReditusFetchQuestActionEvent>(Dialogue);
            Event->FlowAction = int32(Source.OnEnter); Node.Events.Add(Event);
        }
        Dialogue->Data.Add(Node);
    }
    Dialogue->NextNodeId = Dialogue->Data.Num();
    ActiveFetchDialogue = Dialogue;
    return Dialogue;
}

bool UReditusQuestNPCComponent::ReadFetchSpeakerName(const UDialogueUserWidget* Widget, FText& Name) const
{
    if (bEndingPlay || !bTalking || !Widget || Widget != ActiveDialogueWidget || !ActiveFetchDialogue) return false;
    Name = ActiveFetchDialogue->Name; // Keep the authored FText identity through presentation.
    return true;
}

bool UReditusFetchQuestCondition::IsConditionMet_Implementation(APlayerController* Player, AActor* NPCActor)
{
    auto* NPC = IsValid(NPCActor) ? NPCActor->FindComponentByClass<UReditusQuestNPCComponent>() : nullptr;
    return NPC && int32(NPC->GetFetchQuestPhase(Player)) == ExpectedPhase;
}

void UReditusFetchQuestActionEvent::RecieveEventTriggered_Implementation(APlayerController* Player, AActor* NPCActor)
{
    if (auto* NPC = IsValid(NPCActor) ? NPCActor->FindComponentByClass<UReditusQuestNPCComponent>() : nullptr)
        NPC->ApplyFetchQuestAction(Player, static_cast<ReditusFetchQuestFlow::Action>(FlowAction));
}
