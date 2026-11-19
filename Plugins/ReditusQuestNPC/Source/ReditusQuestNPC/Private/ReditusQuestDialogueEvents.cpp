#include "ReditusQuestDialogueEvents.h"
#include "ReditusQuestSubsystem.h"
#include "ReditusQuestNPCComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"

namespace
{
    FDataTableRowHandle ResolveQuest(AActor* NPC, FDataTableRowHandle Override)
    {
        if (Override.DataTable) return Override;
        auto* Component = IsValid(NPC) ? NPC->FindComponentByClass<UReditusQuestNPCComponent>() : nullptr;
        return Component ? Component->GetNPCQuestRow() : FDataTableRowHandle();
    }
}

void UReditusQuestActionEvent::RecieveEventTriggered_Implementation(APlayerController* ConsideringPlayer, AActor* NPCActor)
{
    auto* System = UReditusQuestSubsystem::GetQuestSystem(ConsideringPlayer);
    if (!System) return;
    const FDataTableRowHandle Quest = ResolveQuest(NPCActor, QuestRowOverride);
    if (Action == EReditusQuestAction::StartQuest)
    {
        if (!System->StartQuest(Quest))
            UE_LOG(LogTemp, Warning, TEXT("Start Quest did not start %s / %s. Check the NPC's Quest row or this event's Quest Row Override, prerequisites and whether it is already started."), *GetNameSafe(Quest.DataTable), *Quest.RowName.ToString());
    }
    else if (Action == EReditusQuestAction::TurnInQuest)
    {
        if (!System->TurnInDialogueQuest(Quest, ConsideringPlayer))
            UE_LOG(LogTemp, Warning, TEXT("Turn In Quest did not finish %s / %s. Check Ready to Deliver state, current item ownership, pending receipts and the central reward handler."), *GetNameSafe(Quest.DataTable), *Quest.RowName.ToString());
    }
    else if (Action == EReditusQuestAction::RetryFailedQuest) System->RetryFailedQuest(Quest);
    else if (Action == EReditusQuestAction::RecordChoice)
    {
        if (!System->RecordQuestChoice(Quest, ChoiceID, OptionID))
            UE_LOG(LogTemp, Warning, TEXT("Quest choice rejected: check the current stage and authored choice/option IDs."));
    }
    else if (Action == EReditusQuestAction::RequestStepAction)
    {
        if (!System->RequestQuestStepAction(Quest, StepActionID, ConsideringPlayer))
            UE_LOG(LogTemp, Warning, TEXT("Quest step rejected: check the stage, inventory, receipt and central delivery handler."));
    }
    else if (Action == EReditusQuestAction::ReportObjectiveEvent)
        System->ReportQuestEvent(ObjectiveKind, TargetID, UniqueEventID, Progress);
    // The retired integer AdvanceQuest operation never advances the new system.
}

bool UReditusQuestStateCondition::IsConditionMet_Implementation(APlayerController* ConsideringPlayer, AActor* NPCActor)
{
    FReditusQuestSnapshot Snapshot;
    auto* System = UReditusQuestSubsystem::GetQuestSystem(ConsideringPlayer);
    return System && System->ReadQuest(ResolveQuest(NPCActor, QuestRowOverride), Snapshot)
        && Snapshot.State == RequiredState && (RequiredStageID.IsNone() || Snapshot.StageID == RequiredStageID);
}

void UReditusNPCActionEvent::RecieveEventTriggered_Implementation(APlayerController* ConsideringPlayer, AActor* NPCActor)
{
    auto* NPC = IsValid(NPCActor) ? NPCActor->FindComponentByClass<UReditusQuestNPCComponent>() : nullptr;
    if (!NPC)
    { UE_LOG(LogTemp, Warning, TEXT("NPC dialogue action needs a QuestNPC component on its NPCActor.")); return; }
    bool Applied = false;
    if (Action == EReditusNPCDialogueAction::StartBattle) Applied = NPC->StartNPCBattle(ConsideringPlayer, EncounterIDOverride);
    else if (Action == EReditusNPCDialogueAction::Recruit) Applied = NPC->RecruitNPCToParty(ConsideringPlayer);
    else if (Action == EReditusNPCDialogueAction::RemovePermanently) Applied = NPC->RemoveNPCPermanently(RemovalReason);
    else if (Action == EReditusNPCDialogueAction::FinishFollowUp) Applied = NPC->CompletePostBattleDialogue();
    if (!Applied) UE_LOG(LogTemp, Warning, TEXT("NPC action failed for %s. Check Persistent NPC ID, recruitment row/ownership or battle-handler binding."), *NPCActor->GetName());
}

bool UReditusQuestChoiceCondition::IsConditionMet_Implementation(APlayerController* ConsideringPlayer, AActor* NPCActor)
{
    auto* System = UReditusQuestSubsystem::GetQuestSystem(ConsideringPlayer);
    if (!System || ChoiceID.IsNone()) return false;
    const auto Quest = ResolveQuest(NPCActor, QuestRowOverride);
    FReditusQuestSnapshot Snapshot;
    if (!System->ReadQuest(Quest, Snapshot) || Snapshot.State == EReditusQuestState::Unknown) return false;
    FName Selected; int32 Value = 0;
    const bool Chosen = System->GetQuestChoice(Quest, ChoiceID, Selected, Value);
    return MustBeUnselected ? !Chosen : Chosen && (OptionID.IsNone() || OptionID == Selected);
}
bool UReditusQuestStepAppliedCondition::IsConditionMet_Implementation(APlayerController* ConsideringPlayer, AActor* NPCActor)
{
    auto* System = UReditusQuestSubsystem::GetQuestSystem(ConsideringPlayer);
    FReditusQuestSnapshot Snapshot;
    const auto Quest = ResolveQuest(NPCActor, QuestRowOverride);
    return System && !ActionID.IsNone() && System->ReadQuest(Quest, Snapshot)
        && System->WasQuestStepActionApplied(Quest, ActionID) == ExpectedApplied;
}
