#pragma once

#include "Components/ActorComponent.h"
#include "ReditusBuffVisualLibrary.h"
#include "ReditusBuffVisualState.generated.h"

/** One visual lifetime per buff actor. Deliberately has no Tick. */
UCLASS(Transient, NotBlueprintable)
class UReditusBuffVisualState : public UActorComponent
{
    GENERATED_BODY()

public:
    UReditusBuffVisualState();
    UNiagaraComponent* Start(USceneComponent* InAnchor, UDataTable* Table, FName ID);
    void PlayTick();
    void Stop(bool bPlayEndEffect);

protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

private:
    UPROPERTY(Transient)
    FReditusBuffVisualRow ActiveRow;

    UPROPERTY(Transient)
    TWeakObjectPtr<USceneComponent> Anchor;

    UPROPERTY(Transient)
    TWeakObjectPtr<UDataTable> ActiveTable;

    UPROPERTY(Transient)
    TWeakObjectPtr<UNiagaraComponent> Loop;

    FName ActiveID = NAME_None;
    bool bStarted = false;
    bool bTearingDown = false;
};
