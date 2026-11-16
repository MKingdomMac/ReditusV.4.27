#include "ReditusNPCLinkedActorComponent.h"
#include "ReditusQuestSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UReditusNPCLinkedActorComponent::UReditusNPCLinkedActorComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UReditusNPCLinkedActorComponent::BeginPlay()
{
    Super::BeginPlay();
    if (auto* System = UReditusQuestSubsystem::GetQuestSystem(this))
    {
        System->OnNPCRemoved.AddUniqueDynamic(this, &UReditusNPCLinkedActorComponent::HandleNPCRemoved);
        System->OnQuestStateRestored.AddUniqueDynamic(this, &UReditusNPCLinkedActorComponent::RefreshRemoval);
    }
    RefreshRemoval();
}

void UReditusNPCLinkedActorComponent::HandleNPCRemoved(FName NPCID)
{
    if (!LinkedNPCID.IsNone() && LinkedNPCID == NPCID) RefreshRemoval();
}

void UReditusNPCLinkedActorComponent::RefreshRemoval()
{
    AActor* Owner = GetOwner();
    auto* System = UReditusQuestSubsystem::GetQuestSystem(this);
    if (bEndingPlay || !IsValid(Owner) || !System || !GetWorld()) return;
    if (!LinkedNPCID.IsNone() && System->WasNPCRemoved(LinkedNPCID))
    {
        if (System->IsNPCConversationActive(LinkedNPCID))
        {
            // A hold can begin after destruction was queued, in the same result dispatcher.
            if (bRemovalScheduled)
            {
                GetWorld()->GetTimerManager().ClearTimer(RemovalTimer);
                bRemovalScheduled = false;
                Owner->SetActorHiddenInGame(bPreviouslyHidden);
                Owner->SetActorEnableCollision(bPreviouslyColliding);
            }
            GetWorld()->GetTimerManager().SetTimer(RemovalTimer, this, &UReditusNPCLinkedActorComponent::RefreshRemoval, .1f, false);
            return;
        }
        if (bRemovalScheduled) return;
        bPreviouslyHidden = Owner->IsHidden();
        bPreviouslyColliding = Owner->GetActorEnableCollision();
        Owner->SetActorHiddenInGame(true);
        Owner->SetActorEnableCollision(false);
        bRemovalScheduled = true;
        // Finish after the current dispatcher/event array has returned.
        RemovalTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(this, &UReditusNPCLinkedActorComponent::FinishRemoval);
    }
    else if (bRemovalScheduled)
    {
        GetWorld()->GetTimerManager().ClearTimer(RemovalTimer);
        bRemovalScheduled = false;
        Owner->SetActorHiddenInGame(bPreviouslyHidden);
        Owner->SetActorEnableCollision(bPreviouslyColliding);
    }
    else GetWorld()->GetTimerManager().ClearTimer(RemovalTimer);
}

void UReditusNPCLinkedActorComponent::FinishRemoval()
{
    if (bEndingPlay || !IsValid(GetOwner())) return;
    auto* System = UReditusQuestSubsystem::GetQuestSystem(this);
    if (System && System->IsNPCConversationActive(LinkedNPCID)) { RefreshRemoval(); return; }
    if (System && !LinkedNPCID.IsNone() && System->WasNPCRemoved(LinkedNPCID)) GetOwner()->Destroy();
    else RefreshRemoval();
}

void UReditusNPCLinkedActorComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    bEndingPlay = true;
    if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(RemovalTimer);
    if (auto* System = UReditusQuestSubsystem::GetQuestSystem(this))
    {
        System->OnNPCRemoved.RemoveDynamic(this, &UReditusNPCLinkedActorComponent::HandleNPCRemoved);
        System->OnQuestStateRestored.RemoveDynamic(this, &UReditusNPCLinkedActorComponent::RefreshRemoval);
    }
    Super::EndPlay(Reason);
}
