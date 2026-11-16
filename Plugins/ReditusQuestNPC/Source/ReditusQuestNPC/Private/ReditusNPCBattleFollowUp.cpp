#include "ReditusQuestNPCComponent.h"
#include "ReditusQuestSubsystem.h"
#include "ReditusQuestSequenceGate.h"
#include "DialogueUserWidget.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "UObject/UnrealType.h"

bool UReditusQuestNPCComponent::BeginBattleFollowUp(const FReditusNPCBattleRequest& Request, EReditusNPCBattleResult Result)
{
    const auto* Definition = ResolveDefinition();
    if (bEndingPlay || !Definition || HasPendingBattleFollowUp() || Request.NPCID != PersistentNPCID) return false;
    FReditusNPCBattleFollowUp Settings;
    if (Result == EReditusNPCBattleResult::Won) Settings = Definition->PostBattleFollowUp;
    int32 Matches = 0;
    for (const auto& Rule : Definition->EncounterFollowUps)
        if (Rule.EncounterID == Request.EncounterID && Rule.Result == Result) { Settings = Rule.FollowUp; ++Matches; }
    if (Matches <= 1 && !Settings.HasWork()) return false;
    ActiveFollowUp = Settings; FollowUpRequest = Request; FollowUpResult = Result;
    FollowUpQuest = LastBattleRequestID == Request.RequestID ? LastBattleQuest : Definition->QuestRow;
    FollowUpState.Begin(!Settings.Sequence.IsNull() || !Settings.PlacedSequenceActorTag.IsNone(), Settings.Dialogue != nullptr);
    HoldEncounterSource(Request.EncounterID);
    if (Matches > 1)
    {
        // An ambiguous rule is an authoring error, never permission to remove an NPC.
        UE_LOG(LogTemp, Error, TEXT("NPC %s has duplicate follow-up rules for encounter %s/result %d. Chain stopped."),
            *PersistentNPCID.ToString(), *Request.EncounterID.ToString(), int32(Result));
        FollowUpState.Cancel();
    }
    return true; // This component now owns the deferred removal decision.
}

bool UReditusQuestNPCComponent::CompletePostBattleDialogue()
{
    return bPostBattleDialogue && bTalking && FollowUpState.ApproveDialogue();
}

void UReditusQuestNPCComponent::HandleFollowUpSequenceFinished(FReditusQuestSequence Sequence, bool Completed)
{
    if (FollowUpState.Busy() && Sequence.NPCFollowUpRequestID.IsValid()
        && Sequence.NPCFollowUpRequestID == FollowUpRequest.RequestID)
        FollowUpState.SequenceEnded(Completed);
}

void UReditusQuestNPCComponent::PumpBattleFollowUp()
{
    if (!FollowUpState.Busy() || bFinishingFollowUp) return;
    auto* System = UReditusQuestSubsystem::GetQuestSystem(this);
    auto* Player = FollowUpRequest.Player;
    if (bEndingPlay || !System || !IsValid(Player) || Player->GetWorld() != GetWorld()
        || FollowUpRequest.RequestWorld.Get() != GetWorld())
    { FinishBattleFollowUp(false, !bEndingPlay); return; }
    bool Ready = System->CanPresentNPCFollowUp() && ReditusQuestSequenceGate::IsExploring(Player) && !System->HasNPCDialogueOpen()
        && !System->HasPendingNPCBattle(PersistentNPCID);
    // A sequence from a quest stage or a custom listener must finish before we open dialogue.
    if (System->HasQueuedQuestSequences()) Ready = false;
    if (Ready)
        for (TActorIterator<ALevelSequenceActor> It(GetWorld()); It; ++It)
            if (auto* Other = It->GetSequencePlayer())
                if (Other->IsPlaying() || Other->IsPaused()) { Ready = false; break; }
    using namespace ReditusNPCFollowUpPolicy;
    switch (FollowUpState.Next(Ready))
    {
    case Action::StartSequence:
    {
        FReditusQuestSequence Scene;
        Scene.Sequence = ActiveFollowUp.Sequence; Scene.PlacedActorTag = ActiveFollowUp.PlacedSequenceActorTag;
        Scene.NPCFollowUpRequestID = FollowUpRequest.RequestID;
        if (!System->PlayQuestLevelSequence(Scene)) FollowUpState.Cancel();
        break;
    }
    case Action::StartDialogue:
        System->RefreshQuestInventory(Player);
        if (!OpenDialogue(Player, ActiveFollowUp.Dialogue, FollowUpQuest, true)) FollowUpState.Cancel();
        break;
    case Action::Finish: FinishBattleFollowUp(true); break;
    case Action::Abort:
        UE_LOG(LogTemp, Warning, TEXT("Follow-up stopped for NPC %s / encounter %s. Check sequence completion and the post-dialogue Finish Follow Up event. No automatic next battle/removal was applied."),
            *PersistentNPCID.ToString(), *FollowUpRequest.EncounterID.ToString());
        FinishBattleFollowUp(false); break;
    default: break;
    }
}

void UReditusQuestNPCComponent::FinishBattleFollowUp(bool Completed, bool Notify)
{
    if (bFinishingFollowUp) return;
    TGuardValue<bool> Guard(bFinishingFollowUp, true);
    const bool HadFollowUp = FollowUpState.Busy();
    const auto Request = FollowUpRequest;
    const bool RemoveOnFinalVictory = Completed && FollowUpResult == EReditusNPCBattleResult::Won && Request.RemoveNPCOnVictory;
    auto* System = UReditusQuestSubsystem::GetQuestSystem(this);
    // Preserve an explicitly chosen next battle; otherwise use the row's automatic continuation.
    const FName Next = Completed ? (!PendingEncounterID.IsNone() ? PendingEncounterID : ActiveFollowUp.NextEncounterID) : NAME_None;
    auto* NextPlayer = PendingBattlePlayer.IsValid() ? PendingBattlePlayer.Get() : Request.Player;
    const auto NextQuest = !PendingEncounterID.IsNone() ? PendingBattleQuest : FollowUpQuest;
    FollowUpState.Reset(); ActiveFollowUp = FReditusNPCBattleFollowUp(); FollowUpRequest = FReditusNPCBattleRequest();
    FollowUpQuest = FDataTableRowHandle();
    if (System && Request.RequestID.IsValid()) System->CancelNPCFollowUpSequences(Request.RequestID);
    ReleaseEncounterSource();
    if (!Completed)
    {
        if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(BattleSubmitTimer);
        PendingEncounterID = NAME_None; PendingBattlePlayer.Reset(); PendingBattleQuest = FDataTableRowHandle();
        bRemoveAfterPendingBattle = false;
        if (bPostBattleDialogue)
        {
            UDialogueUserWidget* Widget = ActiveDialogueWidget;
            FinishConversation(false);
            if (IsValid(Widget)) Widget->RemoveFromParent();
        }
    }
    else if (!Next.IsNone() && System && !System->WasNPCRemoved(PersistentNPCID))
    {
        // Queue rather than recurse through battle delegates. Every encounter gets a fresh GUID.
        PendingEncounterID = Next; PendingBattlePlayer = NextPlayer; PendingBattleQuest = NextQuest;
        bRemoveAfterPendingBattle = Request.RemoveNPCOnVictory;
        if (GetWorld()) BattleSubmitTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(this, &UReditusQuestNPCComponent::SubmitPendingNPCBattle);
    }
    else
    {
        PendingEncounterID = NAME_None; PendingBattlePlayer.Reset(); PendingBattleQuest = FDataTableRowHandle();
        bRemoveAfterPendingBattle = false;
        if (RemoveOnFinalVictory && System) System->RemoveNPCPermanently(PersistentNPCID, EReditusNPCRemovalReason::Defeated);
    }
    if (HadFollowUp && Notify && System)
        System->OnNPCFollowUpFinished.Broadcast(Request.NPCID, Request.EncounterID, Completed);
    if (!bEndingPlay) ApplyPersistentRemoval();
}

void UReditusQuestNPCComponent::HoldEncounterSource(FName EncounterID)
{
    ReleaseEncounterSource();
    AActor* Source = nullptr; int32 Matches = 0;
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
        if (It->ActorHasTag(EncounterID) && FindFProperty<FBoolProperty>(It->GetClass(), TEXT("isStartingBattle")))
        { Source = *It; ++Matches; }
    if (Matches != 1) return;
    if (auto* Field = FindFProperty<FBoolProperty>(Source->GetClass(), TEXT("isStartingBattle")))
    {
        HeldEncounterSource = Source; bSourcePreviouslyStarting = Field->GetPropertyValue_InContainer(Source);
        Field->SetPropertyValue_InContainer(Source, true);
    }
}

void UReditusQuestNPCComponent::ReleaseEncounterSource()
{
    if (auto* Source = HeldEncounterSource.Get())
        if (auto* Field = FindFProperty<FBoolProperty>(Source->GetClass(), TEXT("isStartingBattle")))
            Field->SetPropertyValue_InContainer(Source, bSourcePreviouslyStarting);
    HeldEncounterSource.Reset(); bSourcePreviouslyStarting = false;
}

UReditusQuestNPCComponent* UReditusQuestSubsystem::FindNPCComponent(FName NPCID) const
{
    if (NPCID.IsNone() || !GetTickableGameObjectWorld()) return nullptr;
    UReditusQuestNPCComponent* Found = nullptr;
    for (TActorIterator<AActor> It(GetTickableGameObjectWorld()); It; ++It)
    {
        TArray<UReditusQuestNPCComponent*> Components; It->GetComponents<UReditusQuestNPCComponent>(Components);
        for (auto* NPC : Components)
            if (IsValid(NPC) && NPC->PersistentNPCID == NPCID)
            { if (Found) return nullptr; Found = NPC; }
    }
    return Found;
}

bool UReditusQuestSubsystem::HasNPCDialogueOpen() const
{
    if (!GetTickableGameObjectWorld()) return false;
    for (TActorIterator<AActor> It(GetTickableGameObjectWorld()); It; ++It)
    {
        TArray<UReditusQuestNPCComponent*> Components; It->GetComponents<UReditusQuestNPCComponent>(Components);
        for (auto* NPC : Components) if (IsValid(NPC) && NPC->IsTalking()) return true;
    }
    return false;
}

bool UReditusQuestSubsystem::HasNPCFollowUps() const
{
    if (!GetTickableGameObjectWorld()) return false;
    for (TActorIterator<AActor> It(GetTickableGameObjectWorld()); It; ++It)
    {
        TArray<UReditusQuestNPCComponent*> Components; It->GetComponents<UReditusQuestNPCComponent>(Components);
        for (auto* NPC : Components) if (IsValid(NPC) && NPC->HasPendingBattleFollowUp()) return true;
    }
    return false;
}

bool UReditusQuestSubsystem::HasNPCStoryWork() const
{
    if (!GetTickableGameObjectWorld()) return false;
    for (TActorIterator<AActor> It(GetTickableGameObjectWorld()); It; ++It)
    {
        TArray<UReditusQuestNPCComponent*> Components; It->GetComponents<UReditusQuestNPCComponent>(Components);
        for (auto* NPC : Components)
            if (IsValid(NPC) && (NPC->HasPendingBattleFollowUp() || !NPC->PendingEncounterID.IsNone())) return true;
    }
    return false;
}

void UReditusQuestSubsystem::CancelNPCFollowUpSequences(FGuid RequestID)
{
    if (!RequestID.IsValid()) return;
    SequenceQueue.RemoveAll([&](const FReditusQuestSequence& Scene) { return Scene.NPCFollowUpRequestID == RequestID; });
    if (ActiveSequence.NPCFollowUpRequestID == RequestID) EndSequence(false);
}

void UReditusQuestSubsystem::CancelNPCStoryWork()
{
    if (!GetTickableGameObjectWorld()) return;
    for (TActorIterator<AActor> It(GetTickableGameObjectWorld()); It; ++It)
    {
        TArray<UReditusQuestNPCComponent*> Components; It->GetComponents<UReditusQuestNPCComponent>(Components);
        for (auto* NPC : Components) if (IsValid(NPC)) NPC->FinishBattleFollowUp(false, false);
    }
}
