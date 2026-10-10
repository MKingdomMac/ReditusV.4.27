#pragma once

#include "CoreMinimal.h"
#include "ReditusTurnTypes.generated.h"

class AActor;

/**
 * Why the authoritative turn timeline changed.
 *
 * The UI should use this only to choose presentation/animation.
 * The NewTimeline snapshot is always the source of truth.
 */
UENUM(BlueprintType)
enum class EReditusTurnChangeReason : uint8
{
    None                UMETA(DisplayName = "None"),
    InitialSync         UMETA(DisplayName = "Initial Sync"),
    LegacySync          UMETA(DisplayName = "Legacy Sync"),
    TurnStarted         UMETA(DisplayName = "Turn Started"),
    TurnEnded           UMETA(DisplayName = "Turn Ended"),
    Delay               UMETA(DisplayName = "Delay"),
    Advance             UMETA(DisplayName = "Advance"),
    Swap                UMETA(DisplayName = "Swap"),
    Redeploy            UMETA(DisplayName = "Redeploy"),
    ForceEndOfRound     UMETA(DisplayName = "Force End Of Round"),
    ExtraTurnInserted   UMETA(DisplayName = "Extra Turn Inserted"),
    UnitRemoved         UMETA(DisplayName = "Unit Removed"),
    TurnBlocked         UMETA(DisplayName = "Turn Blocked"),
    Custom              UMETA(DisplayName = "Custom")
};

/**
 * Reasons a unit can be unable to act when its turn arrives.
 *
 * IMPORTANT:
 * Turn delay/reordering is NOT a turn block. A delayed unit still acts later.
 */
UENUM(BlueprintType)
enum class EReditusTurnBlockReason : uint8
{
    None        UMETA(DisplayName = "None"),
    Staggered   UMETA(DisplayName = "Staggered"),
    Sleeping    UMETA(DisplayName = "Sleeping"),
    Petrified   UMETA(DisplayName = "Petrified"),
    Stunned     UMETA(DisplayName = "Stunned"),
    Paralyzed   UMETA(DisplayName = "Paralyzed"),
    Frozen      UMETA(DisplayName = "Frozen"),
    CustomStatus UMETA(DisplayName = "Custom Status")
};

UENUM(BlueprintType)
enum class EReditusBeginTurnResult : uint8
{
    NoTurnAvailable UMETA(DisplayName = "No Turn Available"),
    TurnStarted     UMETA(DisplayName = "Turn Started"),
    TurnBlocked     UMETA(DisplayName = "Turn Blocked")
};

/**
 * One unresolved action slot on the battle timeline.
 *
 * EntryID identifies the SLOT, not just the character. This matters because a
 * character can eventually have more than one future action (AMP/extra turns).
 */
USTRUCT(BlueprintType)
struct CHAPTER1REDITUS_API FReditusTurnEntry
{
    GENERATED_BODY()

public:

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reditus|Battle|Turn")
    int32 EntryID = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reditus|Battle|Turn")
    AActor* Unit = nullptr;

    /** 0 = current round, 1 = next round, etc. Relative/absolute use is up to the session. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reditus|Battle|Turn")
    int32 RoundIndex = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reditus|Battle|Turn")
    bool bIsExtraTurn = false;

    /** Optional source such as TimedAttack, AMP, SkillName, Overdrive, etc. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reditus|Battle|Turn")
    FName SourceID = NAME_None;
};

USTRUCT(BlueprintType)
struct CHAPTER1REDITUS_API FReditusTurnEligibility
{
    GENERATED_BODY()

public:

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reditus|Battle|Turn")
    bool bCanAct = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reditus|Battle|Turn")
    EReditusTurnBlockReason BlockReason = EReditusTurnBlockReason::None;

    /** Stable source/status ID, e.g. Stagger, Sleep, Petrify. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reditus|Battle|Turn")
    FName BlockingSourceID = NAME_None;

    /** Presentation text such as STAGGERED, ASLEEP, PETRIFIED. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reditus|Battle|Turn")
    FText DisplayText;
};

/** One active source that currently prevents a unit from acting. */
USTRUCT(BlueprintType)
struct CHAPTER1REDITUS_API FReditusTurnBlocker
{
    GENERATED_BODY()

public:

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reditus|Battle|Turn")
    FName SourceID = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reditus|Battle|Turn")
    EReditusTurnBlockReason Reason = EReditusTurnBlockReason::CustomStatus;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reditus|Battle|Turn")
    FText DisplayText;

    /** Higher priority wins when several blockers are active at once. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reditus|Battle|Turn")
    int32 Priority = 0;
};

/**
 * Complete read-only view used by UI/presentation.
 * CurrentUnit is displayed separately from Entries, matching the UI reference
 * you showed (current action + upcoming timeline).
 */
USTRUCT(BlueprintType)
struct CHAPTER1REDITUS_API FReditusTurnTimelineSnapshot
{
    GENERATED_BODY()

public:

    UPROPERTY(BlueprintReadOnly, Category = "Reditus|Battle|Turn")
    AActor* CurrentUnit = nullptr;

    UPROPERTY(BlueprintReadOnly, Category = "Reditus|Battle|Turn")
    TArray<FReditusTurnEntry> Entries;

    UPROPERTY(BlueprintReadOnly, Category = "Reditus|Battle|Turn")
    int32 CurrentRoundIndex = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Reditus|Battle|Turn")
    int32 Revision = 0;
};

/** Metadata describing the mutation that produced a new timeline. */
USTRUCT(BlueprintType)
struct CHAPTER1REDITUS_API FReditusTurnChangeInfo
{
    GENERATED_BODY()

public:

    UPROPERTY(BlueprintReadOnly, Category = "Reditus|Battle|Turn")
    EReditusTurnChangeReason Reason = EReditusTurnChangeReason::None;

    UPROPERTY(BlueprintReadOnly, Category = "Reditus|Battle|Turn")
    AActor* AffectedUnit = nullptr;

    UPROPERTY(BlueprintReadOnly, Category = "Reditus|Battle|Turn")
    AActor* SecondaryUnit = nullptr;

    UPROPERTY(BlueprintReadOnly, Category = "Reditus|Battle|Turn")
    int32 PreviousIndex = INDEX_NONE;

    UPROPERTY(BlueprintReadOnly, Category = "Reditus|Battle|Turn")
    int32 NewIndex = INDEX_NONE;

    UPROPERTY(BlueprintReadOnly, Category = "Reditus|Battle|Turn")
    int32 Amount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Reditus|Battle|Turn")
    FName SourceID = NAME_None;
};

/** Old + new snapshots are supplied so BP_BattleUI can animate the difference. */
USTRUCT(BlueprintType)
struct CHAPTER1REDITUS_API FReditusTurnTimelineUpdate
{
    GENERATED_BODY()

public:

    UPROPERTY(BlueprintReadOnly, Category = "Reditus|Battle|Turn")
    FReditusTurnTimelineSnapshot OldTimeline;

    UPROPERTY(BlueprintReadOnly, Category = "Reditus|Battle|Turn")
    FReditusTurnTimelineSnapshot NewTimeline;

    UPROPERTY(BlueprintReadOnly, Category = "Reditus|Battle|Turn")
    FReditusTurnChangeInfo Change;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FReditusTurnTimelineChangedSignature,
    FReditusTurnTimelineUpdate,
    Update
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FReditusCurrentTurnChangedSignature,
    AActor*,
    PreviousUnit,
    AActor*,
    NewUnit
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FReditusUnitTurnBlockedSignature,
    AActor*,
    Unit,
    FReditusTurnEligibility,
    Eligibility
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FReditusTurnEligibilityChangedSignature,
    FReditusTurnEligibility,
    Eligibility
);
