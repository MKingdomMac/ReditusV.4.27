#include "ReditusQuestGraph.h"
#include "Editor.h"
#include "EdGraph/EdGraphPin.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "ReditusQuestGraph"

FName UReditusQuestGraphNode::ChoicePin(FName Choice, FName Option)
{
    return FName(*(Choice.ToString() + TEXT("/") + Option.ToString()));
}

void UReditusQuestGraphNode::AllocateDefaultPins()
{
    const FName Category(TEXT("QuestFlow"));
    if (Kind != EReditusQuestGraphNodeKind::Start) CreatePin(EGPD_Input, Category, NAME_None, nullptr, TEXT("In"));
    if (Kind != EReditusQuestGraphNodeKind::Finish) CreatePin(EGPD_Output, Category, NAME_None, nullptr, TEXT("Next"));
    if (Kind == EReditusQuestGraphNodeKind::Stage)
        for (const auto& Choice : Stage.Choices) for (const auto& Option : Choice.Options)
        {
            auto* Pin = CreatePin(EGPD_Output, Category, NAME_None, nullptr, ChoicePin(Choice.ChoiceID, Option.OptionID));
            Pin->PinFriendlyName = FText::Format(LOCTEXT("ChoicePin", "{0}: {1}"),
                FText::FromName(Choice.ChoiceID), Option.Label.IsEmpty() ? FText::FromName(Option.OptionID) : Option.Label);
        }
}

FText UReditusQuestGraphNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
    (void)TitleType;
    if (Kind == EReditusQuestGraphNodeKind::Start) return LOCTEXT("Start", "Start Quest");
    if (Kind == EReditusQuestGraphNodeKind::Finish) return LOCTEXT("Finish", "Ready for Turn-in");
    return FText::Format(LOCTEXT("StageTitle", "{0}\n{1}"), FText::FromName(Stage.StageID), Stage.Description);
}

FLinearColor UReditusQuestGraphNode::GetNodeTitleColor() const
{
    return Kind == EReditusQuestGraphNodeKind::Start ? FLinearColor(.12f, .42f, .24f)
        : Kind == EReditusQuestGraphNodeKind::Finish ? FLinearColor(.5f, .34f, .08f) : FLinearColor(.08f, .24f, .42f);
}

FText UReditusQuestGraphNode::GetTooltipText() const
{
    return Kind == EReditusQuestGraphNodeKind::Finish
        ? LOCTEXT("ReadyTip", "Finishes the objectives. Rewards still require the existing checked Turn In Quest action.")
        : LOCTEXT("StageTip", "Select a stage to edit objectives, timers, choices, handoffs, sequences and its existing dialogue asset.");
}

void UReditusQuestGraphNode::Changed()
{
    if (auto* Asset = GetTypedOuter<UReditusQuestAsset>()) Asset->InvalidateCompilation();
}

void UReditusQuestGraphNode::RefreshPins()
{
    const auto OldPins = Pins;
    Pins.Reset(); AllocateDefaultPins();
    for (auto* Old : OldPins)
    {
        if (!Old) continue;
        auto* Replacement = FindPin(Old->PinName, Old->Direction);
        const auto Links = Old->LinkedTo;
        for (auto* Other : Links) if (Other) Other->Modify();
        if (Replacement)
        {
            const auto Label = Replacement->PinFriendlyName;
            Replacement->MovePersistentDataFromOldPin(*Old);
            Replacement->PinFriendlyName = Label;
        }
        else Old->BreakAllPinLinks();
        UEdGraphNode::DestroyPin(Old);
    }
    Changed();
    if (GetGraph()) GetGraph()->NotifyGraphChanged();
}

void UReditusQuestGraphNode::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
    Super::PostEditChangeProperty(Event);
    RefreshPins();
}

void UReditusQuestGraphNode::PostEditUndo()
{
    Super::PostEditUndo(); Changed();
    if (GetGraph()) GetGraph()->NotifyGraphChanged();
}

void UReditusQuestGraphNode::PinConnectionListChanged(UEdGraphPin* Pin)
{
    Super::PinConnectionListChanged(Pin); Changed();
}

void UReditusQuestGraphNode::PostPasteNode()
{
    Super::PostPasteNode();
    Stage.StageID = FName(*(TEXT("Stage_") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8)));
    Changed();
}

namespace
{
    UReditusQuestGraphNode* CreateNode(UEdGraph* Graph, EReditusQuestGraphNodeKind Kind, int32 X)
    {
        auto* Node = NewObject<UReditusQuestGraphNode>(Graph, NAME_None, RF_Transactional);
        Node->Kind = Kind; Node->NodePosX = X;
        Graph->AddNode(Node, false, false); Node->CreateNewGuid(); Node->PostPlacedNewNode();
        return Node;
    }

    struct FAddStageAction : FEdGraphSchemaAction
    {
        FAddStageAction() : FEdGraphSchemaAction(LOCTEXT("Quest", "Quest"), LOCTEXT("AddStage", "Add Quest Stage"),
            LOCTEXT("AddStageTip", "Add objectives, a timer, branches and a linked conversation."), 0) {}
        virtual UEdGraphNode* PerformAction(UEdGraph* Graph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode = true) override
        {
            (void)bSelectNewNode;
            return UReditusQuestGraphSchema::AddStage(Graph, Location, FromPin);
        }
    };
}

UReditusQuestGraphNode* UReditusQuestGraphSchema::AddStage(UEdGraph* Graph, const FVector2D& Position, UEdGraphPin* FromPin)
{
    if (!Graph || (GEditor && GEditor->PlayWorld)) return nullptr;
    const FScopedTransaction Transaction(LOCTEXT("AddStageTransaction", "Add quest stage"));
    Graph->Modify();
    if (auto* Asset = Graph->GetTypedOuter<UReditusQuestAsset>()) Asset->Modify();
    auto* Node = CreateNode(Graph, EReditusQuestGraphNodeKind::Stage, int32(Position.X));
    Node->NodePosY = int32(Position.Y);
    Node->Stage.StageID = FName(*(TEXT("Stage_") + Node->NodeGuid.ToString(EGuidFormats::Digits).Left(8)));
    FReditusQuestTaskDefinition Task;
    Task.ObjectiveID = TEXT("Objective_1"); Task.Kind = EReditusQuestObjectiveKind::Talk; Task.TargetID = TEXT("QuestGiver");
    Task.Description = LOCTEXT("DefaultObjective", "Talk to the quest giver"); Node->Stage.Objectives.Add(Task);
    Node->AllocateDefaultPins();
    if (FromPin) Graph->GetSchema()->TryCreateConnection(FromPin, Node->FindPin(TEXT("In")));
    Node->Changed(); Graph->NotifyGraphChanged(); return Node;
}

void UReditusQuestGraphSchema::CreateDefaultNodesForGraph(UEdGraph& Graph) const
{
    auto* Start = CreateNode(&Graph, EReditusQuestGraphNodeKind::Start, 0);
    auto* Stage = AddStage(&Graph, FVector2D(300, 0));
    auto* Finish = CreateNode(&Graph, EReditusQuestGraphNodeKind::Finish, 700);
    Start->AllocateDefaultPins(); Finish->AllocateDefaultPins();
    if (Stage)
    {
        Start->FindPin(TEXT("Next"))->MakeLinkTo(Stage->FindPin(TEXT("In")));
        Stage->FindPin(TEXT("Next"))->MakeLinkTo(Finish->FindPin(TEXT("In")));
    }
}

void UReditusQuestGraphSchema::GetGraphContextActions(FGraphContextMenuBuilder& Builder) const
{
    if (!GEditor || !GEditor->PlayWorld) Builder.AddAction(MakeShared<FAddStageAction>());
}

const FPinConnectionResponse UReditusQuestGraphSchema::CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const
{
    if (!A || !B || A->GetOwningNode() == B->GetOwningNode() || A->Direction == B->Direction
        || A->GetOwningNode()->GetGraph() != B->GetOwningNode()->GetGraph() || (GEditor && GEditor->PlayWorld))
        return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("InvalidConnection", "Connect an output to another node's input."));
    const auto* Output = A->Direction == EGPD_Output ? A : B;
    if (Output->LinkedTo.Num())
        return FPinConnectionResponse(Output == A ? CONNECT_RESPONSE_BREAK_OTHERS_A : CONNECT_RESPONSE_BREAK_OTHERS_B,
            LOCTEXT("Replace", "Replace this output's destination."));
    return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());
}

FLinearColor UReditusQuestGraphSchema::GetPinTypeColor(const FEdGraphPinType& PinType) const
{
    (void)PinType; return FLinearColor(.9f, .72f, .3f);
}

#undef LOCTEXT_NAMESPACE
