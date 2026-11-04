#include "ReditusATBComponent.h"
#include "ReditusATBCore.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "Particles/ParticleSystemComponent.h"
#include "UObject/UnrealType.h"

namespace
{
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
    ReditusATBCore::Settings CoreSettings(const FReditusATBTuning& Tuning)
    {
        ReditusATBCore::Settings Result;
        Result.BaseFillSeconds=Tuning.BaseFillSeconds;
        Result.ReferenceSpeed=Tuning.ReferenceSpeed;
        Result.FillRateMultiplier=Tuning.FillRateMultiplier;
        Result.RefillGauge=Tuning.RefillGauge;
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
    for (const FReditusATBUnitView& State : Before) ClearAmpParticle(State.Unit);
    for (const FReditusATBAction& Action : Cancelled)
    {
        bool bHadStarted=false;
        for (const FReditusATBUnitView& State : Before)
            if (State.Unit==Action.Unit) bHadStarted=State.Phase==EReditusATBPhase::Executing || State.Phase==EReditusATBPhase::CycleEnd;
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
        return State.Phase==EReditusATBPhase::Ready && State.CycleToken==CycleToken;
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
    Result.bGaugePaused=State->PauseGauge || State->GaugeMultiplier<=0.0;
    Result.bPreparationPaused=State->PausePreparation || State->PreparationMultiplier<=0.0;
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
    for (const ReditusATBCore::Unit& State : Scheduler->Snapshot()) Result.Add(GetUnitState(ResolveActor(State.ID)));
    return Result;
}
void UReditusATBComponent::RequestAmp(AActor* Unit)
{
    if (bRunning && UnitAlive(Unit) && Tuning.AmpGain>0 && !ReadBool(Unit,ExhaustedProperty))
        OnAmpGainRequested.Broadcast(Unit,Tuning.AmpGain);
}
bool UReditusATBComponent::CompleteCycleStart(AActor* Unit,int32 CycleToken,bool bCanAct)
{
    if (!bRunning) return false;
    const int32* Identity=UnitIDs.Find(Unit);
    const ReditusATBCore::Unit* State=Identity ? Scheduler->Find(*Identity) : nullptr;
    if (!State || !UnitAlive(Unit)) return false;
    const bool bFirst=State->CompletedCycles==0;
    const bool bAmbushRecovery=ReadBool(Unit,AmbushedProperty);
    if (bAmbushRecovery) bCanAct=false;
    const bool bAccepted=Scheduler->CompleteStart(*Identity,CycleToken,bCanAct);
    if (!bAccepted) return false;
    if (bAmbushRecovery) ResolveAmbushFacing(Unit);
    NotifyCycleStartCompleted(Unit,CycleToken,bFirst,bCanAct);
    return true;
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
    if (!bRunning || !Identity || !UnitAlive(Action.Unit) || !FMath::IsFinite(PreparationOverride)) return false;
    const float Preparation=PreparationOverride>=0.f ? PreparationOverride : GetPreparationTime(Action);
    const int32 Token=Scheduler->Submit(*Identity,Preparation);
    if (Token<0) return false;
    FReditusATBAction Saved=Action;
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
    if (!bRunning || !Scheduler->RejectAction(ActionToken)) return false;
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
    if (!Scheduler->CompleteEnd(*Identity,CycleToken,CoreSettings(Tuning))) return false;
    PendingActions.Remove(Token);
    ExecutionInterruptRequests.Remove(Token);
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
    const double Elapsed=ReditusATBCore::NonNegative(DeltaTime)*ReditusATBCore::NonNegative(Tuning.ClockMultiplier);
    if (!std::isfinite(Elapsed) || Elapsed<=0.0) return;
    // A pushed-back READY unit refills in the same cycle, without replaying start effects.
    TArray<FReditusATBUnitView> Before=GetUnitStates();
    Scheduler->Advance(Elapsed,CoreSettings(Tuning));
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
    }
    else if (Task.Kind==ReditusATBCore::WorkKind::CycleEnd) OnCycleEndRequested.Broadcast(Unit,Task.Cycle);
    else if (Task.Kind==ReditusATBCore::WorkKind::Action)
    {
        if (const FReditusATBAction* Saved=PendingActions.Find(Task.Action))
        {
            const FReditusATBAction Copy=*Saved;
            OnExecuteActionRequested.Broadcast(Copy);
        }
        else Scheduler->RejectAction(Task.Action);
    }
}
