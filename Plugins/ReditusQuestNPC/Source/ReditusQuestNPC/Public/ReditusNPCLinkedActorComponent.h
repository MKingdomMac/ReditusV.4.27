#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TimerManager.h"
#include "ReditusNPCLinkedActorComponent.generated.h"

/** Mirrors a story NPC's saved removal on a separate overworld actor. */
UCLASS(ClassGroup=(Reditus), meta=(BlueprintSpawnableComponent, DisplayName="Reditus NPC Linked Actor"))
class REDITUSQUESTNPC_API UReditusNPCLinkedActorComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UReditusNPCLinkedActorComponent();

    // This is a link to the quest NPC, not another independently removable NPC.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPC Removal",
        meta=(ToolTip="Match the talking quest NPC's Persistent NPC ID. This actor follows that NPC's permanent removal. Attach to the overworld EnemyNPC, not the shared Battle actor."))
    FName LinkedNPCID;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    UFUNCTION() void RefreshRemoval();
    UFUNCTION() void HandleNPCRemoved(FName NPCID);
    UFUNCTION() void FinishRemoval();
    FTimerHandle RemovalTimer;
    bool bEndingPlay = false;
    bool bRemovalScheduled = false;
    bool bPreviouslyHidden = false;
    bool bPreviouslyColliding = true;
};
