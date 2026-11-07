#include "ReditusATBComponent.h"
#include "ReditusATBCore.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "Particles/ParticleSystemComponent.h"
#include "UObject/UnrealType.h"

namespace
{
    bool IsLegacyPlayer(const AActor* Unit)
    {
        if (!IsValid(Unit)) return false;
        for (const UClass* Type=Unit->GetClass(); Type; Type=Type->GetSuperClass())
            if (Type->GetPathName()==TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Units/PlayerUnits/BP_PlayerUnitBase.BP_PlayerUnitBase_C")) return true;
        return false;
    }
    TArray<AActor*> ActorList(UObject* Object,FName Name)
    {
        TArray<AActor*> Result;
        auto* Field=IsValid(Object) ? FindFProperty<FArrayProperty>(Object->GetClass(),Name) : nullptr;
        const auto* Item=Field ? CastField<FObjectPropertyBase>(Field->Inner) : nullptr;
        if (!Item) return Result;
        FScriptArrayHelper Array(Field,Field->ContainerPtrToValuePtr<void>(Object));
        for (int32 Index=0; Index<Array.Num(); ++Index)
            if (auto* Actor=Cast<AActor>(Item->GetObjectPropertyValue(Array.GetRawPtr(Index)))) Result.AddUnique(Actor);
        return Result;
    }
    double ReadNumber(const UObject* Object,FName PropertyName,double Fallback)
    {
        if (!IsValid(Object)) return Fallback;
        const FNumericProperty* Value=FindFProperty<FNumericProperty>(Object->GetClass(),PropertyName);
        if (!Value) return Fallback;
        const void* Address=Value->ContainerPtrToValuePtr<void>(Object);
        return Value->IsInteger() ? static_cast<double>(Value->GetSignedIntPropertyValue(Address)) : Value->GetFloatingPointPropertyValue(Address);
    }
    bool ReadBool(const UObject* Object,FName PropertyName)
    {
        const FBoolProperty* Value=IsValid(Object) ? FindFProperty<FBoolProperty>(Object->GetClass(),PropertyName) : nullptr;
        return Value && Value->GetPropertyValue_InContainer(Object);
    }
    bool IsIncapacitated(const AActor* Unit)
    {
        return ReadBool(Unit,TEXT("isStun")) || ReadBool(Unit,TEXT("IsSleeping"));
    }
    void RemoveLegacyActionEndBinding(AActor* Unit,AActor* Controller)
    {
        if (!IsValid(Unit) || !IsValid(Controller)) return;
        static const FName HandlerName(TEXT("ATB_ActionFinished"));
        UFunction* Handler=Controller->FindFunction(HandlerName);
        auto* Event=FindFProperty<FMulticastInlineDelegateProperty>(Unit->GetClass(),TEXT("OnTurnEnded"));
        if (!Handler || !Event || !Event->SignatureFunction || !Handler->IsSignatureCompatibleWith(Event->SignatureFunction)) return;
        FScriptDelegate Binding;
        Binding.BindUFunction(Controller,HandlerName);
        Event->ContainerPtrToValuePtr<FMulticastScriptDelegate>(Unit)->Remove(Binding);
    }

    TArray<TWeakObjectPtr<UObject>> LegacyStaggerBuffs(AActor* Unit)
    {
        TArray<TWeakObjectPtr<UObject>> Staggers;
        if (!IsValid(Unit)) return Staggers;
        auto* Map=FindFProperty<FMapProperty>(Unit->GetClass(),TEXT("activeBuffs"));
        auto* Value=Map ? CastField<FObjectPropertyBase>(Map->ValueProp) : nullptr;
        if (!Value) return Staggers;
        FScriptMapHelper Buffs(Map,Map->ContainerPtrToValuePtr<void>(Unit));
        // Expiry removes an entry and may destroy its actor, so iterate a snapshot.
        for (int32 Index=0; Index<Buffs.GetMaxIndex(); ++Index)
        {
            if (!Buffs.IsValidIndex(Index)) continue;
            UObject* Buff=Value->GetObjectPropertyValue(Buffs.GetValuePtr(Index));
            if (!IsValid(Buff)) continue;
            bool bStagger=false;
            for (UClass* Type=Buff->GetClass(); Type; Type=Type->GetSuperClass())
                if (Type->GetPathName()==TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Skills/Buffs/BP_Staggered.BP_Staggered_C"))
                { bStagger=true; break; }
            auto* Target=FindFProperty<FObjectPropertyBase>(Buff->GetClass(),TEXT("targetUnit"));
            if (bStagger && Target && Target->GetObjectPropertyValue_InContainer(Buff)==Unit)
                Staggers.AddUnique(TWeakObjectPtr<UObject>(Buff));
        }
        return Staggers;
    }
    void AdvanceLegacyStaggerRecovery(AActor* Unit)
    {
        for (const auto& Entry:LegacyStaggerBuffs(Unit))
        {
            UObject* Buff=Entry.Get();
            if (!Buff || ReadBool(Buff,TEXT("Permenant State")) || ReadNumber(Buff,TEXT("turnLeft"),0.0)<=0.0) continue;
            UFunction* Handler=Buff->FindFunction(TEXT("OnTurnChangedHandler"));
            if (!Handler || Handler->NumParms!=1) continue;
            FIntProperty* TurnArgument=nullptr;
            for (TFieldIterator<FProperty> It(Handler); It; ++It)
                if (It->HasAnyPropertyFlags(CPF_Parm) && !It->HasAnyPropertyFlags(CPF_OutParm|CPF_ReturnParm))
                    TurnArgument=CastField<FIntProperty>(*It);
            if (!TurnArgument) continue;
            TArray<uint8> Parameters;
            Parameters.SetNumZeroed(Handler->ParmsSize);
            // The original handler ignores currentTurn and decrements its own
            // turnLeft once, invoking its own speed/stun/stamina restoration.
            Buff->ProcessEvent(Handler,Parameters.GetData());
        }
    }
    const FName AmbushedProperty(TEXT("IsAmbushed"));

    void ClearDefendStance(AActor* Unit)
    {
        static const FName DefendingProperty(TEXT("IsDefending"));
        if (IsValid(Unit))
            if (auto* Field=FindFProperty<FBoolProperty>(Unit->GetClass(),DefendingProperty))
                Field->SetPropertyValue_InContainer(Unit,false);
    }

    void ClearAmpParticle(AActor* Unit)
    {
        if (!IsValid(Unit)) return;
        auto* Field=FindFProperty<FObjectPropertyBase>(Unit->GetClass(),TEXT("AmpParticle"));
        if (!Field || !Field->PropertyClass || !Field->PropertyClass->IsChildOf(UParticleSystemComponent::StaticClass())) return;
        auto* Particle=Cast<UParticleSystemComponent>(Field->GetObjectPropertyValue_InContainer(Unit));
        // AMP input uses this reference to decide whether it needs a fresh emitter.
        Field->SetObjectPropertyValue_InContainer(Unit,nullptr);
        if (IsValid(Particle)) Particle->DestroyComponent();
    }

    void ClearStaggeredCommand(AActor* Unit,bool bActionRunning)
    {
        if (!IsValid(Unit)) return;
        static const FName Flags[]={TEXT("isInputBlocked"),TEXT("TargetSet"),TEXT("Confirmed")};
        for (FName Flag:Flags)
            if (auto* Field=FindFProperty<FBoolProperty>(Unit->GetClass(),Flag))
                Field->SetPropertyValue_InContainer(Unit,Flag==TEXT("isInputBlocked"));
        if (auto* Field=FindFProperty<FObjectPropertyBase>(Unit->GetClass(),TEXT("PS_GPS_Selection_02")))
            if (auto* FX=Cast<USceneComponent>(Field->GetObjectPropertyValue_InContainer(Unit)))
                if (IsValid(FX)) FX->SetVisibility(false);
        // An in-flight animation still owns its target and committed resources
        // until its existing finish/interruption callback acknowledges cleanup.
        if (bActionRunning) return;
        if (auto* Field=FindFProperty<FObjectPropertyBase>(Unit->GetClass(),TEXT("currentTarget")))
            Field->SetObjectPropertyValue_InContainer(Unit,nullptr);
        if (auto* Field=FindFProperty<FIntProperty>(Unit->GetClass(),TEXT("CurrentAmpPointsInPlay")))
            Field->SetPropertyValue_InContainer(Unit,0);
        ClearAmpParticle(Unit);
    }

    void ResolveAmbushFacing(AActor* Unit)
    {
        // BP_BattleBase sets this flag and faces the Pixel2D component away at spawn.
        // Restore the same facing as the ambush branch in BP_UnitBase::EndTurn.
        if (auto* Ambushed=FindFProperty<FBoolProperty>(Unit->GetClass(),AmbushedProperty))
            Ambushed->SetPropertyValue_InContainer(Unit,false);
        if (auto* Sprite=FindFProperty<FObjectPropertyBase>(Unit->GetClass(),TEXT("Pixel2D")))
            if (auto* Scene=Cast<USceneComponent>(Sprite->GetObjectPropertyValue_InContainer(Unit)))
                if (IsValid(Scene)) Scene->SetRelativeRotation(FRotator(0.f,-270.f,0.f));
    }

    bool CanResolveStartWithoutEffects(const AActor* Unit,FName HPProperty)
    {
        if (!IsValid(Unit) || !(ReadNumber(Unit,HPProperty,0.0)>0.0)) return false;
        // A surprised unit needs its first cycle to recover and cannot be made ready by this shortcut.
        if (ReadBool(Unit,AmbushedProperty)) return false;
        // Match the Reditus cycle-start adapter. Unknown unit types take the
        // normal Blueprint path instead of assuming that no effect is active.
        static const FName BlockingFlags[]={TEXT("IsRegen"),TEXT("Is DOT"),TEXT("IsSleeping"),TEXT("isStun"),TEXT("IsDead?")};
        for (FName Flag : BlockingFlags)
        {
            const auto* Property=FindFProperty<FBoolProperty>(Unit->GetClass(),Flag);
            if (!Property || Property->GetPropertyValue_InContainer(Unit)) return false;
        }
        return true;
    }
    ReditusATBCore::Settings CoreSettings(const FReditusATBTuning& Tuning,bool bFreezeStaggerGauge)
    {
        ReditusATBCore::Settings Result;
        Result.BaseFillSeconds=Tuning.BaseFillSeconds;
        Result.ReferenceSpeed=Tuning.ReferenceSpeed;
        Result.FillRateMultiplier=Tuning.FillRateMultiplier;
        Result.RefillGauge=Tuning.RefillGauge;
        Result.BlockedRecoverySeconds=Tuning.StaggerRecoveryTurnSeconds;
        Result.FreezeBlockedGauge=bFreezeStaggerGauge;
        Result.ContinueFillingDuringExecution=Tuning.bFillDuringActions;
        Result.ContinuePreparingDuringExecution=Tuning.bPrepareDuringActions;
        return Result;
    }
}

UReditusATBComponent::UReditusATBComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    Scheduler=MakeUnique<ReditusATBCore::Scheduler>();
}
UReditusATBComponent::~UReditusATBComponent() = default;

bool UReditusATBComponent::UnitAlive(AActor* Unit) const
{
    return IsValid(Unit) && (!bRemoveUnitsWithZeroHP || ReadNumber(Unit,HPProperty,1.0)>0.0);
}
AActor* UReditusATBComponent::ResolveActor(int32 UnitID) const
{
    for (const auto& Pair : UnitIDs) if (Pair.Value==UnitID) return Pair.Key;
    return nullptr;
}
bool UReditusATBComponent::RegisterUnit(AActor* Unit,float FallbackSpeed)
{
    if (bStopping || !UnitAlive(Unit) || UnitIDs.Contains(Unit) || NextUnitID==MAX_int32) return false;
    const int32 Identity=NextUnitID++;
    const double UnitSpeed=bReadLiveSpeed ? ReadNumber(Unit,SpeedProperty,FallbackSpeed) : FallbackSpeed;
    if (!Scheduler->Add(Identity,UnitSpeed,Tuning.InitialGauge)) return false;
    UnitIDs.Add(Unit,Identity);
    if (IsLegacyPlayer(Unit))
    {
        auto* Health=Unit->FindComponentByClass<UReditusRollingHPComponent>();
        if (!Health)
        {
            Health=NewObject<UReditusRollingHPComponent>(Unit);
            Unit->AddInstanceComponent(Health);
            Health->RegisterComponent();
        }
        Health->InitializeBattle(this);
        RollingUnits.AddUnique(TWeakObjectPtr<UReditusRollingHPComponent>(Health));
        AddTickPrerequisiteComponent(Health);
    }
    // Fallback is a normal override only when live reads are explicitly disabled.
    if (!bReadLiveSpeed) SpeedOverrides.Add(Unit,FallbackSpeed);
    return true;
}
void UReditusATBComponent::UnregisterUnit(AActor* Unit)
{
    const int32* Found=UnitIDs.Find(Unit);
    if (!Found) return;
    const ReditusATBCore::Unit* State=Scheduler->Find(*Found);
    FReditusATBAction Removed;
    bool bCancelled=false,bHadStarted=false;
    if (State)
    {
        if (const FReditusATBAction* Action=PendingActions.Find(State->Action))
        {
            Removed=*Action; bCancelled=true;
            bHadStarted=State->State==ReditusATBCore::Phase::Executing || State->State==ReditusATBCore::Phase::CycleEnd;
            PendingActions.Remove(State->Action);
            ExecutionInterruptRequests.Remove(State->Action);
        }
    }
    Scheduler->Remove(*Found);
    UnitIDs.Remove(Unit); SpeedOverrides.Remove(Unit); TimingEffects.Remove(Unit);
    if (bHadStarted) RemoveLegacyActionEndBinding(Unit,GetOwner());
    if (IsValid(Unit))
        if (auto* Health=Unit->FindComponentByClass<UReditusRollingHPComponent>())
        {
            RemoveTickPrerequisiteComponent(Health);
            // A dead actor stays associated with this battle until StopATB.
            // Its ordinary Heal event must still reject a missed deadline.
            if (ReadNumber(Unit,HPProperty,0.0)>0.0)
            {
                Health->FinishBattle();
                RollingUnits.Remove(TWeakObjectPtr<UReditusRollingHPComponent>(Health));
            }
        }
    ClearAmpParticle(Unit);
    if (bCancelled) OnActionCancelled.Broadcast(Removed,bHadStarted);
}
bool UReditusATBComponent::StartATB()
{
    if (bStopping || bRunning || UnitIDs.Num()==0) return false;
    if (!OnCycleStartRequested.IsBound() || !OnExecuteActionRequested.IsBound() || !OnCycleEndRequested.IsBound())
    {
        UE_LOG(LogTemp,Warning,TEXT("Reditus ATB: bind cycle-start, execute-action and cycle-end handlers before Start ATB."));
        return false;
    }
    bRunning=true; PulseElapsed=0.0;
    return true;
}
void UReditusATBComponent::StopATB()
{
    if (bStopping) return;
    bStopping=true;
    ++ClockEpoch;
    bRunning=false;
    const TArray<FReditusATBUnitView> Before=GetUnitStates();
    TArray<FReditusATBAction> Cancelled;
    PendingActions.GenerateValueArray(Cancelled);
    Scheduler->Reset(); UnitIDs.Reset(); SpeedOverrides.Reset(); TimingEffects.Reset();
    PendingActions.Reset(); ExecutionInterruptRequests.Reset(); PulseElapsed=0.0;
    const auto HealthUnits=RollingUnits;
    RollingUnits.Reset();
    for (const auto& Entry:HealthUnits)
        if (auto* Health=Entry.Get())
        {
            RemoveTickPrerequisiteComponent(Health);
            Health->FinishBattle();
        }
    for (const FReditusATBUnitView& State : Before) ClearAmpParticle(State.Unit);
    for (const FReditusATBAction& Action : Cancelled)
    {
        bool bHadStarted=false;
        for (const FReditusATBUnitView& State : Before)
            if (State.Unit==Action.Unit) bHadStarted=State.Phase==EReditusATBPhase::Executing || State.Phase==EReditusATBPhase::CycleEnd;
        if (bHadStarted) RemoveLegacyActionEndBinding(Action.Unit,GetOwner());
        OnActionCancelled.Broadcast(Action,bHadStarted);
    }
    bStopping=false;
}
void UReditusATBComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopATB();
    Super::EndPlay(EndPlayReason);
}
void UReditusATBComponent::SetUnitSpeedOverride(AActor* Unit,float Speed)
{
    if (UnitIDs.Contains(Unit)) SpeedOverrides.Add(Unit,Speed);
}
void UReditusATBComponent::ClearUnitSpeedOverride(AActor* Unit)
{
    SpeedOverrides.Remove(Unit);
}
void UReditusATBComponent::UpdateTimingRates(AActor* Unit,int32 UnitID)
{
    std::vector<ReditusATBCore::TimingEffect> Effects;
    if (const FReditusATBTimingEffectList* List=TimingEffects.Find(Unit))
        for (const FReditusATBTimingEffect& Effect : List->Entries)
        {
            ReditusATBCore::TimingEffect CoreEffect;
            CoreEffect.Gauge=Effect.GaugeMultiplier; CoreEffect.Preparation=Effect.PreparationMultiplier;
            CoreEffect.PauseGauge=Effect.bPauseGauge; CoreEffect.PausePreparation=Effect.bPausePreparation;
            Effects.push_back(CoreEffect);
        }
    if (Tuning.bSpeedAffectsPreparation)
    {
        const ReditusATBCore::Unit* State=Scheduler->Find(UnitID);
        ReditusATBCore::TimingEffect SpeedEffect;
        SpeedEffect.Preparation=State && Tuning.ReferenceSpeed>0.f ? State->Speed/Tuning.ReferenceSpeed : 0.0;
        Effects.push_back(SpeedEffect);
    }
    const ReditusATBCore::TimingEffect Combined=ReditusATBCore::CombineTimingEffects(Effects,Tuning.MaximumTimingMultiplier);
    Scheduler->SetRates(UnitID,Combined.Gauge,Combined.Preparation,Combined.PauseGauge,Combined.PausePreparation);
}
bool UReditusATBComponent::AddOrUpdateTimingEffect(AActor* Unit,const FReditusATBTimingEffect& Effect)
{
    const int32* ID=UnitIDs.Find(Unit);
    if (!ID || Effect.SourceID.IsNone() || !FMath::IsFinite(Effect.GaugeMultiplier) ||
        !FMath::IsFinite(Effect.PreparationMultiplier) || Effect.GaugeMultiplier<0.f || Effect.PreparationMultiplier<0.f) return false;
    FReditusATBTimingEffectList& List=TimingEffects.FindOrAdd(Unit);
    bool bReplaced=false;
    for (FReditusATBTimingEffect& Existing : List.Entries)
        if (Existing.SourceID==Effect.SourceID) { Existing=Effect; bReplaced=true; break; }
    if (!bReplaced) List.Entries.Add(Effect);
    UpdateTimingRates(Unit,*ID);
    return true;
}
bool UReditusATBComponent::RemoveTimingEffect(AActor* Unit,FName SourceID)
{
    const int32* ID=UnitIDs.Find(Unit);
    FReditusATBTimingEffectList* List=TimingEffects.Find(Unit);
    if (!ID || !List) return false;
    const int32 Removed=List->Entries.RemoveAll([SourceID](const FReditusATBTimingEffect& E) { return E.SourceID==SourceID; });
    if (List->Entries.Num()==0) TimingEffects.Remove(Unit);
    UpdateTimingRates(Unit,*ID);
    return Removed>0;
}
TArray<FReditusATBTimingEffect> UReditusATBComponent::GetTimingEffects(AActor* Unit) const
{
    const FReditusATBTimingEffectList* List=TimingEffects.Find(Unit);
    return List ? List->Entries : TArray<FReditusATBTimingEffect>();
}
void UReditusATBComponent::NotifyReady(AActor* Unit,int32 CycleToken)
{
    const auto IsStillReady=[this,Unit,CycleToken]()
    {
        if (!bRunning || !UnitAlive(Unit)) return false;
        const FReditusATBUnitView State=GetUnitState(Unit);
        return CanIssueCommands(Unit) && State.CycleToken==CycleToken;
    };
    if (!IsStillReady()) return;
    if (Tuning.AmpTiming==EReditusATBAmpTiming::OnReady)
    {
        const int32* Identity=UnitIDs.Find(Unit);
        if (Identity && Scheduler->ClaimReadyAmp(*Identity,CycleToken))
        {
            RequestAmp(Unit);
            // The Blueprint callback may have stopped the clock or removed
            // the actor. Never send Ready for the old cycle in that case.
            if (!IsStillReady()) return;
        }
    }
    OnUnitReady.Broadcast(Unit,CycleToken);
}
bool UReditusATBComponent::PushGaugeBack(AActor* Unit,float Fraction)
{
    const int32* ID=UnitIDs.Find(Unit);
    const FReditusATBUnitView Before=GetUnitState(Unit);
    if (!bRunning || !ID || !Scheduler->PushBack(*ID,Fraction)) return false;
    if (Before.Phase==EReditusATBPhase::Ready) OnUnitReadinessLost.Broadcast(Unit,Before.CycleToken);
    return true;
}
bool UReditusATBComponent::InterruptPendingAction(AActor* Unit,float GaugePushback)
{
    FReditusATBAction Action;
    const int32* ID=UnitIDs.Find(Unit);
    if (!bRunning || !ID || !FMath::IsFinite(GaugePushback) || GaugePushback<0.f ||
        !GetPendingAction(Unit,Action) || !Action.bInterruptibleBeforeExecution || !Scheduler->CancelPending(*ID)) return false;
    // Complete cancellation and pushback before calling Blueprint, which may submit another command.
    if (GaugePushback>0.f) Scheduler->PushBack(*ID,GaugePushback);
    PendingActions.Remove(Action.ActionToken);
    OnActionCancelled.Broadcast(Action,false);
    NotifyReady(Unit,Action.CycleToken);
    return true;
}
bool UReditusATBComponent::RequestExecutionInterruption(AActor* Unit)
{
    FReditusATBAction Action;
    if (!bRunning || GetUnitState(Unit).Phase!=EReditusATBPhase::Executing || !GetPendingAction(Unit,Action) ||
        !Action.bInterruptibleDuringExecution || ExecutionInterruptRequests.Contains(Action.ActionToken) ||
        !OnExecutionInterruptRequested.IsBound()) return false;
    ExecutionInterruptRequests.Add(Action.ActionToken);
    OnExecutionInterruptRequested.Broadcast(Action);
    return true;
}
bool UReditusATBComponent::IsUnitStaggered(AActor* Unit) const
{
    return IsValid(Unit) && (ReadBool(Unit,TEXT("Staggered")) || LegacyStaggerBuffs(Unit).Num()>0);
}
bool UReditusATBComponent::CanIssueCommands(AActor* Unit) const
{
    if (!bRunning || !UnitAlive(Unit) || IsIncapacitated(Unit) || IsUnitStaggered(Unit)) return false;
    const int32* Identity=UnitIDs.Find(Unit);
    const ReditusATBCore::Unit* State=Identity ? Scheduler->Find(*Identity) : nullptr;
    return State && !State->CommandBlocked && State->State==ReditusATBCore::Phase::Ready;
}
void UReditusATBComponent::SynchronizeLegacyStagger()
{
    struct FChangedUnit
    {
        AActor* Unit=nullptr;
        int32 Cycle=INDEX_NONE;
        bool bActionRunning=false;
        bool bCancelled=false;
        FReditusATBAction Cancelled;
    };
    TArray<FChangedUnit> Changes;
    // Update all phases before broadcasting: a handoff must not select a
    // second staggered ally that happens to appear later in the roster.
    for (const auto& Pair:UnitIDs)
    {
        const ReditusATBCore::Unit* State=Scheduler->Find(Pair.Value);
        if (!State || !UnitAlive(Pair.Key)) continue;
        const bool bHold=IsUnitStaggered(Pair.Key);
        const bool bChanged=bHold && !State->CommandBlocked;
        const int32 ActionToken=State->Action;
        const int32 Cycle=State->Cycle;
        const bool bActionRunning=State->State==ReditusATBCore::Phase::Executing ||
            (State->State==ReditusATBCore::Phase::CycleEnd && ActionToken!=INDEX_NONE);
        Scheduler->SetCommandBlocked(Pair.Value,bHold);
        const bool bForfeited=bHold && Scheduler->ForfeitCommand(Pair.Value);
        if (!bChanged && !bForfeited) continue;
        FChangedUnit& Change=Changes.AddDefaulted_GetRef();
        Change.Unit=Pair.Key; Change.Cycle=Cycle; Change.bActionRunning=bActionRunning;
        if (bForfeited)
        {
            Change.bCancelled=PendingActions.RemoveAndCopyValue(ActionToken,Change.Cancelled);
            ExecutionInterruptRequests.Remove(ActionToken);
        }
    }
    const uint64 Epoch=ClockEpoch;
    for (const FChangedUnit& Change:Changes)
    {
        const int32* Identity=UnitIDs.Find(Change.Unit);
        const ReditusATBCore::Unit* Current=Identity ? Scheduler->Find(*Identity) : nullptr;
        if (!Current || Current->Cycle!=Change.Cycle || !Current->CommandBlocked || !IsValid(Change.Unit)) continue;
        if (!IsUnitStaggered(Change.Unit)) { Scheduler->SetCommandBlocked(*Identity,false); continue; }
        ClearStaggeredCommand(Change.Unit,Change.bActionRunning);
        if (Change.bCancelled) OnActionCancelled.Broadcast(Change.Cancelled,false);
        if (!bRunning || ClockEpoch!=Epoch) return;
        if (!UnitIDs.Contains(Change.Unit) || !IsValid(Change.Unit)) continue;
        OnUnitReadinessLost.Broadcast(Change.Unit,Change.Cycle);
        if (!bRunning || ClockEpoch!=Epoch) return;
        // Never release a live animation/projectile's lane without its cleanup
        // acknowledgment. Existing interrupt-enabled actions use that handler.
        if (Change.bActionRunning) RequestExecutionInterruption(Change.Unit);
        if (!bRunning || ClockEpoch!=Epoch) return;
    }
}
void UReditusATBComponent::AdvanceFrozenStaggerRecovery()
{
    if (!bFreezeLegacyStagger || !bAdvanceLegacyStaggerOnCycleEnd) return;
    const uint64 Epoch=ClockEpoch;
    const auto Snapshot=Scheduler->Snapshot();
    for (const auto& State:Snapshot)
    {
        AActor* Unit=ResolveActor(State.ID);
        for (int32 Pulse=0; Pulse<8 && UnitAlive(Unit) && IsUnitStaggered(Unit) &&
            Scheduler->ConsumeBlockedRecovery(State.ID); ++Pulse)
        {
            AdvanceLegacyStaggerRecovery(Unit);
            if (!bRunning || bPaused || ClockEpoch!=Epoch) return;
        }
        if (UnitIDs.Contains(Unit) && !IsUnitStaggered(Unit)) Scheduler->SetCommandBlocked(State.ID,false);
    }
}
FReditusATBUnitView UReditusATBComponent::GetUnitState(AActor* Unit) const
{
    FReditusATBUnitView Result;
    Result.Unit=Unit;
    const int32* Identity=UnitIDs.Find(Unit);
    const ReditusATBCore::Unit* State=Identity ? Scheduler->Find(*Identity) : nullptr;
    if (!State) return Result;
    static_assert(static_cast<int>(EReditusATBPhase::Filling)==static_cast<int>(ReditusATBCore::Phase::Filling) &&
        static_cast<int>(EReditusATBPhase::CycleStart)==static_cast<int>(ReditusATBCore::Phase::CycleStart) &&
        static_cast<int>(EReditusATBPhase::Ready)==static_cast<int>(ReditusATBCore::Phase::Ready) &&
        static_cast<int>(EReditusATBPhase::Preparing)==static_cast<int>(ReditusATBCore::Phase::Preparing) &&
        static_cast<int>(EReditusATBPhase::Queued)==static_cast<int>(ReditusATBCore::Phase::Queued) &&
        static_cast<int>(EReditusATBPhase::Executing)==static_cast<int>(ReditusATBCore::Phase::Executing) &&
        static_cast<int>(EReditusATBPhase::CycleEnd)==static_cast<int>(ReditusATBCore::Phase::CycleEnd) &&
        static_cast<int>(EReditusATBPhase::Inactive)==static_cast<int>(ReditusATBCore::Phase::Inactive),"ATB phase mapping changed");
    Result.Phase=static_cast<EReditusATBPhase>(State->State);
    Result.Gauge=static_cast<float>(State->Gauge);
    Result.CycleToken=State->Cycle;
    Result.CompletedCycles=State->CompletedCycles;
    Result.GaugeMultiplier=static_cast<float>(State->GaugeMultiplier);
    Result.PreparationMultiplier=static_cast<float>(State->PreparationMultiplier);
    const bool bStaggerHeld=bFreezeLegacyStagger && IsUnitStaggered(Unit);
    Result.bGaugePaused=(bFreezeLegacyStagger && State->CommandBlocked) || bStaggerHeld || State->PauseGauge || State->GaugeMultiplier<=0.0;
    Result.bPreparationPaused=State->CommandBlocked || bStaggerHeld || State->PausePreparation || State->PreparationMultiplier<=0.0;
    if (State->Action!=INDEX_NONE)
    {
        Result.PreparationRemaining=static_cast<float>(State->PreparationRemaining);
        Result.PreparationProgress=State->PreparationTotal>0.0 ? static_cast<float>(ReditusATBCore::Fraction(1.0-State->PreparationRemaining/State->PreparationTotal)) : 1.f;
    }
    return Result;
}
bool UReditusATBComponent::GetPendingAction(AActor* Unit,FReditusATBAction& Action) const
{
    Action=FReditusATBAction();
    const int32* ID=UnitIDs.Find(Unit);
    const ReditusATBCore::Unit* State=ID ? Scheduler->Find(*ID) : nullptr;
    const FReditusATBAction* Saved=State ? PendingActions.Find(State->Action) : nullptr;
    if (!Saved) return false;
    Action=*Saved;
    return true;
}
TArray<FReditusATBUnitView> UReditusATBComponent::GetUnitStates() const
{
    TArray<FReditusATBUnitView> Result;
    // Existing PickNextATBCommandUnit graphs consume this array. Put Ready
    // units first in arrival order, preserving all other states and party slots.
    for (const ReditusATBCore::Unit& State : Scheduler->SnapshotInCommandOrder()) Result.Add(GetUnitState(ResolveActor(State.ID)));
    return Result;
}
void UReditusATBComponent::RequestAmp(AActor* Unit)
{
    // AMP recharges throughout the fight, independently of legacy exhaustion.
    if (bRunning && UnitAlive(Unit) && Tuning.AmpGain>0)
        OnAmpGainRequested.Broadcast(Unit,Tuning.AmpGain);
}
bool UReditusATBComponent::CompleteCycleStart(AActor* Unit,int32 CycleToken,bool bCanAct)
{
    if (!bRunning) return false;
    const int32* Identity=UnitIDs.Find(Unit);
    const ReditusATBCore::Unit* State=Identity ? Scheduler->Find(*Identity) : nullptr;
    const auto Active=Scheduler->ActiveWork();
    if (!State || State->Cycle!=CycleToken || Active.Kind!=ReditusATBCore::WorkKind::CycleStart ||
        Active.Unit!=*Identity || Active.Cycle!=CycleToken) return false;
    if (!UnitAlive(Unit)) { UnregisterUnit(Unit); return false; }
    const bool bFirst=State->CompletedCycles==0;
    const bool bAmbushRecovery=ReadBool(Unit,AmbushedProperty);
    if (bAmbushRecovery || IsIncapacitated(Unit) || IsUnitStaggered(Unit)) bCanAct=false;
    const bool bAccepted=Scheduler->CompleteStart(*Identity,CycleToken,bCanAct);
    if (!bAccepted) return false;
    if (bAmbushRecovery) ResolveAmbushFacing(Unit);
    NotifyCycleStartCompleted(Unit,CycleToken,bFirst,bCanAct);
    return true;
}
void UReditusATBComponent::FinishSynchronousCycleStart(AActor* Unit,int32 CycleToken)
{
    if (!bRunning || !bFinishSynchronousLegacyCycleStart) return;
    const auto Active=Scheduler->ActiveWork();
    const int32* Identity=UnitIDs.Find(Unit);
    if (!Identity || Active.Kind!=ReditusATBCore::WorkKind::CycleStart ||
        Active.Unit!=*Identity || Active.Cycle!=CycleToken) return;
    // A lethal poison tick cannot leave a dead actor holding the start lane.
    if (!UnitAlive(Unit)) { UnregisterUnit(Unit); return; }
    // An explicit acknowledgment already changes Active, so this cannot grant
    // AMP/Ready twice. This is for the synchronous legacy status graph only.
    CompleteCycleStart(Unit,CycleToken,!IsIncapacitated(Unit) && !IsUnitStaggered(Unit));
}
bool UReditusATBComponent::RevalidateActionTargets(FReditusATBAction& Action) const
{
    static const FName LivingAlly(TEXT("SingleLivingAlly"));
    static const FName AllLivingAllies(TEXT("AllLivingAllies"));
    if (Action.TargetMode!=LivingAlly && Action.TargetMode!=AllLivingAllies) return true;
    UObject* Battle=nullptr;
    if (IsValid(GetOwner()))
        if (auto* Field=FindFProperty<FObjectPropertyBase>(GetOwner()->GetClass(),TEXT("currentBattle")))
            Battle=Field->GetObjectPropertyValue_InContainer(GetOwner());
    TArray<AActor*> Allies=ActorList(Battle,IsLegacyPlayer(Action.Unit) ? TEXT("playerUnits") : TEXT("enemyUnits"));
    // Include the registered roster too: the legacy death path can remove a
    // dead target from currentBattle before this queued action is dispatched.
    for (const auto& State:Scheduler->Snapshot())
    {
        AActor* Candidate=ResolveActor(State.ID);
        if (IsValid(Candidate) && IsLegacyPlayer(Candidate)==IsLegacyPlayer(Action.Unit)) Allies.AddUnique(Candidate);
    }
    Allies.AddUnique(Action.Unit);
    const auto Living=[this,&Allies](AActor* Candidate)
    {
        return Allies.Contains(Candidate) && UnitAlive(Candidate) && !ReadBool(Candidate,TEXT("IsDead?"));
    };
    if (Action.TargetMode==AllLivingAllies)
    {
        Action.Targets.Reset();
        for (AActor* Candidate:Allies) if (Living(Candidate)) Action.Targets.AddUnique(Candidate);
        return Action.Targets.Num()>0;
    }
    AActor* Original=Action.Targets.Num()>0 ? Action.Targets[0] : nullptr;
    if (Living(Original)) return true;
    if (!bRetargetLostLivingAlly) return false;
    const int32 Start=Allies.IndexOfByKey(Original);
    // Preserve party order, moving forward from the lost target if present.
    for (int32 Step=1; Step<=Allies.Num(); ++Step)
    {
        const int32 Index=(FMath::Max(-1,Start)+Step)%Allies.Num();
        AActor* Candidate=Allies[Index];
        if (Candidate!=Action.Unit && Living(Candidate))
        { Action.Targets.Reset(); Action.Targets.Add(Candidate); return true; }
    }
    if (Living(Action.Unit))
    { Action.Targets.Reset(); Action.Targets.Add(Action.Unit); return true; }
    return false;
}
void UReditusATBComponent::NotifyCycleStartCompleted(AActor* Unit,int32 CycleToken,bool bFirst,bool bCanAct)
{
    if (Tuning.AmpTiming==EReditusATBAmpTiming::PersonalCycle &&
        (!bFirst || Tuning.bAmpOnFirstCycle) && (bCanAct || Tuning.bAmpOnSkippedCycles)) RequestAmp(Unit);
    // The AMP Blueprint callback may stop the clock or remove this actor.
    if (bCanAct) NotifyReady(Unit,CycleToken);
}
float UReditusATBComponent::GetPreparationTime(const FReditusATBAction& Action) const
{
    if (!Action.ActionID.IsNone())
        if (const float* Duration=Tuning.PreparationByID.Find(Action.ActionID)) return FMath::IsFinite(*Duration) ? FMath::Max(0.f,*Duration) : 0.f;
    for (UClass* Type=Action.LegacyClass.Get(); Type && Type->IsChildOf(AActor::StaticClass()); Type=Type->GetSuperClass())
    {
        const TSubclassOf<AActor> Key(Type);
        if (const float* Duration=Tuning.PreparationByClass.Find(Key)) return FMath::IsFinite(*Duration) ? FMath::Max(0.f,*Duration) : 0.f;
    }
    float Duration=Tuning.CustomPreparation;
    switch (Action.Kind)
    {
    case EReditusATBActionKind::Attack: Duration=Tuning.AttackPreparation; break;
    case EReditusATBActionKind::Skill: Duration=Tuning.SkillPreparation; break;
    case EReditusATBActionKind::Item: Duration=Tuning.ItemPreparation; break;
    case EReditusATBActionKind::Overdrive: Duration=Tuning.OverdrivePreparation; break;
    case EReditusATBActionKind::Defend: Duration=Tuning.DefendPreparation; break;
    default: break;
    }
    return FMath::IsFinite(Duration) ? FMath::Max(0.f,Duration) : 0.f;
}
bool UReditusATBComponent::SubmitAction(const FReditusATBAction& Action,int32& ActionToken,float PreparationOverride)
{
    ActionToken=INDEX_NONE;
    const int32* Identity=UnitIDs.Find(Action.Unit);
    if (!Identity || !CanIssueCommands(Action.Unit) || !FMath::IsFinite(PreparationOverride)) return false;
    const float Preparation=PreparationOverride>=0.f ? PreparationOverride : GetPreparationTime(Action);
    const int32 Token=Scheduler->Submit(*Identity,Preparation);
    if (Token<0) return false;
    FReditusATBAction Saved=Action;
    if (Saved.TargetMode.IsNone() && Saved.LegacyClass &&
        (Saved.Kind==EReditusATBActionKind::Skill || Saved.Kind==EReditusATBActionKind::Overdrive || Saved.Kind==EReditusATBActionKind::Item))
    {
        // Legacy Blueprint SubmitAction graphs predate TargetMode. Keep them
        // compatible by reading their existing class metadata, never guessing
        // healing from display names or redirecting a revival to a living actor.
        UObject* Defaults=Saved.LegacyClass->GetDefaultObject();
        const int32 SkillType=static_cast<int32>(ReadNumber(Defaults,TEXT("skillType"),INDEX_NONE));
        const int32 Filter=static_cast<int32>(ReadNumber(Defaults,TEXT("unitFilter"),INDEX_NONE));
        if (Saved.Kind==EReditusATBActionKind::Item && Filter==2) Saved.TargetMode=TEXT("SingleDeadAlly");
        else if (SkillType==1) Saved.TargetMode=TEXT("SingleLivingAlly");
        else if (SkillType==3) Saved.TargetMode=TEXT("AllLivingAllies");
    }
    Saved.ActionToken=Token; Saved.CycleToken=Scheduler->Find(*Identity)->Cycle;
    Saved.AmpCommitted=FMath::Max(0,Saved.AmpCommitted);
    PendingActions.Add(Token,Saved); ActionToken=Token;
    return true;
}
bool UReditusATBComponent::CompleteAction(int32 ActionToken)
{
    if (!bRunning || !Scheduler->CompleteAction(ActionToken)) return false;
    ExecutionInterruptRequests.Remove(ActionToken);
    // ATB actions can bypass the legacy command nodes that turn off AMP FX.
    if (const FReditusATBAction* Action=PendingActions.Find(ActionToken))
        ClearAmpParticle(Action->Unit);
    return true;
}
bool UReditusATBComponent::RejectAction(int32 ActionToken)
{
    if (!bRunning) return false;
    const FReditusATBAction* Saved=PendingActions.Find(ActionToken);
    const bool bForfeit=Saved && IsUnitStaggered(Saved->Unit);
    // Status may arrive inside Blueprint validation, before the next native
    // tick has synchronized the lock. End that cycle instead of requeueing it.
    if (!(bForfeit ? Scheduler->CompleteAction(ActionToken) : Scheduler->RejectAction(ActionToken))) return false;
    ExecutionInterruptRequests.Remove(ActionToken);
    FReditusATBAction Removed;
    if (PendingActions.RemoveAndCopyValue(ActionToken,Removed))
    {
        OnActionCancelled.Broadcast(Removed,false);
        NotifyReady(Removed.Unit,Removed.CycleToken);
    }
    return true;
}
bool UReditusATBComponent::CancelPendingAction(AActor* Unit)
{
    const int32* Identity=UnitIDs.Find(Unit);
    const ReditusATBCore::Unit* State=Identity ? Scheduler->Find(*Identity) : nullptr;
    if (!bRunning || !State) return false;
    const int32 Token=State->Action;
    if (!Scheduler->CancelPending(*Identity)) return false;
    FReditusATBAction Removed;
    if (PendingActions.RemoveAndCopyValue(Token,Removed))
    {
        OnActionCancelled.Broadcast(Removed,false);
        NotifyReady(Removed.Unit,Removed.CycleToken);
    }
    return true;
}
bool UReditusATBComponent::CompleteCycleEnd(AActor* Unit,int32 CycleToken)
{
    const int32* Identity=UnitIDs.Find(Unit);
    const ReditusATBCore::Unit* State=Identity ? Scheduler->Find(*Identity) : nullptr;
    if (!bRunning || !State) return false;
    const int32 Token=State->Action;
    const bool bFrozen=bFreezeLegacyStagger && (State->CommandBlocked || IsUnitStaggered(Unit));
    if (!Scheduler->CompleteEnd(*Identity,CycleToken,CoreSettings(Tuning,bFreezeLegacyStagger))) return false;
    PendingActions.Remove(Token);
    ExecutionInterruptRequests.Remove(Token);
    if (bAdvanceLegacyStaggerOnCycleEnd && !bFrozen) AdvanceLegacyStaggerRecovery(Unit);
    return true;
}
void UReditusATBComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
    if (!bRunning || bPaused) return;
    const uint64 TickEpoch=ClockEpoch;
    TArray<AActor*> Removed;
    for (const auto& Pair : UnitIDs)
    {
        if (!UnitAlive(Pair.Key)) { Removed.Add(Pair.Key); continue; }
        const float* Override=SpeedOverrides.Find(Pair.Key);
        const ReditusATBCore::Unit* State=Scheduler->Find(Pair.Value);
        const double LiveSpeed=Override ? *Override : bReadLiveSpeed ? ReadNumber(Pair.Key,SpeedProperty,State ? State->Speed : 0.0) : State ? State->Speed : 0.0;
        Scheduler->SetSpeed(Pair.Value,LiveSpeed);
        UpdateTimingRates(Pair.Key,Pair.Value);
    }
    for (AActor* Unit : Removed)
    {
        UnregisterUnit(Unit);
        if (!bRunning || bPaused || ClockEpoch!=TickEpoch) return;
    }
    SynchronizeLegacyStagger();
    if (!bRunning || bPaused || ClockEpoch!=TickEpoch) return;
    const double Elapsed=ReditusATBCore::NonNegative(DeltaTime)*ReditusATBCore::NonNegative(Tuning.ClockMultiplier);
    if (!std::isfinite(Elapsed) || Elapsed<=0.0) return;
    // A pushed-back READY unit refills in the same cycle, without replaying start effects.
    TArray<FReditusATBUnitView> Before=GetUnitStates();
    Scheduler->Advance(Elapsed,CoreSettings(Tuning,bFreezeLegacyStagger));
    AdvanceFrozenStaggerRecovery();
    if (!bRunning || bPaused || ClockEpoch!=TickEpoch) return;
    for (const FReditusATBUnitView& State : Before)
        if (State.Phase==EReditusATBPhase::Filling)
        {
            const FReditusATBUnitView After=GetUnitState(State.Unit);
            if (After.Phase==EReditusATBPhase::Ready && After.CycleToken==State.CycleToken) NotifyReady(State.Unit,State.CycleToken);
            if (!bRunning || bPaused || ClockEpoch!=TickEpoch) return;
        }
    if (!bRunning || bPaused || ClockEpoch!=TickEpoch) return;
    if (Tuning.AmpTiming==EReditusATBAmpTiming::SharedPulse && FMath::IsFinite(Tuning.SharedAmpPulseSeconds) && Tuning.SharedAmpPulseSeconds>0.f)
    {
        PulseElapsed+=Elapsed;
        const double Interval=Tuning.SharedAmpPulseSeconds;
        int32 Pulses=0;
        while (PulseElapsed>=Interval && bRunning && !bPaused && Pulses<FMath::Max(1,Tuning.MaxAmpPulsesPerTick))
        {
            ++Pulses;
            PulseElapsed-=Interval;
            TArray<AActor*> Recipients;
            UnitIDs.GenerateKeyArray(Recipients);
            for (AActor* Unit : Recipients)
            {
                if (UnitIDs.Contains(Unit)) RequestAmp(Unit);
                if (!bRunning || bPaused || ClockEpoch!=TickEpoch) return;
            }
        }
    }
    else PulseElapsed=0.0;
    if (!bRunning || bPaused) return;
    if (Tuning.bAllowEffectFreeReadinessDuringActions &&
        Scheduler->ActiveWork().Kind==ReditusATBCore::WorkKind::Action)
    {
        // Snapshot because OnUnitReady can submit an action or remove a unit.
        auto Candidates=Scheduler->Snapshot();
        std::sort(Candidates.begin(),Candidates.end(),[](const ReditusATBCore::Unit& A,const ReditusATBCore::Unit& B)
        {
            return A.EligibleAt<B.EligibleAt || (A.EligibleAt==B.EligibleAt && A.ID<B.ID);
        });
        for (const auto& Candidate : Candidates)
        {
            if (Candidate.State!=ReditusATBCore::Phase::CycleStart) continue;
            AActor* ReadyUnit=ResolveActor(Candidate.ID);
            if (CanResolveStartWithoutEffects(ReadyUnit,HPProperty) &&
                Scheduler->CompleteEffectFreeStartDuringAction(Candidate.ID,Candidate.Cycle))
            {
                ClearDefendStance(ReadyUnit);
                NotifyCycleStartCompleted(ReadyUnit,Candidate.Cycle,Candidate.CompletedCycles==0,true);
                if (!bRunning || bPaused || ClockEpoch!=TickEpoch) return;
            }
        }
    }
    const ReditusATBCore::Work Task=Scheduler->Poll();
    AActor* Unit=ResolveActor(Task.Unit);
    if (!UnitAlive(Unit)) { UnregisterUnit(Unit); return; }
    if (Task.Kind==ReditusATBCore::WorkKind::CycleStart)
    {
        ClearDefendStance(Unit);
        OnCycleStartRequested.Broadcast(Unit,Task.Cycle);
        if (bRunning && ClockEpoch==TickEpoch) FinishSynchronousCycleStart(Unit,Task.Cycle);
    }
    else if (Task.Kind==ReditusATBCore::WorkKind::CycleEnd) OnCycleEndRequested.Broadcast(Unit,Task.Cycle);
    else if (Task.Kind==ReditusATBCore::WorkKind::Action)
    {
        if (const FReditusATBAction* Saved=PendingActions.Find(Task.Action))
        {
            FReditusATBAction Copy=*Saved;
            // An enemy may have chosen this action before being staggered.
            // Its stunned animation cannot emit the normal attack-finished
            // callback. Consume only this action and run its cycle-end cleanup.
            if (IsIncapacitated(Unit))
            {
                if (CompleteAction(Task.Action)) OnActionCancelled.Broadcast(Copy,false);
                return;
            }
            if (!RevalidateActionTargets(Copy))
            {
                // No animation/item has started. Complete this action rather
                // than waiting forever for a callback that cannot occur.
                if (CompleteAction(Task.Action)) OnActionCancelled.Broadcast(Copy,false);
                return;
            }
            if (FReditusATBAction* Current=PendingActions.Find(Task.Action)) Current->Targets=Copy.Targets;
            OnExecuteActionRequested.Broadcast(Copy);
        }
        else Scheduler->RejectAction(Task.Action);
    }
}
