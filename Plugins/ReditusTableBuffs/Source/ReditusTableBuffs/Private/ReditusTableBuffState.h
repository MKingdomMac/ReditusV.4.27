#pragma once

#include "Components/ActorComponent.h"
#include "ReditusTableBuffLibrary.h"
#include "ReditusTableBuffRules.h"
#include "ReditusTableBuffState.generated.h"

class USceneComponent;

UCLASS(Transient, Blueprintable, BlueprintType)
class REDITUSTABLEBUFFS_API UReditusTableBuffState : public UActorComponent
{
    GENERATED_BODY()

public:
    UReditusTableBuffState();

    bool Apply(AActor* InBattle, UActorComponent* Clock, UDataTable* Table, FName ID, const FReditusTableBuffRow& Row,
        FName Slot, int32 SkipEndToken, bool bLegacySelfCast);
    FName GetEffectSlot() const { return EffectSlot; }
    double DefenseDelta() const;
    uint64 GetAggroFocusOrder(const AActor* InBattle) const;

protected:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

private:
    UFUNCTION()
    void OnOwnerCycleEnd(AActor* Unit, int32 CycleToken);

    bool IsLive() const;
    void Clear(bool bNormalExpiry, bool bKeepTiming = false);

    UPROPERTY(Transient)
    FReditusTableBuffRow ActiveRow;

    TWeakObjectPtr<AActor> Battle;
    TWeakObjectPtr<UActorComponent> BattleClock;
    TWeakObjectPtr<UDataTable> ActiveTable;
    TWeakObjectPtr<USceneComponent> Loop;
    FName ActiveID = NAME_None;
    uint64 AggroFocusOrder = 0;
    FName EffectSlot = NAME_None;
    ReditusTableBuffRules::FGuardUpLifetime Lifetime;
};

/** Attached only to a legacy buff actor whose row explicitly sets Owner Cycles. */
UCLASS(Transient, NotBlueprintable)
class UReditusLegacyTableBuffTimer : public UActorComponent
{
    GENERATED_BODY()

public:
    bool Arm(AActor* Unit, UActorComponent* InClock, int32 Cycles, int32 OpenToken);
    void Disarm();

protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    UFUNCTION()
    void OnRecipientCycleEnd(AActor* Unit, int32 Token);

    TWeakObjectPtr<AActor> Recipient;
    TWeakObjectPtr<UActorComponent> Clock;
    ReditusTableBuffRules::FGuardUpLifetime Lifetime;
};
