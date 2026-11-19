#include "ReditusQuestSubsystem.h"
#include "ReditusQuestSequenceGate.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "UObject/UnrealType.h"

bool UReditusQuestSubsystem::AreQuestTimersPaused() const
{
    UWorld* World = GetTickableGameObjectWorld();
    if (!World || World->IsPaused() || PauseQuestTimers) return true;
    if ((SequencePlayer && ActiveSequence.PauseQuestTimers)
        || (QueueWorld.Get() == World && SequenceQueue.Num() > 0 && SequenceQueue[0].PauseQuestTimers))
        return true;

    // Level Blueprints, dialogue events and dynamically created actors all use
    // a Level Sequence player. Include players started outside the quest runner.
    for (TActorIterator<ALevelSequenceActor> It(World); It; ++It)
    {
        const auto* Player = It->GetSequencePlayer();
        // The quest runner already has an authored pause setting for its player.
        if (!IsValid(Player) || Player == SequencePlayer) continue;
        if (Player->IsPlaying()) return true;
        if (!Player->IsPaused()) continue;

        // Dialogue can pause Sequencer mid-scene. Pause at End can also
        // leave a completed scene paused on its final frame indefinitely.
        if (!It->PlaybackSettings.bPauseAtEnd) return true;
        const double Position = Player->GetCurrentTime().Time.AsDecimal();
        const double End = Player->IsReversed() ? Player->GetStartTime().Time.AsDecimal()
                                                : Player->GetEndTime().Time.AsDecimal();
        const double FramesRemaining = Player->IsReversed() ? Position - End : End - Position;
        if (FramesRemaining > 0.001) return true;
    }
    return false;
}

bool UReditusQuestSubsystem::CanSaveQuestState() const
{
    return !bImporting && !bFlushing && !bApplyingNPCAction && PendingNPCBattles.Num() == 0 && !HasNPCStoryWork()
        && Deliveries.Num() == 0 && !SequencePlayer && SequenceQueue.Num() == 0;
}

bool UReditusQuestSubsystem::PlayQuestLevelSequence(FReditusQuestSequence Sequence)
{
    if (bImporting || !GetTickableGameObjectWorld() || (Sequence.Sequence.IsNull() && Sequence.PlacedActorTag.IsNone())) return false;
    if (QueueWorld.Get() != GetTickableGameObjectWorld()) SequenceQueue.Empty();
    QueueWorld = GetTickableGameObjectWorld();
    SequenceQueue.Add(Sequence);
    return true;
}

void UReditusQuestSubsystem::PumpSequences()
{
    UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    if (SequencePlayer)
    {
        if (!IsValid(SequencePlayer) || !IsValid(SequenceActor) || SequenceWorld.Get() != World)
            EndSequence(false);
        return;
    }
    if (!World || !World->IsGameWorld() || SequenceQueue.Num() == 0) return;
    if (QueueWorld.Get() != World) { SequenceQueue.Empty(); return; }
    if (SequenceQueue[0].WaitForExploration && (PendingNPCBattles.Num() > 0 || bApplyingNPCAction || HasNPCDialogueOpen()
        || !ReditusQuestSequenceGate::IsExploring(UGameplayStatics::GetPlayerController(World, 0)))) return;
    for (TActorIterator<ALevelSequenceActor> It(World); It; ++It)
        if (auto* Other = It->GetSequencePlayer())
            if (Other->IsPlaying() || Other->IsPaused()) return; // Respect existing story playback.
    ActiveSequence = SequenceQueue[0]; SequenceQueue.RemoveAt(0);
    SequenceActor = nullptr; bOwnSequenceActor = false;
    if (!ActiveSequence.PlacedActorTag.IsNone())
    {
        int32 Matches = 0;
        for (TActorIterator<ALevelSequenceActor> It(World); It; ++It)
            if (It->ActorHasTag(ActiveSequence.PlacedActorTag)) { SequenceActor = *It; ++Matches; }
        // Never play an arbitrary actor when a tag is missing/ambiguous.
        if (Matches != 1) SequenceActor = nullptr;
        if (SequenceActor)
        {
            SequencePlayer = SequenceActor->GetSequencePlayer();
            if (!SequencePlayer || SequencePlayer->IsPlaying() || SequencePlayer->IsPaused()
                || (!ActiveSequence.Sequence.IsNull() && SequenceActor->GetSequence() != ActiveSequence.Sequence.LoadSynchronous()))
            { SequenceActor = nullptr; SequencePlayer = nullptr; }
        }
    }
    else if (ULevelSequence* Asset = ActiveSequence.Sequence.LoadSynchronous())
    {
        FMovieSceneSequencePlaybackSettings Settings;
        Settings.bAutoPlay = false;
        Settings.bRestoreState = true;
        Settings.bDisableMovementInput = ActiveSequence.DisableMovementAndLook;
        Settings.bDisableLookAtInput = ActiveSequence.DisableMovementAndLook;
        SequencePlayer = ULevelSequencePlayer::CreateLevelSequencePlayer(World, Asset, Settings, SequenceActor);
        bOwnSequenceActor = true;
    }
    if (!SequencePlayer)
    {
        UE_LOG(LogTemp, Warning, TEXT("Quest sequence could not start. Check its asset and unique placed-actor tag in this map."));
        OnQuestSequenceFinished.Broadcast(ActiveSequence, false);
        ActiveSequence = FReditusQuestSequence();
        return;
    }
    SequenceWorld = World;
    // Publish through the actual controller member used by the approved dialogue
    // presentation, without changing that widget or invoking legacy quest logic.
    if (auto* PC = UGameplayStatics::GetPlayerController(World, 0))
        if (auto* Property = FindFProperty<FObjectPropertyBase>(PC->GetClass(), TEXT("Current Sequence ")))
            if (SequenceActor->IsA(Property->PropertyClass))
            {
                PreviousSequenceContext = Property->GetObjectPropertyValue_InContainer(PC);
                SequenceContextController = PC;
                Property->SetObjectPropertyValue_InContainer(PC, SequenceActor);
            }
    SequencePlayer->OnFinished.AddUniqueDynamic(this, &UReditusQuestSubsystem::SequenceFinished);
    SequencePlayer->OnStop.AddUniqueDynamic(this, &UReditusQuestSubsystem::SequenceStopped);
    SequencePlayer->Play();
}

void UReditusQuestSubsystem::SequenceFinished() { EndSequence(true); }
void UReditusQuestSubsystem::SequenceStopped() { EndSequence(false); }

void UReditusQuestSubsystem::EndSequence(bool Completed)
{
    if (bEndingSequence || !SequencePlayer) return;
    TGuardValue<bool> Guard(bEndingSequence, true);
    const auto Finished = ActiveSequence;
    auto* PreviousPlayer = SequencePlayer; auto* PreviousActor = SequenceActor;
    const bool Owned = bOwnSequenceActor;
    SequencePlayer = nullptr; SequenceActor = nullptr; bOwnSequenceActor = false;
    ActiveSequence = FReditusQuestSequence(); SequenceWorld.Reset();
    if (IsValid(PreviousPlayer))
    {
        PreviousPlayer->OnFinished.RemoveDynamic(this, &UReditusQuestSubsystem::SequenceFinished);
        PreviousPlayer->OnStop.RemoveDynamic(this, &UReditusQuestSubsystem::SequenceStopped);
        PreviousPlayer->Stop();
    }
    if (auto* PC = SequenceContextController.Get())
        if (auto* Property = FindFProperty<FObjectPropertyBase>(PC->GetClass(), TEXT("Current Sequence ")))
            if (Property->GetObjectPropertyValue_InContainer(PC) == PreviousActor)
                Property->SetObjectPropertyValue_InContainer(PC, IsValid(PreviousSequenceContext) ? PreviousSequenceContext : nullptr);
    SequenceContextController.Reset(); PreviousSequenceContext = nullptr;
    if (Owned && IsValid(PreviousActor)) PreviousActor->SetLifeSpan(0.01f);
    if (Completed && !Finished.CompletionTargetID.IsNone())
        ReportQuestEvent(EReditusQuestObjectiveKind::Custom, Finished.CompletionTargetID, FName(*FGuid::NewGuid().ToString()), 1);
    OnQuestSequenceFinished.Broadcast(Finished, Completed);
}
