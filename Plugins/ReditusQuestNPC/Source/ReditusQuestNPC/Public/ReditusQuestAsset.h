#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ReditusQuestSystemTypes.h"
#include "ReditusQuestAsset.generated.h"

class UEdGraph;

USTRUCT()
struct REDITUSQUESTNPC_API FReditusQuestStageConversation
{
    GENERATED_BODY()
    UPROPERTY() FName StageID;
    UPROPERTY() UDialogue* Dialogue = nullptr;
};

// Authoring graph is editor-only. Packaged games read the checked compiled definition.
UCLASS(BlueprintType, meta=(DisplayName="Reditus Quest Graph"))
class REDITUSQUESTNPC_API UReditusQuestAsset : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category="Quest", meta=(ToolTip="Stable catalogue row name. Keep this ID after publishing saves."))
    FName QuestID;
    UPROPERTY(VisibleAnywhere, Category="Catalogue") FDataTableRowHandle RegisteredQuest;
    UPROPERTY(VisibleAnywhere, Category="Compilation") bool Compiled = false;
    UPROPERTY(VisibleAnywhere, Category="Compilation") FReditusQuestDefinition CompiledDefinition;
    UPROPERTY() TArray<FReditusQuestStageConversation> StageConversations;

    // Optional defaults. Explicit NPC rules and dialogue slots take precedence.
    UPROPERTY(EditAnywhere, Category="Conversations") UDialogue* AvailableDialogue = nullptr;
    UPROPERTY(EditAnywhere, Category="Conversations") UDialogue* ReadyToDeliverDialogue = nullptr;
    UPROPERTY(EditAnywhere, Category="Conversations") UDialogue* CompletedDialogue = nullptr;
    UPROPERTY(EditAnywhere, Category="Conversations") UDialogue* FailedDialogue = nullptr;
    UPROPERTY(EditAnywhere, Category="Conversations") UDialogue* UnavailableDialogue = nullptr;

#if WITH_EDITORONLY_DATA
    UPROPERTY(EditAnywhere, Category="Quest Settings") FReditusQuestDefinition Settings;
    UPROPERTY() UEdGraph* EditorGraph = nullptr;
#endif
#if WITH_EDITOR
    void InvalidateCompilation();
    virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
    virtual void PostEditUndo() override;
    virtual void PostDuplicate(bool bDuplicateForPIE) override;
#endif
};
