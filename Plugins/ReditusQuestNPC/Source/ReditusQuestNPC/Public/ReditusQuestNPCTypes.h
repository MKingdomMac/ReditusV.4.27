#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Dialogue.h"
#include "ReditusQuestNPCTypes.generated.h"

class AActor;
class UPaperFlipbook;
class UDialogue;
class UDialogueUserWidget;
class UTexture2D;
class ULevelSequence;

UENUM(BlueprintType)
enum class EReditusNPCBattleResult : uint8 { Won, Lost, Escaped, Cancelled };

USTRUCT(BlueprintType)
struct REDITUSQUESTNPC_API FReditusNPCBattleFollowUp
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Follow Up", meta=(DisplayName="Post Sequence"))
    TSoftObjectPtr<ULevelSequence> Sequence;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Follow Up", meta=(AdvancedDisplay))
    FName PlacedSequenceActorTag;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Follow Up", meta=(DisplayName="Post Dialogue"))
    UDialogue* Dialogue = nullptr;
    // Optional automatic continuation after both presentation steps finish.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Follow Up")
    FName NextEncounterID;
    bool HasWork() const { return !Sequence.IsNull() || !PlacedSequenceActorTag.IsNone() || Dialogue || !NextEncounterID.IsNone(); }
};

USTRUCT(BlueprintType)
struct REDITUSQUESTNPC_API FReditusNPCBattleFollowUpRule
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Follow Up") FName EncounterID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Follow Up") EReditusNPCBattleResult Result = EReditusNPCBattleResult::Won;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Follow Up") FReditusNPCBattleFollowUp FollowUp;
};

UENUM(BlueprintType)
enum class EReditusQuestState : uint8
{
    Unknown, Unavailable, Available, Active, ReadyToDeliver, Completed, Failed
};

UENUM(BlueprintType)
enum class EReditusQuestMarkerKind : uint8
{
    FromQuest, MainQuest, SideQuest
};

UENUM(BlueprintType)
enum class EReditusQuestAction : uint8
{
    StartQuest, AdvanceQuest UMETA(Hidden), TurnInQuest, ReportObjectiveEvent, RetryFailedQuest, RecordChoice, RequestStepAction
};

UENUM(BlueprintType)
enum class EReditusQuestNPCDialogueMode : uint8
{
    DialogueAssets,
    FetchQuestText UMETA(DisplayName="Simple Quest Text", ToolTip="Shared text flow for single-stage item and monster-hunt quests, optionally with a Deadline timer.")
};

USTRUCT(BlueprintType)
struct REDITUSQUESTNPC_API FReditusFetchQuestText
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest Text", meta=(ToolTip="Name shown above this NPC's dialogue. Localizable Text; leave empty to use the NPC Display Name."))
    FText SpeakerName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest Text", meta=(MultiLine=true))
    FText Offer = NSLOCTEXT("ReditusFetch", "Offer", "Could you bring me the supplies I need?");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest Text", meta=(MultiLine=true))
    FText Reminder = NSLOCTEXT("ReditusFetch", "Reminder", "Please come back when you have the supplies.");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest Text", meta=(MultiLine=true, DisplayName="Ready to Deliver"))
    FText HandIn = NSLOCTEXT("ReditusFetch", "HandIn", "You have what I need. Would you like to hand it over?");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest Text", meta=(MultiLine=true))
    FText Completed = NSLOCTEXT("ReditusFetch", "Completed", "Thank you for your help.");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest Text", meta=(AdvancedDisplay))
    FText AcceptChoice = NSLOCTEXT("ReditusFetch", "AcceptChoice", "I'll help.");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest Text", meta=(AdvancedDisplay))
    FText HandInChoice = NSLOCTEXT("ReditusFetch", "HandInChoice", "Here you go.");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest Text", meta=(AdvancedDisplay))
    FText NotNowChoice = NSLOCTEXT("ReditusFetch", "NotNowChoice", "Not now.");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest Text", meta=(AdvancedDisplay, MultiLine=true))
    FText Declined = NSLOCTEXT("ReditusFetch", "Declined", "All right. Speak to me when you're ready.");
};

USTRUCT(BlueprintType)
struct REDITUSQUESTNPC_API FReditusQuestSnapshot
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="Quest")
    EReditusQuestState State = EReditusQuestState::Unknown;

    UPROPERTY(BlueprintReadOnly, Category="Quest")
    int32 Progress = 0;

    UPROPERTY(BlueprintReadOnly, Category="Quest")
    int32 Goal = 0;

    UPROPERTY(BlueprintReadOnly, Category="Quest")
    FName StageID;

    UPROPERTY(BlueprintReadOnly, Category="Quest")
    bool IsMainQuest = false;
};

USTRUCT(BlueprintType)
struct REDITUSQUESTNPC_API FReditusNPCDialogueRule
{
    GENERATED_BODY()

    // Optional override: otherwise use the NPC definition's quest.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dialogue Rule")
    FDataTableRowHandle QuestRow;

    UPROPERTY(BlueprintReadWrite, Category="Retired Quest System", meta=(DeprecatedProperty, DeprecationMessage="Use QuestRow. Legacy quest classes are ignored."))
    TSubclassOf<AActor> Quest;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dialogue Rule")
    EReditusQuestState RequiredState = EReditusQuestState::Active;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dialogue Rule")
    bool AnyKnownState = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dialogue Rule", meta=(ClampMin="0"))
    int32 MinimumProgress = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dialogue Rule") FName RequiredStageID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dialogue Rule") FName RequiredChoiceID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dialogue Rule") FName RequiredOptionID;

    // -1 means no upper limit. 0..1 covers the first stage, 1..2 the second, etc.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dialogue Rule")
    int32 MaximumProgressExclusive = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dialogue Rule")
    UDialogue* Dialogue = nullptr;
};

USTRUCT(BlueprintType)
struct REDITUSQUESTNPC_API FReditusQuestNPCDefinition : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Identity")
    FText DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Identity")
    FText InteractionText = NSLOCTEXT("ReditusQuestNPC", "Talk", "Talk");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Appearance")
    UPaperFlipbook* IdleFlipbook = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Appearance")
    UPaperFlipbook* NorthFlipbook = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Appearance")
    UPaperFlipbook* EastFlipbook = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Appearance")
    UPaperFlipbook* WestFlipbook = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Appearance")
    bool FacePlayerWhenTalking = true;

    // Routed to the existing game's encounter setup through one central handler.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle")
    FName EncounterID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle")
    bool RemoveNPCOnBattleVictory = false;

    // Default victory follow-up. Both presentation fields are optional.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle", meta=(DisplayName="Post Battle Follow Up"))
    FReditusNPCBattleFollowUp PostBattleFollowUp;

    // Exact encounter/result overrides allow different dialogue/scenes for each round.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle")
    TArray<FReditusNPCBattleFollowUpRule> EncounterFollowUps;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Recruitment")
    FDataTableRowHandle RecruitedCharacter;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Recruitment", meta=(ClampMin="1"))
    int32 RecruitmentStartingLevel = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Recruitment")
    bool AddRecruitToActivePartyIfSpace = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Recruitment")
    bool RemoveNPCOnRecruitment = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest", meta=(DisplayName="Quest", ToolTip="Select a quest row from DT_Quests or DT_QuestCatalog."))
    FDataTableRowHandle QuestRow;

    UPROPERTY(BlueprintReadWrite, Category="Retired Quest System", meta=(DeprecatedProperty, DeprecationMessage="Use QuestRow. Legacy quest classes are ignored."))
    TSubclassOf<AActor> Quest;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dialogue")
    EReditusQuestNPCDialogueMode DialogueMode = EReditusQuestNPCDialogueMode::DialogueAssets;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dialogue", meta=(DisplayName="Quest Text", EditCondition="DialogueMode == EReditusQuestNPCDialogueMode::FetchQuestText", EditConditionHides))
    FReditusFetchQuestText FetchQuestText;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dialogue", meta=(AdvancedDisplay, DisplayName="Portrait Speaker Type", ToolTip="Optional existing portrait preset. Set the written name under Quest Text > Speaker Name.", EditCondition="DialogueMode == EReditusQuestNPCDialogueMode::FetchQuestText", EditConditionHides))
    EDialogueNodeType FetchSpeakerType = EDialogueNodeType::DNT_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dialogue", meta=(EditCondition="DialogueMode == EReditusQuestNPCDialogueMode::DialogueAssets", EditConditionHides))
    UDialogue* AvailableDialogue = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dialogue", meta=(EditCondition="DialogueMode == EReditusQuestNPCDialogueMode::DialogueAssets", EditConditionHides))
    UDialogue* ActiveDialogue = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dialogue", meta=(EditCondition="DialogueMode == EReditusQuestNPCDialogueMode::DialogueAssets", EditConditionHides))
    UDialogue* ReadyToDeliverDialogue = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dialogue", meta=(EditCondition="DialogueMode == EReditusQuestNPCDialogueMode::DialogueAssets", EditConditionHides))
    UDialogue* CompletedDialogue = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dialogue", meta=(EditCondition="DialogueMode == EReditusQuestNPCDialogueMode::DialogueAssets", EditConditionHides))
    UDialogue* FailedDialogue = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dialogue", meta=(EditCondition="DialogueMode == EReditusQuestNPCDialogueMode::DialogueAssets", EditConditionHides))
    UDialogue* UnavailableDialogue = nullptr;

    // Also supports a plain talking NPC with no quest.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dialogue", meta=(EditCondition="DialogueMode == EReditusQuestNPCDialogueMode::DialogueAssets", EditConditionHides))
    UDialogue* FallbackDialogue = nullptr;

    // Checked in array order before the simple state slots above.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dialogue", meta=(EditCondition="DialogueMode == EReditusQuestNPCDialogueMode::DialogueAssets", EditConditionHides))
    TArray<FReditusNPCDialogueRule> ExtraRules;

    // Defaults to the project's existing DemoDialogueWidget in the generated Blueprint.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dialogue")
    TSubclassOf<UDialogueUserWidget> DialogueWidgetClass;

    UPROPERTY(BlueprintReadWrite, Category="Retired Marker Settings", meta=(DeprecatedProperty, DeprecationMessage="Ignored: quest type/visibility belong to DT_Quests; appearance belongs to MinimapPlugin."))
    EReditusQuestMarkerKind MarkerKind = EReditusQuestMarkerKind::FromQuest;

    UPROPERTY(BlueprintReadWrite, Category="Retired Marker Settings", meta=(DeprecatedProperty, DeprecationMessage="Ignored: quest type/visibility belong to DT_Quests; appearance belongs to MinimapPlugin."))
    bool ShowAvailableQuestMarker = true;

    UPROPERTY(BlueprintReadWrite, Category="Retired Marker Settings", meta=(DeprecatedProperty, DeprecationMessage="Ignored: quest type/visibility belong to DT_Quests; appearance belongs to MinimapPlugin."))
    bool ShowNPCMarkerWhileQuestActive = false;

    UPROPERTY(BlueprintReadWrite, Category="Retired Marker Settings", meta=(DeprecatedProperty, DeprecationMessage="Ignored: quest type/visibility belong to DT_Quests; appearance belongs to MinimapPlugin."))
    bool ShowTurnInMarker = true;

    UPROPERTY(BlueprintReadWrite, Category="Retired Marker Settings", meta=(DeprecatedProperty, DeprecationMessage="Ignored: quest type/visibility belong to DT_Quests; appearance belongs to MinimapPlugin."))
    UTexture2D* CustomMarkerIcon = nullptr;
};

USTRUCT(BlueprintType)
struct REDITUSQUESTNPC_API FReditusQuestObjectiveDefinition : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective", meta=(DisplayName="Quest", ToolTip="Select a quest row from DT_Quests or DT_QuestCatalog."))
    FDataTableRowHandle QuestRow;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective")
    FName StageID;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective")
    FName ObjectiveID;

    UPROPERTY(BlueprintReadWrite, Category="Retired Quest System", meta=(DeprecatedProperty, DeprecationMessage="Use QuestRow. Legacy quest classes are ignored."))
    TSubclassOf<AActor> Quest;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective")
    FText Label;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective", meta=(ClampMin="0"))
    int32 MinimumProgress = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective")
    int32 MaximumProgressExclusive = -1;

    UPROPERTY(BlueprintReadWrite, Category="Retired Marker Settings", meta=(DeprecatedProperty, DeprecationMessage="Ignored: quest type/visibility belong to DT_Quests; appearance belongs to MinimapPlugin."))
    EReditusQuestMarkerKind MarkerKind = EReditusQuestMarkerKind::FromQuest;

    UPROPERTY(BlueprintReadWrite, Category="Retired Marker Settings", meta=(DeprecatedProperty, DeprecationMessage="Ignored: quest type/visibility belong to DT_Quests; appearance belongs to MinimapPlugin."))
    bool ShowEdgeArrow = true;

    UPROPERTY(BlueprintReadWrite, Category="Retired Marker Settings", meta=(DeprecatedProperty, DeprecationMessage="Ignored: quest type/visibility belong to DT_Quests; appearance belongs to MinimapPlugin."))
    UTexture2D* CustomMarkerIcon = nullptr;
};
