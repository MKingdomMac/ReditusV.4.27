#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ReditusQuestNPCTypes.h"
#include "ReditusQuestSystemTypes.generated.h"

class ULevelSequence;
class APlayerController;
class UWorld;

UENUM(BlueprintType)
enum class EReditusQuestObjectiveKind : uint8
{
    Talk, CollectItem, DefeatEnemy, ReachLocation, RecruitUnit, Interact, Custom, TimerElapsed, WinBattle, Choice, StepAction
};

UENUM(BlueprintType)
enum class EReditusNPCRemovalReason : uint8 { Removed, Defeated, Recruited };

USTRUCT(BlueprintType)
struct REDITUSQUESTNPC_API FReditusQuestRemovedNPC
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite, SaveGame, Category="NPC Save") FName NPCID;
    UPROPERTY(BlueprintReadWrite, SaveGame, Category="NPC Save") EReditusNPCRemovalReason Reason = EReditusNPCRemovalReason::Removed;
};

USTRUCT(BlueprintType)
struct REDITUSQUESTNPC_API FReditusNPCBattleRequest
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="NPC Battle") FGuid RequestID;
    UPROPERTY(BlueprintReadOnly, Category="NPC Battle") FName NPCID;
    UPROPERTY(BlueprintReadOnly, Category="NPC Battle") FName EncounterID;
    UPROPERTY(BlueprintReadOnly, Category="NPC Battle") bool RemoveNPCOnVictory = false;
    UPROPERTY(BlueprintReadOnly, Category="NPC Battle") APlayerController* Player = nullptr;
    UPROPERTY(Transient) TWeakObjectPtr<UWorld> RequestWorld;
};

UENUM(BlueprintType)
enum class EReditusQuestItemKind : uint8 { Usable, Misc, KeyItem, Equipment };

UENUM(BlueprintType)
enum class EReditusQuestNoticeKind : uint8 { Added, Updated, Completed, Failed };

UENUM(BlueprintType)
enum class EReditusQuestTimerMode : uint8 { None, Wait, Deadline };

UENUM(BlueprintType)
enum class EReditusQuestMarkerVisibility : uint8 { InheritQuest, Show, Hide };

USTRUCT(BlueprintType)
struct REDITUSQUESTNPC_API FReditusQuestMarkerRules
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest Markers") bool ShowWhenAvailable = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest Markers") bool ShowWhileActive = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest Markers") bool ShowWhenReadyToTurnIn = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest Markers") bool ShowOnMinimap = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest Markers") bool ShowOverhead = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest Markers") bool ShowObjectiveDestinations = true;
};

USTRUCT(BlueprintType)
struct REDITUSQUESTNPC_API FReditusQuestMarkerState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Quest Markers") bool Visible = false;
    UPROPERTY(BlueprintReadOnly, Category="Quest Markers") bool MinimapVisible = false;
    UPROPERTY(BlueprintReadOnly, Category="Quest Markers") bool OverheadVisible = false;
    UPROPERTY(BlueprintReadOnly, Category="Quest Markers") bool IsMainQuest = false;
    UPROPERTY(BlueprintReadOnly, Category="Quest Markers") FText Title;
};

USTRUCT(BlueprintType)
struct REDITUSQUESTNPC_API FReditusQuestSequence
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sequence") TSoftObjectPtr<ULevelSequence> Sequence;
    // Optional placed LevelSequenceActor tag, for sequences with level-specific actor bindings.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sequence") FName PlacedActorTag;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sequence") bool PauseQuestTimers = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sequence") bool DisableMovementAndLook = true;
    // Natural completion reports a Custom objective event with this TargetID.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sequence") FName CompletionTargetID;
    // Keeps post-victory cinematics behind the combat/results UI.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sequence") bool WaitForExploration = true;
    // Internal correlation only; never match follow-ups merely by shared sequence asset.
    UPROPERTY(Transient) FGuid NPCFollowUpRequestID;
};

USTRUCT(BlueprintType)
struct REDITUSQUESTNPC_API FReditusQuestTimerDefinition
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Timer") EReditusQuestTimerMode Mode = EReditusQuestTimerMode::None;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Timer", meta=(ClampMin="0.01")) float DurationSeconds = 60.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Timer") bool CountUp = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Timer") bool ShowOnScreen = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Timer") FText Label;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Timer") TArray<FReditusQuestSequence> OnTimeoutSequences;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Timer")
    TArray<FDataTableRowHandle> StartQuestsOnTimeout;
};

USTRUCT(BlueprintType)
struct REDITUSQUESTNPC_API FReditusQuestTimerStatus
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Timer") FDataTableRowHandle Quest;
    UPROPERTY(BlueprintReadOnly, Category="Timer") FName StageID;
    UPROPERTY(BlueprintReadOnly, Category="Timer") FText Label;
    UPROPERTY(BlueprintReadOnly, Category="Timer") float RemainingSeconds = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="Timer") float ElapsedSeconds = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="Timer") bool CountUp = false;
    UPROPERTY(BlueprintReadOnly, Category="Timer") bool Paused = false;
    UPROPERTY(BlueprintReadOnly, Category="Timer") FString DisplayTime;
};

USTRUCT(BlueprintType)
struct REDITUSQUESTNPC_API FReditusQuestNotification
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Quest Notification") EReditusQuestNoticeKind Kind = EReditusQuestNoticeKind::Updated;
    UPROPERTY(BlueprintReadOnly, Category="Quest Notification") FDataTableRowHandle Quest;
    UPROPERTY(BlueprintReadOnly, Category="Quest Notification") FText Title;
    UPROPERTY(BlueprintReadOnly, Category="Quest Notification") FText Detail;
};

USTRUCT(BlueprintType)
struct REDITUSQUESTNPC_API FReditusQuestItemAmount
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Item") FDataTableRowHandle ItemRow;
    // Delivery output/serialized compatibility. Derived from ItemRow; never authored separately.
    UPROPERTY(BlueprintReadWrite, SaveGame, Category="Item") EReditusQuestItemKind Kind = EReditusQuestItemKind::Misc;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Item", meta=(ClampMin="1")) int32 Amount = 1;
};

USTRUCT(BlueprintType)
struct REDITUSQUESTNPC_API FReditusQuestRewards
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Rewards", meta=(ClampMin="0")) int32 Gold = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Rewards", meta=(ClampMin="0")) int32 Experience = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Rewards") TArray<FReditusQuestItemAmount> Items;
};

// Stable choice/option IDs are stored; integer values come only from authored rows.
USTRUCT(BlueprintType)
struct REDITUSQUESTNPC_API FReditusQuestChoiceOption
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Choice") FName OptionID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Choice") FText Label;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Choice") int32 Value = 0;
};
USTRUCT(BlueprintType)
struct REDITUSQUESTNPC_API FReditusQuestChoiceDefinition
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Choice") FName ChoiceID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Choice") FText Description;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Choice") TArray<FReditusQuestChoiceOption> Options;
};
USTRUCT(BlueprintType)
struct REDITUSQUESTNPC_API FReditusQuestSavedChoice
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite, SaveGame, Category="Choice") FName ChoiceID;
    UPROPERTY(BlueprintReadWrite, SaveGame, Category="Choice") FName OptionID;
    UPROPERTY(BlueprintReadWrite, SaveGame, Category="Choice") int32 Value = 0;
};
USTRUCT(BlueprintType)
struct REDITUSQUESTNPC_API FReditusQuestStageRoute
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Branch") FName ChoiceID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Branch") FName OptionID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Branch") FName NextStageID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Branch") bool Finish = false;
};
USTRUCT(BlueprintType)
struct REDITUSQUESTNPC_API FReditusQuestStepAction
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Step Action") FName ActionID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Step Action") TArray<FReditusQuestItemAmount> ItemsToRemove;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Step Action") FReditusQuestRewards Rewards;
    // Optional: use a previously selected option's Value as gold, instead of Rewards.Gold.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Step Action") FName GoldRewardChoiceID;
};

USTRUCT(BlueprintType)
struct REDITUSQUESTNPC_API FReditusQuestTaskDefinition
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective") FName ObjectiveID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective") FText Description;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective") EReditusQuestObjectiveKind Kind = EReditusQuestObjectiveKind::Talk;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective") FName TargetID;
    // CollectItem objectives use this full item row handle, not TargetID.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Item Objective") FDataTableRowHandle ItemRow;
    // Retained for old assets/pins only. Runtime routing derives from ItemRow.
    UPROPERTY(BlueprintReadWrite, Category="Legacy Item Routing") EReditusQuestItemKind ItemKind = EReditusQuestItemKind::Misc;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective", meta=(ClampMin="1")) int32 RequiredCount = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective") bool Optional = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective")
    TArray<FDataTableRowHandle> StartQuestsOnCompletion;
};

USTRUCT(BlueprintType)
struct REDITUSQUESTNPC_API FReditusQuestStageDefinition
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage") FName StageID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage") FText Description;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage") TArray<FReditusQuestTaskDefinition> Objectives;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage") FReditusQuestTimerDefinition Timer;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Choices") TArray<FReditusQuestChoiceDefinition> Choices;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Step Actions") TArray<FReditusQuestStepAction> Actions;
    // First matching route wins. Otherwise use NextStageID or the next array entry.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Branches") TArray<FReditusQuestStageRoute> Routes;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Branches") FName NextStageID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Branches") bool FinishAfterStage = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest Markers")
    EReditusQuestMarkerVisibility NPCMarkerVisibility = EReditusQuestMarkerVisibility::InheritQuest;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cinematics") TArray<FReditusQuestSequence> OnEnterSequences;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cinematics") TArray<FReditusQuestSequence> OnCompleteSequences;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    TArray<FDataTableRowHandle> StartQuestsOnCompletion;
};

USTRUCT(BlueprintType)
struct REDITUSQUESTNPC_API FReditusQuestDefinition : public FTableRowBase
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest") FText Title;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest", meta=(MultiLine="true")) FText Description;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest") bool IsMainQuest = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest Markers") FReditusQuestMarkerRules Markers;
    // Increment when changing stage/objective structure after publishing saves.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest", meta=(ClampMin="1")) int32 DefinitionVersion = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Availability")
    TArray<FDataTableRowHandle> RequiredCompletedQuests;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest") TArray<FReditusQuestStageDefinition> Stages;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Turn In") TArray<FReditusQuestItemAmount> TurnInItems;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rewards") FReditusQuestRewards Rewards;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cinematics") TArray<FReditusQuestSequence> OnCompleteSequences;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Follow Up")
    TArray<FDataTableRowHandle> StartQuestsOnCompletion;
};

USTRUCT(BlueprintType)
struct REDITUSQUESTNPC_API FReditusQuestObjectiveStatus
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Objective") FName StageID;
    UPROPERTY(BlueprintReadOnly, Category="Objective") FReditusQuestTaskDefinition Objective;
    UPROPERTY(BlueprintReadOnly, Category="Objective") int32 Count = 0;
    UPROPERTY(BlueprintReadOnly, Category="Objective") bool Complete = false;
    UPROPERTY(BlueprintReadOnly, Category="Objective") bool CurrentStage = false;
};

UENUM(BlueprintType)
enum class EReditusQuestDeliveryKind : uint8 { TurnIn, StepAction };

USTRUCT(BlueprintType)
struct REDITUSQUESTNPC_API FReditusQuestDelivery
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, SaveGame, Category="Delivery") FDataTableRowHandle Quest;
    UPROPERTY(BlueprintReadOnly, SaveGame, Category="Delivery") FGuid ReceiptID;
    UPROPERTY(BlueprintReadOnly, SaveGame, Category="Delivery") EReditusQuestDeliveryKind Kind = EReditusQuestDeliveryKind::TurnIn;
    UPROPERTY(BlueprintReadOnly, SaveGame, Category="Delivery") FName StageID;
    UPROPERTY(BlueprintReadOnly, SaveGame, Category="Delivery") FName ActionID;
    UPROPERTY(BlueprintReadOnly, SaveGame, Category="Delivery") TArray<FReditusQuestItemAmount> ItemsToRemove;
    UPROPERTY(BlueprintReadOnly, SaveGame, Category="Delivery") FReditusQuestRewards Rewards;
};

USTRUCT(BlueprintType)
struct REDITUSQUESTNPC_API FReditusQuestSavedCounter
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite, SaveGame, Category="Quest Save") FName StageID;
    UPROPERTY(BlueprintReadWrite, SaveGame, Category="Quest Save") FName ObjectiveID;
    UPROPERTY(BlueprintReadWrite, SaveGame, Category="Quest Save") int32 Count = 0;
};

USTRUCT(BlueprintType)
struct REDITUSQUESTNPC_API FReditusQuestSavedRecord
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite, SaveGame, Category="Quest Save") FDataTableRowHandle Quest;
    UPROPERTY(BlueprintReadWrite, SaveGame, Category="Quest Save") int32 DefinitionVersion = 1;
    UPROPERTY(BlueprintReadWrite, SaveGame, Category="Quest Save") EReditusQuestState State = EReditusQuestState::Active;
    UPROPERTY(BlueprintReadWrite, SaveGame, Category="Quest Save") FName StageID;
    UPROPERTY(BlueprintReadWrite, SaveGame, Category="Quest Save") float TimerRemainingSeconds = 0.f;
    UPROPERTY(BlueprintReadWrite, SaveGame, Category="Quest Save") TArray<FName> VisitedStages;
    UPROPERTY(BlueprintReadWrite, SaveGame, Category="Quest Save") TArray<FReditusQuestSavedChoice> Choices;
    UPROPERTY(BlueprintReadWrite, SaveGame, Category="Quest Save") TArray<FName> AppliedStepActions;
    UPROPERTY(BlueprintReadWrite, SaveGame, Category="Quest Save") TArray<FReditusQuestSavedCounter> Counters;
    UPROPERTY(BlueprintReadWrite, SaveGame, Category="Quest Save") TArray<FName> SeenEventIDs;
    UPROPERTY(BlueprintReadWrite, SaveGame, Category="Quest Save") TArray<FName> ReachedStages;
    UPROPERTY(BlueprintReadWrite, SaveGame, Category="Quest Save") TArray<FName> ReachedObjectives;
    UPROPERTY(BlueprintReadWrite, SaveGame, Category="Quest Save") bool HasPendingDelivery = false;
    UPROPERTY(BlueprintReadWrite, SaveGame, Category="Quest Save") FReditusQuestDelivery PendingDelivery;
};

// Store ONE variable of this type in the existing BP_JRPGSaveGame.
// This plugin does not create a second save slot or call SaveGameToSlot.
USTRUCT(BlueprintType)
struct REDITUSQUESTNPC_API FReditusQuestSaveData
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite, SaveGame, Category="Quest Save") int32 FormatVersion = 2;
    UPROPERTY(BlueprintReadWrite, SaveGame, Category="Quest Save") TArray<FReditusQuestSavedRecord> Quests;
    UPROPERTY(BlueprintReadWrite, SaveGame, Category="Quest Save") TArray<FReditusQuestRemovedNPC> RemovedNPCs;
    // Receipts only; actual roster/party data stays in the existing roster save.
    UPROPERTY(BlueprintReadWrite, SaveGame, Category="Quest Save") TArray<FName> RecordedRecruitments;
};
