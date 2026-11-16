#include "ReditusQuestObjectiveComponent.h"
#include "ReditusQuestSubsystem.h"
#include "ReditusQuestNPCPolicy.h"
#include "MapIconComponent.h"
#include "MapQuestMarkerLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "Engine/World.h"

UReditusQuestObjectiveComponent::UReditusQuestObjectiveComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

const FReditusQuestObjectiveDefinition* UReditusQuestObjectiveComponent::ResolveDefinition() const
{
    if (!DefinitionRow.DataTable || DefinitionRow.RowName.IsNone()) return &InlineDefinition;
    if (DefinitionRow.DataTable->GetRowStruct() != FReditusQuestObjectiveDefinition::StaticStruct()) return nullptr;
    return DefinitionRow.DataTable->FindRow<FReditusQuestObjectiveDefinition>(DefinitionRow.RowName, TEXT("Quest Objective"), false);
}

void UReditusQuestObjectiveComponent::BeginPlay()
{
    Super::BeginPlay();
    FindMapIcon();
    if (auto* System = UReditusQuestSubsystem::GetQuestSystem(this))
    {
        System->OnQuestChanged.AddUniqueDynamic(this, &UReditusQuestObjectiveComponent::HandleQuestChanged);
        System->OnQuestStateRestored.AddUniqueDynamic(this, &UReditusQuestObjectiveComponent::HandleStateRestored);
    }
    if (MapIcon.IsValid()) MapIcon->SetIconVisible(false);
    GetWorld()->GetTimerManager().SetTimer(RefreshTimer, this, &UReditusQuestObjectiveComponent::RefreshObjective, .5f, true);
    RefreshObjective();
}

void UReditusQuestObjectiveComponent::FindMapIcon()
{
    auto* Owner = GetOwner();
    if (!IsValid(Owner)) return;
    MapIcon = Owner->FindComponentByClass<UMapIconComponent>();
    if (!MapIcon.IsValid() && HasBegunPlay() && Owner->GetRootComponent())
    {
        auto* Created = NewObject<UMapIconComponent>(Owner);
        Created->SetIconVisible(false); Created->SetupAttachment(Owner->GetRootComponent());
        Owner->AddInstanceComponent(Created); Created->RegisterComponent(); MapIcon = Created;
    }
}

void UReditusQuestObjectiveComponent::RefreshObjective()
{
    if (bEndingPlay) return;
    FindMapIcon();
    if (!MapIcon.IsValid()) return;
    const auto* Definition = ResolveDefinition();
    FReditusQuestSnapshot Snapshot;
    FReditusQuestDefinition QuestDefinition;
    TArray<FReditusQuestObjectiveStatus> Objectives;
    auto* System = UReditusQuestSubsystem::GetQuestSystem(this);
    const bool Read = Definition && Definition->QuestRow.DataTable && !Definition->QuestRow.RowName.IsNone()
        && System && System->GetQuestDetails(Definition->QuestRow, QuestDefinition, Snapshot, Objectives);
    using namespace ReditusQuestNPCPolicy;
    bool Visible = Read && ObjectiveVisible(static_cast<State>(Snapshot.State), Snapshot.Progress,
        Definition->MinimumProgress, Definition->MaximumProgressExclusive)
        && QuestDefinition.Markers.ShowOnMinimap && QuestDefinition.Markers.ShowObjectiveDestinations
        && (Definition->StageID.IsNone() || Definition->StageID == Snapshot.StageID);
    if (Visible && !Definition->ObjectiveID.IsNone())
    {
        Visible = false;
        for (const auto& Objective : Objectives)
            if (Objective.CurrentStage && Objective.Objective.ObjectiveID == Definition->ObjectiveID && !Objective.Complete)
                Visible = true;
    }
    UMapQuestMarkerLibrary::ApplyQuestMapIcon(MapIcon.Get(), Visible, Snapshot.IsMainQuest,
        Definition && !Definition->Label.IsEmpty() ? Definition->Label : QuestDefinition.Title);
}

void UReditusQuestObjectiveComponent::HandleQuestChanged(FDataTableRowHandle Quest)
{ (void)Quest; RefreshObjective(); }
void UReditusQuestObjectiveComponent::HandleStateRestored()
{ RefreshObjective(); }

bool UReditusQuestObjectiveComponent::ReportObjectiveReached(FName UniqueEventID)
{
    const auto* Definition = ResolveDefinition();
    auto* System = UReditusQuestSubsystem::GetQuestSystem(this);
    FReditusQuestDefinition QuestDefinition;
    FReditusQuestSnapshot Snapshot;
    TArray<FReditusQuestObjectiveStatus> Objectives;
    if (!Definition || !System || UniqueEventID.IsNone()
        || !System->GetQuestDetails(Definition->QuestRow, QuestDefinition, Snapshot, Objectives)
        || Snapshot.State != EReditusQuestState::Active
        || (!Definition->StageID.IsNone() && Definition->StageID != Snapshot.StageID)) return false;
    for (const auto& Objective : Objectives)
        if (Objective.CurrentStage && Objective.Objective.ObjectiveID == Definition->ObjectiveID && !Objective.Complete)
            return System->ReportQuestEvent(Objective.Objective.Kind, Objective.Objective.TargetID, UniqueEventID);
    return false;
}

void UReditusQuestObjectiveComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    bEndingPlay = true;
    if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(RefreshTimer);
    if (auto* System = UReditusQuestSubsystem::GetQuestSystem(this))
    {
        System->OnQuestChanged.RemoveDynamic(this, &UReditusQuestObjectiveComponent::HandleQuestChanged);
        System->OnQuestStateRestored.RemoveDynamic(this, &UReditusQuestObjectiveComponent::HandleStateRestored);
    }
    Super::EndPlay(Reason);
}
