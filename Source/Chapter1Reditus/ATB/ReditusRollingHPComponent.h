#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ReditusRollingHPCore.h"
#include "ReditusRollingHPComponent.generated.h"

class UReditusATBComponent;
class AActor;

USTRUCT(BlueprintType)
struct CHAPTER1REDITUS_API FReditusRollingHPTuning
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Rolling HP") bool bEnabled=true;
    /** A full bar drains in this many battle seconds; smaller hits drain proportionally faster. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Rolling HP",meta=(ClampMin="0.01")) float DamageFullBarSeconds=3.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Rolling HP",meta=(ClampMin="0.01")) float HealingFullBarSeconds=0.6f;
    /** A newly lethal hit gets at least this rescue window. More hits do not reset it. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Rolling HP",meta=(ClampMin="0.0")) float MinimumLethalDrainSeconds=1.f;
};

/** Attached to player battle actors by the ATB component. Enemy HP stays immediate. */
UCLASS(ClassGroup=(Reditus),meta=(BlueprintSpawnableComponent))
class CHAPTER1REDITUS_API UReditusRollingHPComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UReditusRollingHPComponent();
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
    void InitializeBattle(UReditusATBComponent* InATB);
    void FinishBattle();
    int32 CalculationHP() const;
    int32 ResolveWrite(int32 RequestedHP);
    UFUNCTION(BlueprintPure,Category="Reditus|Rolling HP") int32 GetTargetHP() const;
    UFUNCTION(BlueprintPure,Category="Reditus|Rolling HP") bool IsLethalDamagePending() const;
    bool CanReceiveHealing() const;
    /** False means the BP_UnitBase SetHP bridge has not been connected for this actor yet. */
    UFUNCTION(BlueprintPure,Category="Reditus|Rolling HP") bool HasReceivedLegacyHPWrite() const { return bBridgeSeen; }
private:
    UPROPERTY(Transient) TWeakObjectPtr<UReditusATBComponent> ATB;
    ReditusRollingHP::Meter Meter;
    int32 LastPublishedHP=0;
    int32 PublishingHP=0;
    bool bPublishing=false;
    bool bBridgeSeen=false;
    bool bBattleAttached=false;
    bool Enabled() const;
    ReditusRollingHP::Settings Settings() const;
    bool Publish(int32 HP);
};

/** Two small bridges in the existing BP_UnitBase HP graph; no combat formulas move here. */
UCLASS()
class CHAPTER1REDITUS_API UReditusRollingHPLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Replace only arithmetic currentHP getters in TakeDamage, Heal, poison, Regen and SetHP's OD calculation. */
    UFUNCTION(BlueprintPure,Category="Reditus|Rolling HP") static int32 GetHPForCalculation(AActor* Unit);
    /** In SetHP, place after the OD calculation and before assigning currentHP. */
    UFUNCTION(BlueprintCallable,Category="Reditus|Rolling HP") static int32 ResolveHPWrite(AActor* Unit,int32 RequestedHP);
    /** Guard the existing Heal event; ordinary healing cannot resurrect a counter that reached 0. */
    UFUNCTION(BlueprintPure,Category="Reditus|Rolling HP") static bool CanReceiveBattleHealing(AActor* Unit);
};
