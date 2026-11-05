#include "ReditusRollingHPComponent.h"
#include "ReditusATBComponent.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"

namespace
{
    int32 HPValue(const AActor* Unit,FName Name,int32 Fallback=0)
    {
        const auto* Field=IsValid(Unit) ? FindFProperty<FIntProperty>(Unit->GetClass(),Name) : nullptr;
        return Field ? Field->GetPropertyValue_InContainer(Unit) : Fallback;
    }
    UReditusRollingHPComponent* Rolling(AActor* Unit)
    { return IsValid(Unit) ? Unit->FindComponentByClass<UReditusRollingHPComponent>() : nullptr; }
}

UReditusRollingHPComponent::UReditusRollingHPComponent()
{ PrimaryComponentTick.bCanEverTick=true; }

bool UReditusRollingHPComponent::Enabled() const
{ return bBattleAttached && ATB.IsValid() && ATB->IsATBRunning() && ATB->RollingHP.bEnabled; }

ReditusRollingHP::Settings UReditusRollingHPComponent::Settings() const
{
    ReditusRollingHP::Settings Result;
    if (ATB.IsValid())
    {
        Result.DamageFullBarSeconds=ATB->RollingHP.DamageFullBarSeconds;
        Result.HealingFullBarSeconds=ATB->RollingHP.HealingFullBarSeconds;
        Result.MinimumLethalDrainSeconds=ATB->RollingHP.MinimumLethalDrainSeconds;
    }
    return Result;
}
void UReditusRollingHPComponent::InitializeBattle(UReditusATBComponent* InATB)
{
    ATB=InATB; bBattleAttached=true; bBridgeSeen=false;
    LastPublishedHP=HPValue(GetOwner(),TEXT("currentHP"));
    Meter.Reset(LastPublishedHP,HPValue(GetOwner(),TEXT("maxHP")));
}
int32 UReditusRollingHPComponent::CalculationHP() const
{
    // Publishing a counter step must not award Overdrive as another hit.
    if (bPublishing) return PublishingHP;
    return Enabled() ? GetTargetHP() : HPValue(GetOwner(),TEXT("currentHP"));
}
int32 UReditusRollingHPComponent::GetTargetHP() const
{ return static_cast<int32>(Meter.Destination()); }
bool UReditusRollingHPComponent::IsLethalDamagePending() const
{ return Enabled() && Meter.IsLethal(); }
bool UReditusRollingHPComponent::CanReceiveHealing() const
{
    return !bBattleAttached || !ATB.IsValid() || !ATB->IsATBRunning() || HPValue(GetOwner(),TEXT("currentHP"))>0;
}
int32 UReditusRollingHPComponent::ResolveWrite(int32 RequestedHP)
{
    if (bPublishing)
    {
        // Consume only this internal SetHP call. Subscriber callbacks are then
        // free to queue another hit/heal without bypassing the rolling rules.
        bPublishing=false;
        return RequestedHP;
    }
    bBridgeSeen=true;
    AActor* Unit=GetOwner();
    const int32 Current=HPValue(Unit,TEXT("currentHP"));
    const int32 Maximum=HPValue(Unit,TEXT("maxHP"));
    if (!Enabled() || Current<=0)
    {
        // Initialization and revival keep their original immediate behavior.
        Meter.Reset(RequestedHP,Maximum); LastPublishedHP=RequestedHP;
        return RequestedHP;
    }
    if (Current!=LastPublishedHP) Meter.Reset(Current,Maximum);
    Meter.SetMaximum(Maximum);
    Meter.SetTarget(RequestedHP,Settings());
    LastPublishedHP=Meter.CounterInteger();
    return LastPublishedHP;
}
bool UReditusRollingHPComponent::Publish(int32 HP)
{
    AActor* Unit=GetOwner();
    UFunction* Function=IsValid(Unit) ? Unit->FindFunction(TEXT("SetHP")) : nullptr;
    FIntProperty* Argument=nullptr;
    if (!Function || Function->NumParms!=1) return false;
    for (TFieldIterator<FProperty> It(Function); It; ++It)
        if (It->HasAnyPropertyFlags(CPF_Parm) && !It->HasAnyPropertyFlags(CPF_OutParm|CPF_ReturnParm))
            Argument=CastField<FIntProperty>(*It);
    if (!Argument) return false;
    TArray<uint8> Parameters;
    Parameters.SetNumZeroed(Function->ParmsSize);
    Argument->SetPropertyValue_InContainer(Parameters.GetData(),HP);
    LastPublishedHP=HP; PublishingHP=HP;
    struct FPublishingGuard
    {
        bool& Value;
        explicit FPublishingGuard(bool& InValue) : Value(InValue) { Value=true; }
        ~FPublishingGuard() { Value=false; }
    } Guard(bPublishing);
    Unit->ProcessEvent(Function,Parameters.GetData());
    return true;
}
void UReditusRollingHPComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
    if (!bBattleAttached || !ATB.IsValid() || !ATB->IsATBRunning() || ATB->bPaused || !bBridgeSeen) return;
    AActor* Unit=GetOwner();
    if (!IsValid(Unit)) return;
    const int32 Current=HPValue(Unit,TEXT("currentHP"));
    const int32 Maximum=HPValue(Unit,TEXT("maxHP"));
    if (Current!=LastPublishedHP) Meter.Reset(Current,Maximum);
    Meter.SetMaximum(Maximum);
    if (!ATB->RollingHP.bEnabled)
    {
        // A runtime disable settles outstanding damage/healing once.
        const int32 Target=GetTargetHP();
        Meter.Reset(Target,Maximum);
        if (Current!=Target) Publish(Target);
        return;
    }
    if (Meter.IsDead()) return;
    Meter.Advance(ReditusRollingHP::Positive(DeltaTime)*ReditusRollingHP::Positive(ATB->Tuning.ClockMultiplier),Settings());
    const int32 Next=Meter.CounterInteger();
    if (Current!=Next) Publish(Next);
}
void UReditusRollingHPComponent::FinishBattle()
{
    if (!bBattleAttached) return;
    bBattleAttached=false;
    const int32 Current=HPValue(GetOwner(),TEXT("currentHP"));
    if (Current!=LastPublishedHP) Meter.Reset(Current,HPValue(GetOwner(),TEXT("maxHP")));
    Meter.FinishBattle();
    const int32 Next=Meter.CounterInteger();
    if (IsValid(GetOwner()) && HPValue(GetOwner(),TEXT("currentHP"))!=Next) Publish(Next);
    ATB.Reset();
}
int32 UReditusRollingHPLibrary::GetHPForCalculation(AActor* Unit)
{
    if (auto* Component=Rolling(Unit)) return Component->CalculationHP();
    return HPValue(Unit,TEXT("currentHP"));
}
int32 UReditusRollingHPLibrary::ResolveHPWrite(AActor* Unit,int32 RequestedHP)
{
    if (auto* Component=Rolling(Unit)) return Component->ResolveWrite(RequestedHP);
    return RequestedHP;
}
bool UReditusRollingHPLibrary::CanReceiveBattleHealing(AActor* Unit)
{
    if (!IsValid(Unit)) return false;
    if (auto* Component=Rolling(Unit)) return Component->CanReceiveHealing();
    return true;
}
