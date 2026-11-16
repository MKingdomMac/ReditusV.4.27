#pragma once

#include "CoreMinimal.h"
#include "Dialogue.h"
#include "ReditusQuestNPCTypes.h"
#include "ReditusQuestSystemTypes.h"
#include "ReditusQuestDialogueEvents.generated.h"

// Pick this event on an existing dialogue node. Leave QuestRowOverride empty
// to use the NPC's selected quest row handle.
UCLASS(BlueprintType, EditInlineNew, meta=(DisplayName="Reditus Quest Action"))
class REDITUSQUESTNPC_API UReditusQuestActionEvent : public UDialogueEvents
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest Action")
    EReditusQuestAction Action = EReditusQuestAction::StartQuest;

    UPROPERTY(BlueprintReadWrite, Category="Retired Quest System")
    TSubclassOf<AActor> QuestOverride;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest Action", meta=(RowType="ReditusQuestDefinition"))
    FDataTableRowHandle QuestRowOverride;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective Event", meta=(EditCondition="Action == EReditusQuestAction::ReportObjectiveEvent", EditConditionHides))
    EReditusQuestObjectiveKind ObjectiveKind = EReditusQuestObjectiveKind::Talk;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective Event", meta=(EditCondition="Action == EReditusQuestAction::ReportObjectiveEvent", EditConditionHides))
    FName TargetID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective Event", meta=(EditCondition="Action == EReditusQuestAction::ReportObjectiveEvent", EditConditionHides))
    FName UniqueEventID;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective Event", meta=(ClampMin="1", DisplayName="Amount", EditCondition="Action == EReditusQuestAction::ReportObjectiveEvent", EditConditionHides))
    int32 Progress = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Saved Choice", meta=(EditCondition="Action == EReditusQuestAction::RecordChoice", EditConditionHides)) FName ChoiceID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Saved Choice", meta=(EditCondition="Action == EReditusQuestAction::RecordChoice", EditConditionHides)) FName OptionID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Step Action", meta=(EditCondition="Action == EReditusQuestAction::RequestStepAction", EditConditionHides)) FName StepActionID;

    UPROPERTY(BlueprintReadWrite, Category="Retired Quest System")
    bool ShowRewards = true;

    virtual void RecieveEventTriggered_Implementation(APlayerController* ConsideringPlayer, AActor* NPCActor) override;
};

UCLASS(BlueprintType, EditInlineNew, meta=(DisplayName="Reditus Quest State"))
class REDITUSQUESTNPC_API UReditusQuestStateCondition : public UDialogueConditions
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintReadWrite, Category="Retired Quest System")
    TSubclassOf<AActor> QuestOverride;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest Condition", meta=(RowType="ReditusQuestDefinition"))
    FDataTableRowHandle QuestRowOverride;

    // Optional additional stage requirement for Active dialogue branches.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest Condition")
    FName RequiredStageID;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest Condition")
    EReditusQuestState RequiredState = EReditusQuestState::Active;

    virtual bool IsConditionMet_Implementation(APlayerController* ConsideringPlayer, AActor* NPCActor) override;
};

UENUM(BlueprintType)
enum class EReditusNPCDialogueAction : uint8 { StartBattle, Recruit, RemovePermanently, FinishFollowUp };

// Explicit branch events. Merely closing a conversation never implies joining
// or fighting, and no recruitment enum from an old widget is guessed.
UCLASS(BlueprintType, EditInlineNew, meta=(DisplayName="Reditus NPC Action"))
class REDITUSQUESTNPC_API UReditusNPCActionEvent : public UDialogueEvents
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPC Action")
    EReditusNPCDialogueAction Action = EReditusNPCDialogueAction::StartBattle;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPC Action", meta=(EditCondition="Action == EReditusNPCDialogueAction::StartBattle", EditConditionHides))
    FName EncounterIDOverride;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPC Action", meta=(EditCondition="Action == EReditusNPCDialogueAction::RemovePermanently", EditConditionHides))
    EReditusNPCRemovalReason RemovalReason = EReditusNPCRemovalReason::Removed;
    virtual void RecieveEventTriggered_Implementation(APlayerController* ConsideringPlayer, AActor* NPCActor) override;
};

// Conditions only read saved state; they never choose an answer or grant items.
UCLASS(BlueprintType, EditInlineNew, meta=(DisplayName="Reditus Quest Choice"))
class REDITUSQUESTNPC_API UReditusQuestChoiceCondition : public UDialogueConditions
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Saved Choice", meta=(RowType="ReditusQuestDefinition")) FDataTableRowHandle QuestRowOverride;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Saved Choice") FName ChoiceID;
    // Empty means any selected option.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Saved Choice") FName OptionID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Saved Choice") bool MustBeUnselected = false;
    virtual bool IsConditionMet_Implementation(APlayerController* ConsideringPlayer, AActor* NPCActor) override;
};
UCLASS(BlueprintType, EditInlineNew, meta=(DisplayName="Reditus Quest Step Applied"))
class REDITUSQUESTNPC_API UReditusQuestStepAppliedCondition : public UDialogueConditions
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Step Action", meta=(RowType="ReditusQuestDefinition")) FDataTableRowHandle QuestRowOverride;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Step Action") FName ActionID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Step Action") bool ExpectedApplied = true;
    virtual bool IsConditionMet_Implementation(APlayerController* ConsideringPlayer, AActor* NPCActor) override;
};
