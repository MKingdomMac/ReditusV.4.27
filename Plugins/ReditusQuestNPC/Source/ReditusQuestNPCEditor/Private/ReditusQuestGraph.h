#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphSchema.h"
#include "ReditusQuestAsset.h"
#include "ReditusQuestGraph.generated.h"

UENUM()
enum class EReditusQuestGraphNodeKind : uint8 { Start, Stage, Finish };

UCLASS()
class UReditusQuestEdGraph : public UEdGraph
{
    GENERATED_BODY()
};

UCLASS()
class UReditusQuestGraphNode : public UEdGraphNode
{
    GENERATED_BODY()
public:
    UPROPERTY() EReditusQuestGraphNodeKind Kind = EReditusQuestGraphNodeKind::Stage;
    UPROPERTY(EditAnywhere, Category="Stage") FReditusQuestStageDefinition Stage;
    UPROPERTY(EditAnywhere, Category="Conversation", meta=(ToolTip="An existing DialoguePlugin asset. Its nodes, events and recruitment logic are not rewritten."))
    UDialogue* Conversation = nullptr;

    virtual void AllocateDefaultPins() override;
    virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
    virtual FLinearColor GetNodeTitleColor() const override;
    virtual FText GetTooltipText() const override;
    virtual bool CanUserDeleteNode() const override { return Kind == EReditusQuestGraphNodeKind::Stage; }
    virtual bool CanDuplicateNode() const override { return Kind == EReditusQuestGraphNodeKind::Stage; }
    virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
    virtual void PostEditUndo() override;
    virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
    virtual void PostPasteNode() override;
    void RefreshPins();
    void Changed();
    static FName ChoicePin(FName Choice, FName Option);
};

UCLASS()
class UReditusQuestGraphSchema : public UEdGraphSchema
{
    GENERATED_BODY()
public:
    virtual void CreateDefaultNodesForGraph(UEdGraph& Graph) const override;
    virtual void GetGraphContextActions(FGraphContextMenuBuilder& Builder) const override;
    virtual const FPinConnectionResponse CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const override;
    virtual FLinearColor GetPinTypeColor(const FEdGraphPinType& PinType) const override;
    static UReditusQuestGraphNode* AddStage(UEdGraph* Graph, const FVector2D& Position, UEdGraphPin* FromPin = nullptr);
};
