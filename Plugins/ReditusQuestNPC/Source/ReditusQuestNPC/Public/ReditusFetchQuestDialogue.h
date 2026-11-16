#pragma once
#include "CoreMinimal.h"
#include "Dialogue.h"
#include "ReditusFetchQuestDialogue.generated.h"

// Internal events/conditions in the shared generated flow. Designers edit NPC Text fields.
UCLASS(NotBlueprintable, EditInlineNew)
class REDITUSQUESTNPC_API UReditusFetchQuestCondition : public UDialogueConditions
{
    GENERATED_BODY()
public:
    UPROPERTY(Transient) int32 ExpectedPhase = 0;
    virtual bool IsConditionMet_Implementation(APlayerController* Player, AActor* NPCActor) override;
};

UCLASS(NotBlueprintable, EditInlineNew)
class REDITUSQUESTNPC_API UReditusFetchQuestActionEvent : public UDialogueEvents
{
    GENERATED_BODY()
public:
    UPROPERTY(Transient) int32 FlowAction = 0;
    virtual void RecieveEventTriggered_Implementation(APlayerController* Player, AActor* NPCActor) override;
};
