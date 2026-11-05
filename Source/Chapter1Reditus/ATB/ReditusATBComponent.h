#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
// Generated UObject constructors can instantiate TUniquePtr cleanup in other
// translation units, so the scheduler must be complete in this header.
#include "ReditusATBCore.h"
#include "ReditusRollingHPComponent.h"
#include "ReditusATBComponent.generated.h"

class AActor;

UENUM(BlueprintType)
enum class EReditusATBPhase : uint8
{ Filling, CycleStart, Ready, Preparing, Queued, Executing, CycleEnd, Inactive };

UENUM(BlueprintType)
enum class EReditusATBActionKind : uint8
{ Attack, Skill, Item, Overdrive, Defend, Custom };

UENUM(BlueprintType)
enum class EReditusATBAmpTiming : uint8
{ PersonalCycle, SharedPulse, Disabled, OnReady UMETA(DisplayName="On Ready") };

/** All balance values can be edited in BP component defaults/instances or at runtime. */
USTRUCT(BlueprintType)
struct CHAPTER1REDITUS_API FReditusATBTuning
{
    GENERATED_BODY()
    /** ReferenceSpeed reaches READY in this many battle seconds. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gauge", meta=(ClampMin="0.01")) float BaseFillSeconds = 5.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gauge", meta=(ClampMin="0.01")) float ReferenceSpeed = 20.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gauge", meta=(ClampMin="0.0")) float FillRateMultiplier = 1.f;
    /** Tick delta already includes world dilation. Leave 1 if BattleSpeed changes dilation. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Clock", meta=(ClampMin="0.0")) float ClockMultiplier = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gauge", meta=(ClampMin="0.0",ClampMax="1.0")) float InitialGauge = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gauge", meta=(ClampMin="0.0",ClampMax="1.0")) float RefillGauge = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Clock") bool bFillDuringActions = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Clock") bool bPrepareDuringActions = true;
    /** Reditus Blueprint adapter only: units with no start effects can become ready during another action. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Clock", meta=(DisplayName="Allow Effect-Free Readiness During Actions"))
    bool bAllowEffectFreeReadinessDuringActions = false;
    /** Optional: multiply preparation rate by live speed / ReferenceSpeed as well. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preparation") bool bSpeedAffectsPreparation = false;
    /** Cap on combined timing-effect multipliers; does not modify the unit's speed stat. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Modifiers", meta=(ClampMin="1.0")) float MaximumTimingMultiplier = 100.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preparation", meta=(ClampMin="0.0")) float AttackPreparation = 0.35f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preparation", meta=(ClampMin="0.0")) float ItemPreparation = 0.35f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preparation", meta=(ClampMin="0.0")) float SkillPreparation = 0.6f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preparation", meta=(ClampMin="0.0")) float OverdrivePreparation = 1.2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preparation", meta=(ClampMin="0.0")) float DefendPreparation = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preparation", meta=(ClampMin="0.0")) float CustomPreparation = 0.35f;
    /** Battle seconds per existing BP_Staggered turn. Zero uses this unit's normal full-gauge time. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stagger", meta=(ClampMin="0.0")) float StaggerRecoveryTurnSeconds = 0.f;
    /** Exact selected row ID takes priority over the legacy-class timing below. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preparation") TMap<FName,float> PreparationByID;
    /** Walks the supplied class's ancestry; nearest configured parent wins. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preparation") TMap<TSubclassOf<AActor>,float> PreparationByClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AMP") EReditusATBAmpTiming AmpTiming = EReditusATBAmpTiming::OnReady;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AMP", meta=(ClampMin="0")) int32 AmpGain = 1;
    /** Personal Cycle mode only. On Ready includes the first cycle. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AMP") bool bAmpOnFirstCycle = false;
    /** Personal Cycle mode only. On Ready never awards for skipped cycles. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AMP") bool bAmpOnSkippedCycles = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AMP", meta=(ClampMin="0.01")) float SharedAmpPulseSeconds = 5.f;
    /** Work limit per tick; surplus pulses are retained for following ticks. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AMP", meta=(ClampMin="1")) int32 MaxAmpPulsesPerTick = 8;
};

/** The existing buff system owns duration/removal. The same SourceID replaces only itself. */
USTRUCT(BlueprintType)
struct CHAPTER1REDITUS_API FReditusATBTimingEffect
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Timing") FName SourceID = NAME_None;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Timing", meta=(ClampMin="0.0")) float GaugeMultiplier = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Timing", meta=(ClampMin="0.0")) float PreparationMultiplier = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Timing") bool bPauseGauge = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Timing") bool bPausePreparation = false;
};

// Wrapper avoids a reflected nested container (TMap<Actor*, TArray<...>>).
USTRUCT()
struct CHAPTER1REDITUS_API FReditusATBTimingEffectList
{
    GENERATED_BODY()
    UPROPERTY() TArray<FReditusATBTimingEffect> Entries;
};

/** A queued command owns its own actor/targets; it never reads mutable controller globals. */
USTRUCT(BlueprintType)
struct CHAPTER1REDITUS_API FReditusATBAction
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Action") AActor* Unit = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Action") EReditusATBActionKind Kind = EReditusATBActionKind::Attack;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Action") FName ActionID = NAME_None;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Action") TSubclassOf<AActor> LegacyClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Action") TArray<AActor*> Targets;
    /** Optional context for AOE/group targeting, owned by the Blueprint action adapter. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Action") FName TargetMode = NAME_None;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Action", meta=(ClampMin="0")) int32 AmpCommitted = 0;
    /** Only an explicit Interrupt Pending Action call can use this. Damage crits do not. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interruption") bool bInterruptibleBeforeExecution = true;
    /** Requires a Blueprint cleanup handler before enabling; never releases the lane itself. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interruption") bool bInterruptibleDuringExecution = false;
    UPROPERTY(BlueprintReadOnly, Category="Action") int32 ActionToken = INDEX_NONE;
    UPROPERTY(BlueprintReadOnly, Category="Action") int32 CycleToken = INDEX_NONE;
};

USTRUCT(BlueprintType)
struct CHAPTER1REDITUS_API FReditusATBUnitView
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="ATB") AActor* Unit = nullptr;
    UPROPERTY(BlueprintReadOnly, Category="ATB") EReditusATBPhase Phase = EReditusATBPhase::Inactive;
    UPROPERTY(BlueprintReadOnly, Category="ATB") float Gauge = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="ATB") float PreparationRemaining = 0.f;
    /** Normalized preparation progress; remaining is work at 1x, not a wall-clock ETA. */
    UPROPERTY(BlueprintReadOnly, Category="ATB") float PreparationProgress = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="ATB") float GaugeMultiplier = 1.f;
    UPROPERTY(BlueprintReadOnly, Category="ATB") float PreparationMultiplier = 1.f;
    UPROPERTY(BlueprintReadOnly, Category="ATB") bool bGaugePaused = false;
    UPROPERTY(BlueprintReadOnly, Category="ATB") bool bPreparationPaused = false;
    UPROPERTY(BlueprintReadOnly, Category="ATB") int32 CycleToken = INDEX_NONE;
    UPROPERTY(BlueprintReadOnly, Category="ATB") int32 CompletedCycles = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FReditusATBCycleEvent,AActor*,Unit,int32,CycleToken);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FReditusATBActionEvent,FReditusATBAction,Action);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FReditusATBCancelEvent,FReditusATBAction,Action,bool,bHadStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FReditusATBAmpEvent,AActor*,Unit,int32,Amount);

/** Disabled until Start ATB is explicitly called. No constructor gameplay asset loads. */
UCLASS(ClassGroup=(Reditus),meta=(BlueprintSpawnableComponent))
class CHAPTER1REDITUS_API UReditusATBComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UReditusATBComponent();
    virtual ~UReditusATBComponent() override;
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Reditus|ATB") FReditusATBTuning Tuning;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Reditus|ATB|Rolling HP") FReditusRollingHPTuning RollingHP;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Reditus|ATB") bool bPaused = false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Reditus|ATB|Legacy") bool bReadLiveSpeed = true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Reditus|ATB|Legacy") FName SpeedProperty = TEXT("speed");
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Reditus|ATB|Legacy") bool bRemoveUnitsWithZeroHP = true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Reditus|ATB|Legacy") FName HPProperty = TEXT("currentHP");
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Reditus|ATB|Legacy") FName ExhaustedProperty = TEXT("IsExhausted");
    /** Hold staggered gauges. If disabled, stagger still forfeits commands and expires through skipped personal cycles. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Reditus|ATB|Legacy") bool bFreezeLegacyStagger = true;
    /** Advance BP_Staggered's existing turn counter/cleanup. Frozen units use a separate recovery clock. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Reditus|ATB|Legacy") bool bAdvanceLegacyStaggerOnCycleEnd = true;
    /** Reviewed Reditus start effects are synchronous. Disable if you add a latent operation to that handler itself. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Reditus|ATB|Legacy") bool bFinishSynchronousLegacyCycleStart = true;
    /** Single living-ally commands recheck their target at execution; revival keeps its dead-target rule. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Reditus|ATB|Targets") bool bRetargetLostLivingAlly = true;

    /** Run regen/poison/status checks once; acknowledge using Complete Cycle Start. */
    UPROPERTY(BlueprintAssignable,Category="Reditus|ATB") FReditusATBCycleEvent OnCycleStartRequested;
    UPROPERTY(BlueprintAssignable,Category="Reditus|ATB") FReditusATBCycleEvent OnUnitReady;
    /** Close/move a command menu if its READY owner is pushed back. */
    UPROPERTY(BlueprintAssignable,Category="Reditus|ATB") FReditusATBCycleEvent OnUnitReadinessLost;
    /** Validate targets/resources/status, then invoke the existing Blueprint action. */
    UPROPERTY(BlueprintAssignable,Category="Reditus|ATB") FReditusATBActionEvent OnExecuteActionRequested;
    /** Run owner-cycle cleanup once; acknowledge using Complete Cycle End. */
    UPROPERTY(BlueprintAssignable,Category="Reditus|ATB") FReditusATBCycleEvent OnCycleEndRequested;
    UPROPERTY(BlueprintAssignable,Category="Reditus|ATB") FReditusATBAmpEvent OnAmpGainRequested;
    UPROPERTY(BlueprintAssignable,Category="Reditus|ATB") FReditusATBCancelEvent OnActionCancelled;
    /** Stop animation/projectiles, then call Complete Action with this action's token. */
    UPROPERTY(BlueprintAssignable,Category="Reditus|ATB") FReditusATBActionEvent OnExecutionInterruptRequested;

    UFUNCTION(BlueprintCallable,Category="Reditus|ATB") bool RegisterUnit(AActor* Unit,float FallbackSpeed=20.f);
    UFUNCTION(BlueprintCallable,Category="Reditus|ATB") void UnregisterUnit(AActor* Unit);
    UFUNCTION(BlueprintCallable,Category="Reditus|ATB") bool StartATB();
    UFUNCTION(BlueprintCallable,Category="Reditus|ATB") void StopATB();
    UFUNCTION(BlueprintPure,Category="Reditus|ATB") bool IsATBRunning() const { return bRunning; }
    UFUNCTION(BlueprintCallable,Category="Reditus|ATB") void SetUnitSpeedOverride(AActor* Unit,float Speed);
    UFUNCTION(BlueprintCallable,Category="Reditus|ATB") void ClearUnitSpeedOverride(AActor* Unit);
    UFUNCTION(BlueprintCallable,Category="Reditus|ATB|Timing") bool AddOrUpdateTimingEffect(AActor* Unit,const FReditusATBTimingEffect& Effect);
    UFUNCTION(BlueprintCallable,Category="Reditus|ATB|Timing") bool RemoveTimingEffect(AActor* Unit,FName SourceID);
    UFUNCTION(BlueprintPure,Category="Reditus|ATB|Timing") TArray<FReditusATBTimingEffect> GetTimingEffects(AActor* Unit) const;
    /** Fraction 0.25 removes 25 percentage points. Does not replay cycle-start effects. */
    UFUNCTION(BlueprintCallable,Category="Reditus|ATB|Timing") bool PushGaugeBack(AActor* Unit,float Fraction);
    UFUNCTION(BlueprintCallable,Category="Reditus|ATB|Timing") bool InterruptPendingAction(AActor* Unit,float GaugePushback=0.25f);
    UFUNCTION(BlueprintCallable,Category="Reditus|ATB|Timing") bool RequestExecutionInterruption(AActor* Unit);
    UFUNCTION(BlueprintPure,Category="Reditus|ATB") FReditusATBUnitView GetUnitState(AActor* Unit) const;
    UFUNCTION(BlueprintPure,Category="Reditus|ATB") TArray<FReditusATBUnitView> GetUnitStates() const;
    /** Read intent for an enemy timeline without reading currentSkill/controller globals. */
    UFUNCTION(BlueprintPure,Category="Reditus|ATB") bool GetPendingAction(AActor* Unit,FReditusATBAction& Action) const;
    UFUNCTION(BlueprintPure,Category="Reditus|ATB") bool IsUnitStaggered(AActor* Unit) const;
    /** Checks live status as well as phase, including status applied between a tick and an input press. */
    UFUNCTION(BlueprintPure,Category="Reditus|ATB") bool CanIssueCommands(AActor* Unit) const;
    UFUNCTION(BlueprintCallable,Category="Reditus|ATB") bool CompleteCycleStart(AActor* Unit,int32 CycleToken,bool bCanAct);
    /** Override -1 uses the editable tuning/ID/class lookup. Does not spend resources. */
    UFUNCTION(BlueprintCallable,Category="Reditus|ATB") bool SubmitAction(const FReditusATBAction& Action,int32& ActionToken,float PreparationOverride=-1.f);
    UFUNCTION(BlueprintCallable,Category="Reditus|ATB") bool CompleteAction(int32 ActionToken);
    /** Validation failed before execution; restores READY, or ends the cycle if stagger was applied. */
    UFUNCTION(BlueprintCallable,Category="Reditus|ATB") bool RejectAction(int32 ActionToken);
    UFUNCTION(BlueprintCallable,Category="Reditus|ATB") bool CancelPendingAction(AActor* Unit);
    UFUNCTION(BlueprintCallable,Category="Reditus|ATB") bool CompleteCycleEnd(AActor* Unit,int32 CycleToken);
    UFUNCTION(BlueprintPure,Category="Reditus|ATB") float GetPreparationTime(const FReditusATBAction& Action) const;

private:
    TUniquePtr<ReditusATBCore::Scheduler> Scheduler;
    UPROPERTY(Transient) TMap<AActor*,int32> UnitIDs;
    UPROPERTY(Transient) TMap<AActor*,float> SpeedOverrides;
    UPROPERTY(Transient) TMap<AActor*,FReditusATBTimingEffectList> TimingEffects;
    UPROPERTY(Transient) TMap<int32,FReditusATBAction> PendingActions;
    UPROPERTY(Transient) TArray<TWeakObjectPtr<UReditusRollingHPComponent>> RollingUnits;
    TSet<int32> ExecutionInterruptRequests;
    bool bRunning = false;
    bool bStopping = false;
    int32 NextUnitID = 1;
    uint64 ClockEpoch = 0;
    double PulseElapsed = 0.0;
    AActor* ResolveActor(int32 UnitID) const;
    bool UnitAlive(AActor* Unit) const;
    void RequestAmp(AActor* Unit);
    void UpdateTimingRates(AActor* Unit,int32 UnitID);
    void NotifyReady(AActor* Unit,int32 CycleToken);
    void NotifyCycleStartCompleted(AActor* Unit,int32 CycleToken,bool bFirst,bool bCanAct);
    void SynchronizeLegacyStagger();
    void AdvanceFrozenStaggerRecovery();
    void FinishSynchronousCycleStart(AActor* Unit,int32 CycleToken);
    bool RevalidateActionTargets(FReditusATBAction& Action) const;
};
