// Copyright Epic Games, Inc. All Rights Reserved.

#include "Pixel2DAnimStateConduitGraphSchema.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Pixel2DAnimGraphNode_TransitionResult.h"
#include "Pixel2DAnimTransitionGraph.h"
#include "Pixel2DAnimStateConduitNode.h"

/////////////////////////////////////////////////////
// UPixel2DAnimStateConduitGraphSchema

UPixel2DAnimStateConduitGraphSchema::UPixel2DAnimStateConduitGraphSchema(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UPixel2DAnimStateConduitGraphSchema::CreateDefaultNodesForGraph(UEdGraph& Graph) const
{
	FGraphNodeCreator<UPixel2DAnimGraphNode_TransitionResult> NodeCreator(Graph);
	UPixel2DAnimGraphNode_TransitionResult* ResultSinkNode = NodeCreator.CreateNode();
	SetNodeMetaData(ResultSinkNode, FNodeMetadata::DefaultGraphNode);

	UPixel2DAnimTransitionGraph* TypedGraph = CastChecked<UPixel2DAnimTransitionGraph>(&Graph);
	TypedGraph->MyResultNode = ResultSinkNode;

	NodeCreator.Finalize();
}

void UPixel2DAnimStateConduitGraphSchema::GetGraphDisplayInformation(const UEdGraph& Graph, /*out*/ FGraphDisplayInfo& DisplayInfo) const
{
	DisplayInfo.PlainName = FText::FromString(Graph.GetName());

	if (const UPixel2DAnimStateConduitNode* ConduitNode = Cast<const UPixel2DAnimStateConduitNode>(Graph.GetOuter()))
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("NodeTitle"), ConduitNode->GetNodeTitle(ENodeTitleType::FullTitle));

		DisplayInfo.PlainName = FText::Format(NSLOCTEXT("Pixel2DAnimation", "ConduitRuleGraphTitle", "{NodeTitle} (conduit rule)"), Args);
	}

	DisplayInfo.DisplayName = DisplayInfo.PlainName;
}

void UPixel2DAnimStateConduitGraphSchema::HandleGraphBeingDeleted(UEdGraph& GraphBeingRemoved) const
{
	Super::HandleGraphBeingDeleted(GraphBeingRemoved);

	if (UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(&GraphBeingRemoved))
	{
		// Handle composite anim graph nodes
		TArray<UPixel2DAnimStateNodeBase*> StateNodes;
		FBlueprintEditorUtils::GetAllNodesOfClassEx<UPixel2DAnimStateConduitNode>(Blueprint, StateNodes);

		TSet<UPixel2DAnimStateNodeBase*> NodesToDelete;
		for (int32 i = 0; i < StateNodes.Num(); ++i)
		{
			UPixel2DAnimStateNodeBase* StateNode = StateNodes[i];
			if (StateNode->GetBoundGraph() == &GraphBeingRemoved)
			{
				NodesToDelete.Add(StateNode);
			}
		}

		// Delete the node that owns us
		ensure(NodesToDelete.Num() <= 1);
		for (TSet<UPixel2DAnimStateNodeBase*>::TIterator It(NodesToDelete); It; ++It)
		{
			UPixel2DAnimStateNodeBase* NodeToDelete = *It;

			FBlueprintEditorUtils::RemoveNode(Blueprint, NodeToDelete, true);

			// Prevent re-entrancy here
			NodeToDelete->ClearBoundGraph();
		}
	}
}
