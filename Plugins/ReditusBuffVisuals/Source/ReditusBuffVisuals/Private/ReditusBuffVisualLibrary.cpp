#include "ReditusBuffVisualLibrary.h"
#include "ReditusBuffVisualState.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Modules/ModuleManager.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogReditusBuffVisuals, Log, All);

namespace
{
    UNiagaraComponent* SpawnEffect(const FReditusBuffNiagaraEffect& Spec, USceneComponent* Anchor, bool bOneShot)
    {
        const UWorld* World = IsValid(Anchor) ? Anchor->GetWorld() : nullptr;
        if (!World || !World->IsGameWorld() || World->bIsTearingDown ||
            World->GetNetMode() == NM_DedicatedServer || !IsValid(Spec.Niagara))
        {
            return nullptr;
        }

        // A status may be applied offscreen. Keep it attached for when the unit returns.
        UNiagaraComponent* Result = UNiagaraFunctionLibrary::SpawnSystemAttached(
            Spec.Niagara, Anchor, Spec.Socket, Spec.Offset, Spec.Rotation,
            EAttachLocation::KeepRelativeOffset, bOneShot, false, ENCPoolMethod::None, false);
        if (IsValid(Result))
        {
            Result->SetRelativeScale3D(Spec.Scale);
            Result->Activate(true);
        }
        return IsValid(Result) ? Result : nullptr;
    }

    UReditusBuffVisualState* FindState(AActor* Owner)
    {
        return IsValid(Owner) ? Owner->FindComponentByClass<UReditusBuffVisualState>() : nullptr;
    }
}

UReditusBuffVisualState::UReditusBuffVisualState()
{
    PrimaryComponentTick.bCanEverTick = false;
}

UNiagaraComponent* UReditusBuffVisualState::Start(USceneComponent* InAnchor, UDataTable* Table, FName ID)
{
    if (bTearingDown || !IsValid(InAnchor) || !IsValid(Table) ||
        Table->GetRowStruct() != FReditusBuffVisualRow::StaticStruct() || ID.IsNone())
    {
        return nullptr;
    }
    if (bStarted && Anchor.Get() == InAnchor && ActiveTable.Get() == Table && ActiveID == ID)
    {
        // A refresh must not replay OnApply or stack another emitter.
        return Loop.Get();
    }

    const auto* Row = Table->FindRow<FReditusBuffVisualRow>(ID, TEXT("Reditus buff visuals"), false);
    if (!Row)
    {
        UE_LOG(LogReditusBuffVisuals, Warning, TEXT("Missing visual row '%s' in %s."), *ID.ToString(), *GetNameSafe(Table));
        return nullptr;
    }

    Stop(false);
    ActiveRow = *Row;
    ActiveTable = Table;
    ActiveID = ID;
    Anchor = InAnchor;
    bStarted = true;
    SpawnEffect(ActiveRow.OnApply, InAnchor, true);
    Loop = SpawnEffect(ActiveRow.WhileActive, InAnchor, false);
    return Loop.Get();
}

void UReditusBuffVisualState::PlayTick()
{
    if (bStarted && !bTearingDown)
    {
        SpawnEffect(ActiveRow.OnTick, Anchor.Get(), true);
    }
}

void UReditusBuffVisualState::Stop(bool bPlayEndEffect)
{
    if (!bStarted)
    {
        return;
    }
    bStarted = false;
    UNiagaraComponent* OldLoop = Loop.Get();
    USceneComponent* OldAnchor = Anchor.Get();
    const FReditusBuffNiagaraEffect EndEffect = ActiveRow.OnEnd;
    Loop.Reset();
    Anchor.Reset();
    ActiveTable.Reset();
    ActiveID = NAME_None;
    ActiveRow = FReditusBuffVisualRow();

    if (IsValid(OldLoop))
    {
        OldLoop->DestroyComponent();
    }
    if (bPlayEndEffect && !bTearingDown)
    {
        SpawnEffect(EndEffect, OldAnchor, true);
    }
}

void UReditusBuffVisualState::EndPlay(const EEndPlayReason::Type Reason)
{
    bTearingDown = true;
    Stop(false);
    Super::EndPlay(Reason);
}

void UReditusBuffVisualState::OnComponentDestroyed(bool bDestroyingHierarchy)
{
    bTearingDown = true;
    Stop(false);
    Super::OnComponentDestroyed(bDestroyingHierarchy);
}

UNiagaraComponent* UReditusBuffVisualLibrary::StartBuffVisuals(AActor* BuffOwner,
    USceneComponent* AttachToComponent, UDataTable* VisualTable, FName BuffID)
{
    const UWorld* World = IsValid(BuffOwner) ? BuffOwner->GetWorld() : nullptr;
    if (!World || !World->IsGameWorld() || World->bIsTearingDown ||
        World->GetNetMode() == NM_DedicatedServer || !IsValid(AttachToComponent) ||
        World != AttachToComponent->GetWorld() || !IsValid(VisualTable))
    {
        return nullptr;
    }
    UReditusBuffVisualState* State = FindState(BuffOwner);
    if (!IsValid(State))
    {
        State = NewObject<UReditusBuffVisualState>(BuffOwner, NAME_None, RF_Transient);
        BuffOwner->AddInstanceComponent(State);
        State->RegisterComponent();
    }
    return State->Start(AttachToComponent, VisualTable, BuffID);
}

void UReditusBuffVisualLibrary::PlayBuffTickVisual(AActor* BuffOwner)
{
    if (auto* State = FindState(BuffOwner))
    {
        State->PlayTick();
    }
}

void UReditusBuffVisualLibrary::StopBuffVisuals(AActor* BuffOwner)
{
    if (auto* State = FindState(BuffOwner))
    {
        State->Stop(true);
    }
}

IMPLEMENT_MODULE(FDefaultModuleImpl, ReditusBuffVisuals)
