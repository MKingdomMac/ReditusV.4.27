#include "ReditusQuestEditorToolkit.h"
#include "ReditusQuestAsset.h"
#include "ReditusQuestGraph.h"
#include "ReditusQuestGraphCompiler.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "IDetailsView.h"
#include "PropertyEditorModule.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Docking/TabManager.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UnrealType.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ReditusQuestEditor"

namespace
{
    const FName GraphTab(TEXT("ReditusQuest.Graph"));
    const FName DetailsTab(TEXT("ReditusQuest.Details"));
    const FName ConversationsTab(TEXT("ReditusQuest.Conversations"));
}

FReditusQuestEditorToolkit::~FReditusQuestEditorToolkit()
{
    if (GEditor) GEditor->UnregisterForUndo(this);
}

FName FReditusQuestEditorToolkit::GetToolkitFName() const { return TEXT("ReditusQuestEditor"); }
FText FReditusQuestEditorToolkit::GetBaseToolkitName() const { return LOCTEXT("EditorName", "Reditus Quest Editor"); }
FString FReditusQuestEditorToolkit::GetWorldCentricTabPrefix() const { return TEXT("Reditus Quest "); }
FLinearColor FReditusQuestEditorToolkit::GetWorldCentricTabColorScale() const { return FLinearColor(.08f, .24f, .42f); }

void FReditusQuestEditorToolkit::AddReferencedObjects(FReferenceCollector& Collector)
{
    Collector.AddReferencedObject(Quest);
}

bool FReditusQuestEditorToolkit::CanEdit() const { return Quest && (!GEditor || !GEditor->PlayWorld); }

void FReditusQuestEditorToolkit::Init(UReditusQuestAsset* Asset, const TSharedPtr<IToolkitHost>& Host)
{
    Quest = Asset;
    if (!Quest || !Quest->EditorGraph) return;
    LastCompileSucceeded = Quest->Compiled;
    Status = Quest->Compiled ? TEXT("Compiled. Quest progress is stored by the quest system and SaveGame.")
        : TEXT("Draft. Compile & Register before using this quest.");
    FDetailsViewArgs Options; Options.bHideSelectionTip = true;
    Details = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).CreateDetailView(Options);
    Details->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda([](const FPropertyAndParent& Entry)
    {
        const FName Name = Entry.Property.GetFName();
        if (Name == TEXT("CompiledDefinition")) return false;
        if (Entry.Property.GetOwnerStruct() == FReditusQuestDefinition::StaticStruct() && Name == TEXT("Stages")) return false;
        if (Entry.Property.GetOwnerStruct() == FReditusQuestStageDefinition::StaticStruct()
            && (Name == TEXT("Routes") || Name == TEXT("NextStageID") || Name == TEXT("FinishAfterStage"))) return false;
        return true;
    }));
    Details->SetIsPropertyEditingEnabledDelegate(FIsPropertyEditingEnabled::CreateSP(this, &FReditusQuestEditorToolkit::CanEdit));
    Details->OnFinishedChangingProperties().AddSP(this, &FReditusQuestEditorToolkit::PropertiesChanged);
    Details->SetObject(Quest);
    GraphCommands = MakeShared<FUICommandList>();
    GraphCommands->MapAction(FGenericCommands::Get().Undo,
        FExecuteAction::CreateLambda([]() { if (GEditor) GEditor->UndoTransaction(); }),
        FCanExecuteAction::CreateSP(this, &FReditusQuestEditorToolkit::CanEdit));
    GraphCommands->MapAction(FGenericCommands::Get().Redo,
        FExecuteAction::CreateLambda([]() { if (GEditor) GEditor->RedoTransaction(); }),
        FCanExecuteAction::CreateSP(this, &FReditusQuestEditorToolkit::CanEdit));
    GraphCommands->MapAction(FGenericCommands::Get().Delete, FExecuteAction::CreateSP(this, &FReditusQuestEditorToolkit::DeleteSelected),
        FCanExecuteAction::CreateSP(this, &FReditusQuestEditorToolkit::CanDelete));
    GraphCommands->MapAction(FGenericCommands::Get().SelectAll, FExecuteAction::CreateLambda([this]() { if (Graph) Graph->SelectAllNodes(); }));
    SGraphEditor::FGraphEditorEvents Events;
    Events.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateSP(this, &FReditusQuestEditorToolkit::SelectionChanged);
    Events.OnNodeDoubleClicked = FSingleNodeEvent::CreateLambda([](UEdGraphNode* Base)
    {
        auto* Node = Cast<UReditusQuestGraphNode>(Base);
        if (GEditor && Node && Node->Conversation)
            GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Node->Conversation);
    });
    Graph = SNew(SGraphEditor).GraphToEdit(Quest->EditorGraph).AdditionalCommands(GraphCommands).GraphEvents(Events)
        .IsEditable(this, &FReditusQuestEditorToolkit::CanEdit);
    ConversationPanel = SNew(SVerticalBox);
    RefreshConversations();
    const auto Layout = FTabManager::NewLayout(TEXT("ReditusQuestEditor_v1"))
        ->AddArea(FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
            ->Split(FTabManager::NewStack()->SetSizeCoefficient(.1f)->AddTab(GetToolbarTabId(), ETabState::OpenedTab)->SetHideTabWell(true))
            ->Split(FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)->SetSizeCoefficient(.9f)
                ->Split(FTabManager::NewStack()->SetSizeCoefficient(.7f)->AddTab(GraphTab, ETabState::OpenedTab))
                ->Split(FTabManager::NewStack()->SetSizeCoefficient(.3f)
                    ->AddTab(DetailsTab, ETabState::OpenedTab)->AddTab(ConversationsTab, ETabState::OpenedTab))));
    InitAssetEditor(Host.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone, Host,
        TEXT("ReditusQuestEditorApp"), Layout, true, true, Quest);
    GetToolkitCommands()->Append(GraphCommands.ToSharedRef());
    if (GEditor) GEditor->RegisterForUndo(this);
    Graph->ZoomToFit(false);
}

void FReditusQuestEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& Manager)
{
    FAssetEditorToolkit::RegisterTabSpawners(Manager);
    MenuGroup = Manager->AddLocalWorkspaceMenuCategory(LOCTEXT("Workspace", "Reditus Quest"));
    Manager->RegisterTabSpawner(GraphTab, FOnSpawnTab::CreateSP(this, &FReditusQuestEditorToolkit::SpawnGraph))
        .SetDisplayName(LOCTEXT("Graph", "Quest Graph")).SetGroup(MenuGroup.ToSharedRef());
    Manager->RegisterTabSpawner(DetailsTab, FOnSpawnTab::CreateSP(this, &FReditusQuestEditorToolkit::SpawnDetails))
        .SetDisplayName(LOCTEXT("Details", "Details")).SetGroup(MenuGroup.ToSharedRef());
    Manager->RegisterTabSpawner(ConversationsTab, FOnSpawnTab::CreateSP(this, &FReditusQuestEditorToolkit::SpawnConversations))
        .SetDisplayName(LOCTEXT("Conversations", "Conversations")).SetGroup(MenuGroup.ToSharedRef());
}

void FReditusQuestEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& Manager)
{
    Manager->UnregisterTabSpawner(GraphTab); Manager->UnregisterTabSpawner(DetailsTab); Manager->UnregisterTabSpawner(ConversationsTab);
    FAssetEditorToolkit::UnregisterTabSpawners(Manager);
}

TSharedRef<SDockTab> FReditusQuestEditorToolkit::SpawnGraph(const FSpawnTabArgs& Args)
{
    (void)Args;
    return SNew(SDockTab)[SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(4)[SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(LOCTEXT("Compile", "Compile & Register"))
                .IsEnabled(this, &FReditusQuestEditorToolkit::CanEdit).OnClicked_Lambda([this]() { Compile(); return FReply::Handled(); })]
            + SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(LOCTEXT("Settings", "Quest Settings"))
                .OnClicked_Lambda([this]() { Graph->ClearSelectionSet(); Details->SetObject(Quest); return FReply::Handled(); })]
            + SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(LOCTEXT("Add", "Add Stage"))
                .IsEnabled(this, &FReditusQuestEditorToolkit::CanEdit).OnClicked_Lambda([this]()
                { UReditusQuestGraphSchema::AddStage(Quest->EditorGraph, Graph->GetPasteLocation()); return FReply::Handled(); })]
            + SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(LOCTEXT("Catalog", "Open Catalogue"))
                .OnClicked_Lambda([this]()
                {
                    FString Error; auto* Catalog = Quest->RegisteredQuest.DataTable;
                    if (!Catalog) Catalog = ReditusQuestGraphCompiler::DefaultCatalog(Error);
                    if (Catalog && GEditor) GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Catalog->GetPathName());
                    else Status = Error;
                    return FReply::Handled();
                })]]
        + SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(STextBlock).AutoWrapText(true)
            .Text_Lambda([this]() { return FText::FromString(Quest && !Quest->Compiled && LastCompileSucceeded
                ? TEXT("Graph changed. Compile & Register again before Play.") : Status); })]
        + SVerticalBox::Slot().FillHeight(1)[Graph.ToSharedRef()]];
}

TSharedRef<SDockTab> FReditusQuestEditorToolkit::SpawnDetails(const FSpawnTabArgs& Args)
{
    (void)Args; return SNew(SDockTab)[Details.ToSharedRef()];
}

TSharedRef<SDockTab> FReditusQuestEditorToolkit::SpawnConversations(const FSpawnTabArgs& Args)
{
    (void)Args; return SNew(SDockTab)[SNew(SScrollBox) + SScrollBox::Slot()[ConversationPanel.ToSharedRef()]];
}

void FReditusQuestEditorToolkit::SelectionChanged(const TSet<UObject*>& Selection)
{
    if (Selection.Num() == 1) for (auto* Object : Selection)
        if (auto* Node = Cast<UReditusQuestGraphNode>(Object)) if (Node->Kind == EReditusQuestGraphNodeKind::Stage)
        { Details->SetObject(Node); return; }
    Details->SetObject(Quest);
}

void FReditusQuestEditorToolkit::RefreshConversations()
{
    if (!ConversationPanel || !Quest) return;
    ConversationPanel->ClearChildren();
    ConversationPanel->AddSlot().AutoHeight().Padding(6)[SNew(STextBlock).AutoWrapText(true)
        .Text(LOCTEXT("DialogueHelp", "Assign existing Dialogue assets in Quest Settings or on a Stage. Double-click a linked stage to open the original dialogue editor. Existing NPC rules and slots take precedence. Quest events are added explicitly in that dialogue editor."))];
    const auto Add = [this](FText Label, UDialogue* Dialogue)
    {
        const TWeakObjectPtr<UDialogue> WeakDialogue(Dialogue);
        ConversationPanel->AddSlot().AutoHeight().Padding(5)[SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).AutoWrapText(true)
                .Text(FText::Format(LOCTEXT("DialogueEntry", "{0}: {1}"), Label,
                    Dialogue ? FText::FromString(Dialogue->GetName()) : LOCTEXT("Unassigned", "unassigned")))]
            + SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("Open", "Open"))
                .IsEnabled(Dialogue != nullptr).OnClicked_Lambda([WeakDialogue]()
                {
                    if (GEditor && WeakDialogue.IsValid()) GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(WeakDialogue.Get());
                    return FReply::Handled();
                })]];
    };
    Add(LOCTEXT("Offer", "Available"), Quest->AvailableDialogue);
    Add(LOCTEXT("Ready", "Ready to deliver"), Quest->ReadyToDeliverDialogue);
    Add(LOCTEXT("Completed", "Completed"), Quest->CompletedDialogue);
    Add(LOCTEXT("Failed", "Failed"), Quest->FailedDialogue);
    Add(LOCTEXT("Unavailable", "Unavailable"), Quest->UnavailableDialogue);
    if (Quest->EditorGraph) for (auto* Base : Quest->EditorGraph->Nodes)
        if (auto* Node = Cast<UReditusQuestGraphNode>(Base)) if (Node->Kind == EReditusQuestGraphNodeKind::Stage)
            Add(FText::FromName(Node->Stage.StageID), Node->Conversation);
}

void FReditusQuestEditorToolkit::PropertiesChanged(const FPropertyChangedEvent& Event)
{
    (void)Event;
    RefreshConversations();
}

void FReditusQuestEditorToolkit::Compile()
{
    if (!CanEdit()) return;
    const FScopedTransaction Transaction(LOCTEXT("CompileTransaction", "Compile and register quest"));
    FString Error;
    LastCompileSucceeded = ReditusQuestGraphCompiler::Compile(Quest, Error);
    Status = LastCompileSucceeded ? TEXT("Compiled and registered. Save this quest and its catalogue before Play.") : TEXT("Compile failed: ") + Error;
    if (!LastCompileSucceeded) UE_LOG(LogTemp, Warning, TEXT("Reditus Quest: %s"), *Error);
    Details->ForceRefresh(); RefreshConversations();
}

void FReditusQuestEditorToolkit::SaveAsset_Execute()
{
    if (!CanEdit()) return;
    Compile();
    TArray<UPackage*> Packages; Packages.Add(Quest->GetOutermost());
    if (Quest->RegisteredQuest.DataTable) Packages.AddUnique(Quest->RegisteredQuest.DataTable->GetOutermost());
    FEditorFileUtils::PromptForCheckoutAndSave(Packages, true, false);
}

bool FReditusQuestEditorToolkit::CanDelete() const
{
    if (!CanEdit() || !Graph) return false;
    for (auto* Object : Graph->GetSelectedNodes())
        if (auto* Node = Cast<UReditusQuestGraphNode>(Object)) if (Node->CanUserDeleteNode()) return true;
    return false;
}

void FReditusQuestEditorToolkit::DeleteSelected()
{
    if (!CanDelete()) return;
    const FScopedTransaction Transaction(LOCTEXT("DeleteTransaction", "Delete quest stages"));
    Quest->Modify(); Quest->EditorGraph->Modify();
    const auto Selection = Graph->GetSelectedNodes(); Graph->ClearSelectionSet();
    for (auto* Object : Selection) if (auto* Node = Cast<UReditusQuestGraphNode>(Object)) if (Node->CanUserDeleteNode())
    {
        Node->Modify(); Quest->EditorGraph->GetSchema()->BreakNodeLinks(*Node); Node->DestroyNode();
    }
    Quest->InvalidateCompilation(); Graph->NotifyGraphChanged(); RefreshConversations();
}

void FReditusQuestEditorToolkit::PostUndo(bool Success)
{
    if (Success && Quest)
    { Quest->InvalidateCompilation(); Graph->ClearSelectionSet(); Graph->NotifyGraphChanged(); Details->SetObject(Quest); RefreshConversations(); }
}

void FReditusQuestEditorToolkit::PostRedo(bool Success) { PostUndo(Success); }

#undef LOCTEXT_NAMESPACE
