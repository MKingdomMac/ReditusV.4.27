#include "ReditusQuestAsset.h"
#include "ReditusQuestCatalog.h"
#include "ReditusQuestGraph.h"
#include "ReditusQuestGraphCompiler.h"
#include "ReditusQuestDialogueEvents.h"
#include "EdGraph/EdGraphPin.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReditusQuestGraphIntegrationTest,
    "Reditus.QuestEditor.GraphCompileAndDialogueReferences",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReditusQuestGraphIntegrationTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    auto* Catalog = NewObject<UDataTable>(GetTransientPackage());
    Catalog->RowStruct = FReditusQuestCatalogRow::StaticStruct();
    auto* Asset = NewObject<UReditusQuestAsset>(GetTransientPackage(), NAME_None, RF_Transactional);
    Asset->QuestID = TEXT("AutomationHunt"); Asset->Settings.Title = FText::FromString(TEXT("Automation hunt"));
    Asset->RegisteredQuest.DataTable = Catalog; Asset->RegisteredQuest.RowName = Asset->QuestID;
    Asset->EditorGraph = NewObject<UReditusQuestEdGraph>(Asset, NAME_None, RF_Transactional);
    Asset->EditorGraph->Schema = UReditusQuestGraphSchema::StaticClass();
    Asset->EditorGraph->GetSchema()->CreateDefaultNodesForGraph(*Asset->EditorGraph);
    UReditusQuestGraphNode* Stage = nullptr;
    for (auto* Base : Asset->EditorGraph->Nodes) if (auto* Node = Cast<UReditusQuestGraphNode>(Base))
        if (Node->Kind == EReditusQuestGraphNodeKind::Stage) Stage = Node;
    if (!TestNotNull(TEXT("Default stage exists"), Stage)) return false;
    auto* Dialogue = NewObject<UDialogue>(GetTransientPackage());
    auto* Event = NewObject<UReditusNPCActionEvent>(Dialogue); Event->Action = EReditusNPCDialogueAction::Recruit;
    FDialogueNode DialogueNode; DialogueNode.id = 0; DialogueNode.Events.Add(Event); Dialogue->Data.Add(DialogueNode);
    Stage->Conversation = Dialogue;
    const auto OriginalPinID = Stage->FindPin(TEXT("Next"))->PinId;
    Stage->RefreshPins();
    TestTrue(TEXT("Refreshing details preserves pin identity"), Stage->FindPin(TEXT("Next"))->PinId == OriginalPinID);
    TestEqual(TEXT("Refreshing details preserves the route"), Stage->FindPin(TEXT("Next"))->LinkedTo.Num(), 1);
    FString Error;
    TestTrue(TEXT("Actual editor graph compiles"), ReditusQuestGraphCompiler::Compile(Asset, Error));
    TestTrue(TEXT("Compiled catalogue resolves"), ReditusQuestCatalog::Definition(Asset->RegisteredQuest) == &Asset->CompiledDefinition);
    TestTrue(TEXT("Stage conversation is the original asset"),
        ReditusQuestCatalog::Conversation(Asset->RegisteredQuest, EReditusQuestState::Active, Stage->Stage.StageID) == Dialogue);
    TestEqual(TEXT("Original dialogue event count"), Dialogue->Data[0].Events.Num(), 1);
    TestTrue(TEXT("Original event object is preserved"), Dialogue->Data[0].Events[0] == Event);
    TestTrue(TEXT("Original recruitment action is preserved"), Event->Action == EReditusNPCDialogueAction::Recruit);
    TestTrue(TEXT("Finish produces readiness, not an automatic reward"), Asset->CompiledDefinition.Stages[0].FinishAfterStage);
    Stage->Changed();
    TestFalse(TEXT("Edited draft cannot resolve a stale compiled payload"), ReditusQuestCatalog::Definition(Asset->RegisteredQuest) != nullptr);
    TestTrue(TEXT("Draft recompiles"), ReditusQuestGraphCompiler::Compile(Asset, Error));
    auto* Duplicate = DuplicateObject<UReditusQuestAsset>(Asset, GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(), UReditusQuestAsset::StaticClass(), TEXT("QuestDuplicate")));
    TestFalse(TEXT("Duplicate needs registration"), Duplicate->Compiled);
    TestNull(TEXT("Duplicate has no old catalogue identity"), Duplicate->RegisteredQuest.DataTable);
    Stage->FindPin(TEXT("Next"))->BreakAllPinLinks();
    TestFalse(TEXT("Disconnected graph is rejected"), ReditusQuestGraphCompiler::Compile(Asset, Error));
    TestTrue(TEXT("Failed compile leaves the original catalogue entry"),
        Catalog->FindRow<FReditusQuestCatalogRow>(Asset->QuestID, TEXT("Automation"), false)->GraphQuest == Asset);
    TestEqual(TEXT("Compiling never appends recruitment events"), Dialogue->Data[0].Events.Num(), 1);
    return true;
}
#endif
