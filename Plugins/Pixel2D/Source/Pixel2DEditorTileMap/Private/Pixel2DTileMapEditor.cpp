// Copyright Epic Games, Inc. All Rights Reserved.

#include "Pixel2DTileMapEditor.h"
#include "Widgets/Text/STextBlock.h"
#include "EditorViewportClient.h"
#include "UObject/Package.h"
#include "EditorStyleSet.h"
#include "SEditorViewport.h"
#include "SSingleObjectDetailsPanel.h"
#include "Pixel2DTileMap.h"


#include "Pixel2DTileMapEditorViewportClient.h"
#include "Pixel2DTileMapEditorCommands.h"
#include "SCommonEditorViewportToolbarBase.h"
#include "SPixel2DTileMapEditorViewportToolbar.h"
#include "Widgets/Docking/SDockTab.h"
#include "Subsystems/AssetEditorSubsystem.h"

#define LOCTEXT_NAMESPACE "Pixel2DTileMapEditor"

//////////////////////////////////////////////////////////////////////////

const FName TileMapEditorAppName = FName(TEXT("Pixel2DTileMapEditorApp"));

//////////////////////////////////////////////////////////////////////////

struct FTileMapEditorTabs
{
	// Tab identifiers
	static const FName DetailsID;
	static const FName ViewportID;
	static const FName ToolboxHostID;
};

//////////////////////////////////////////////////////////////////////////

const FName FTileMapEditorTabs::DetailsID(TEXT("Details"));
const FName FTileMapEditorTabs::ViewportID(TEXT("Viewport"));
const FName FTileMapEditorTabs::ToolboxHostID(TEXT("Toolbox"));

//////////////////////////////////////////////////////////////////////////
// SPixel2DTileMapEditorViewport

class SPixel2DTileMapEditorViewport : public SEditorViewport, public ICommonEditorViewportToolbarInfoProvider
{
public:
	SLATE_BEGIN_ARGS(SPixel2DTileMapEditorViewport) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<FPixel2DTileMapEditor> InTileMapEditor);

	// SEditorViewport interface
	virtual void BindCommands() override;
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
	virtual TSharedPtr<SWidget> MakeViewportToolbar() override;
	virtual EVisibility GetTransformToolbarVisibility() const override;
	virtual void OnFocusViewportToSelection() override;
	// End of SEditorViewport interface

	// ICommonEditorViewportToolbarInfoProvider interface
	virtual TSharedRef<class SEditorViewport> GetViewportWidget() override;
	virtual TSharedPtr<FExtender> GetExtenders() const override;
	virtual void OnFloatingButtonClicked() override;
	// End of ICommonEditorViewportToolbarInfoProvider interface

	// Invalidate any references to the tile map being edited; it has changed
	void NotifyTileMapBeingEditedHasChanged()
	{
		EditorViewportClient->NotifyTileMapBeingEditedHasChanged();
	}

	void ActivateEditMode()
	{
		EditorViewportClient->ActivateEditMode();
	}
private:
	// Pointer back to owning tile map editor instance (the keeper of state)
	TWeakPtr<class FPixel2DTileMapEditor> TileMapEditorPtr;

	// Viewport client
	TSharedPtr<FPixel2DTileMapEditorViewportClient> EditorViewportClient;
};

void SPixel2DTileMapEditorViewport::Construct(const FArguments& InArgs, TSharedPtr<FPixel2DTileMapEditor> InTileMapEditor)
{
	TileMapEditorPtr = InTileMapEditor;

	SEditorViewport::Construct(SEditorViewport::FArguments());
}

void SPixel2DTileMapEditorViewport::BindCommands()
{
	SEditorViewport::BindCommands();

	const FPixel2DTileMapEditorCommands& Commands = FPixel2DTileMapEditorCommands::Get();

	TSharedRef<FPixel2DTileMapEditorViewportClient> EditorViewportClientRef = EditorViewportClient.ToSharedRef();

	// Show toggles
	CommandList->MapAction(
		Commands.SetShowCollision,
		FExecuteAction::CreateSP(EditorViewportClientRef, &FEditorViewportClient::SetShowCollision),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(EditorViewportClientRef, &FEditorViewportClient::IsSetShowCollisionChecked));

	CommandList->MapAction(
		Commands.SetShowPivot,
		FExecuteAction::CreateSP(EditorViewportClientRef, &FPixel2DTileMapEditorViewportClient::ToggleShowPivot),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(EditorViewportClientRef, &FPixel2DTileMapEditorViewportClient::IsShowPivotChecked));

	CommandList->MapAction(
		Commands.SetShowTileGrid,
		FExecuteAction::CreateSP(EditorViewportClientRef, &FPixel2DTileMapEditorViewportClient::ToggleShowTileGrid),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(EditorViewportClientRef, &FPixel2DTileMapEditorViewportClient::IsShowTileGridChecked));

	CommandList->MapAction(
		Commands.SetShowLayerGrid,
		FExecuteAction::CreateSP(EditorViewportClientRef, &FPixel2DTileMapEditorViewportClient::ToggleShowLayerGrid),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(EditorViewportClientRef, &FPixel2DTileMapEditorViewportClient::IsShowLayerGridChecked));

	CommandList->MapAction(
		Commands.SetShowTileMapStats,
		FExecuteAction::CreateSP(EditorViewportClientRef, &FPixel2DTileMapEditorViewportClient::ToggleShowTileMapStats),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(EditorViewportClientRef, &FPixel2DTileMapEditorViewportClient::IsShowTileMapStatsChecked));
}

TSharedRef<FEditorViewportClient> SPixel2DTileMapEditorViewport::MakeEditorViewportClient()
{
	EditorViewportClient = MakeShareable(new FPixel2DTileMapEditorViewportClient(TileMapEditorPtr, SharedThis(this)));

	return EditorViewportClient.ToSharedRef();
}

TSharedPtr<SWidget> SPixel2DTileMapEditorViewport::MakeViewportToolbar()
{
	return SNew(SPixel2DTileMapEditorViewportToolbar, SharedThis(this));
}

EVisibility SPixel2DTileMapEditorViewport::GetTransformToolbarVisibility() const
{
	return EVisibility::Visible;
}

void SPixel2DTileMapEditorViewport::OnFocusViewportToSelection()
{
	EditorViewportClient->RequestFocusOnSelection(/*bInstant=*/ false);
}

TSharedRef<class SEditorViewport> SPixel2DTileMapEditorViewport::GetViewportWidget()
{
	return SharedThis(this);
}

TSharedPtr<FExtender> SPixel2DTileMapEditorViewport::GetExtenders() const
{
	TSharedPtr<FExtender> Result(MakeShareable(new FExtender));
	return Result;
}

void SPixel2DTileMapEditorViewport::OnFloatingButtonClicked()
{
}

/////////////////////////////////////////////////////
// SPixel2DTileMapPropertiesTabBody

class SPixel2DTileMapPropertiesTabBody : public SSingleObjectDetailsPanel
{
public:
	SLATE_BEGIN_ARGS(SPixel2DTileMapPropertiesTabBody) {}
	SLATE_END_ARGS()

private:
	// Pointer back to owning TileMap editor instance (the keeper of state)
	TWeakPtr<class FPixel2DTileMapEditor> TileMapEditorPtr;
public:
	void Construct(const FArguments& InArgs, TSharedPtr<FPixel2DTileMapEditor> InTileMapEditor)
	{
		TileMapEditorPtr = InTileMapEditor;

		SSingleObjectDetailsPanel::Construct(SSingleObjectDetailsPanel::FArguments().HostCommandList(InTileMapEditor->GetToolkitCommands()).HostTabManager(InTileMapEditor->GetTabManager()), /*bAutoObserve=*/ true, /*bAllowSearch=*/ true);
	}

	// SSingleObjectDetailsPanel interface
	virtual UObject* GetObjectToObserve() const override
	{
		return TileMapEditorPtr.Pin()->GetTileMapBeingEdited();
	}

	virtual TSharedRef<SWidget> PopulateSlot(TSharedRef<SWidget> PropertyEditorWidget) override
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.FillHeight(1)
			[
				PropertyEditorWidget
			];
	}
	// End of SSingleObjectDetailsPanel interface
};

//////////////////////////////////////////////////////////////////////////
// FPixel2DTileMapEditor

FPixel2DTileMapEditor::FPixel2DTileMapEditor()
	: TileMapBeingEdited(nullptr)
{
}

TSharedRef<SDockTab> FPixel2DTileMapEditor::SpawnTab_Viewport(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("ViewportTab_Title", "Viewport"))
		[
			SNew(SOverlay)

			// The TileMap editor viewport
			+ SOverlay::Slot()
			[
				ViewportPtr.ToSharedRef()
			]
		];
}

TSharedRef<SDockTab> FPixel2DTileMapEditor::SpawnTab_ToolboxHost(const FSpawnTabArgs& Args)
{
	TSharedPtr<FPixel2DTileMapEditor> TileMapEditorPtr = SharedThis(this);

	// Spawn the tab
	return SNew(SDockTab)
		.Icon(FEditorStyle::GetBrush("LevelEditor.Tabs.Modes"))
		.Label(LOCTEXT("ToolboxHost_Title", "Toolbox"))
		[
			ToolboxPtr.ToSharedRef()
		];
}

TSharedRef<SDockTab> FPixel2DTileMapEditor::SpawnTab_Details(const FSpawnTabArgs& Args)
{
	TSharedPtr<FPixel2DTileMapEditor> TileMapEditorPtr = SharedThis(this);

	// Spawn the tab
	return SNew(SDockTab)
		.Icon(FEditorStyle::GetBrush("LevelEditor.Tabs.Details"))
		.Label(LOCTEXT("DetailsTab_Title", "Details"))
		[
			SNew(SPixel2DTileMapPropertiesTabBody, TileMapEditorPtr)
		];
}

void FPixel2DTileMapEditor::RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_Pixel2DTileMapEditor", "Pixel2D Tile Map Editor"));
	auto WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(FTileMapEditorTabs::ViewportID, FOnSpawnTab::CreateSP(this, &FPixel2DTileMapEditor::SpawnTab_Viewport))
		.SetDisplayName(LOCTEXT("ViewportTab", "Viewport"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Viewports"));

	InTabManager->RegisterTabSpawner(FTileMapEditorTabs::ToolboxHostID, FOnSpawnTab::CreateSP(this, &FPixel2DTileMapEditor::SpawnTab_ToolboxHost))
		.SetDisplayName(LOCTEXT("ToolboxHostLabel", "Toolbox"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Modes"));

	InTabManager->RegisterTabSpawner(FTileMapEditorTabs::DetailsID, FOnSpawnTab::CreateSP(this, &FPixel2DTileMapEditor::SpawnTab_Details))
		.SetDisplayName(LOCTEXT("DetailsTabLabel", "Details"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details"));
}

void FPixel2DTileMapEditor::UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);

	InTabManager->UnregisterTabSpawner(FTileMapEditorTabs::ViewportID);
	InTabManager->UnregisterTabSpawner(FTileMapEditorTabs::ToolboxHostID);
	InTabManager->UnregisterTabSpawner(FTileMapEditorTabs::DetailsID);
}

void FPixel2DTileMapEditor::InitTileMapEditor(const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, class UPixel2DTileMap* InitTileMap)
{
	GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseOtherEditors(InitTileMap, this);
	TileMapBeingEdited = InitTileMap;

	FPixel2DTileMapEditorCommands::Register();

	BindCommands();

	ViewportPtr = SNew(SPixel2DTileMapEditorViewport, SharedThis(this));
	ToolboxPtr = SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(0.f);

	// Default layout
	const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_PixelMapTileMapEditor_Layout_v1")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.1f)
				->SetHideTabWell(true)
				->AddTab(GetToolbarTabId(), ETabState::OpenedTab)
			)
			->Split
			(
				FTabManager::NewSplitter()
				->SetOrientation(Orient_Horizontal)
				->SetSizeCoefficient(0.9f)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.3f)
					->SetHideTabWell(true)
					->AddTab(FTileMapEditorTabs::ToolboxHostID, ETabState::OpenedTab)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.8f)
					->SetHideTabWell(true)
					->AddTab(FTileMapEditorTabs::ViewportID, ETabState::OpenedTab)
				)
				->Split
				(
					FTabManager::NewSplitter()
					->SetOrientation(Orient_Vertical)
					->SetSizeCoefficient(0.2f)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.75f)
						->AddTab(FTileMapEditorTabs::DetailsID, ETabState::OpenedTab)
					)
				)
			)
		);

	// Initialize the asset editor and spawn the layout above
	InitAssetEditor(Mode, InitToolkitHost, TileMapEditorAppName, StandaloneDefaultLayout, /*bCreateDefaultStandaloneMenu=*/ true, /*bCreateDefaultToolbar=*/ true, InitTileMap);

	// Activate the edit mode
	ViewportPtr->ActivateEditMode();

	// Extend things
	ExtendMenu();
	ExtendToolbar();
	RegenerateMenusAndToolbars();
}

void FPixel2DTileMapEditor::BindCommands()
{
	// Commands would go here
}

FName FPixel2DTileMapEditor::GetToolkitFName() const
{
	return FName("Pixel2DTileMapEditor");
}

FText FPixel2DTileMapEditor::GetBaseToolkitName() const
{
	return LOCTEXT("TileMapEditorAppLabelBase", "Pixel2D Tile Map Editor");
}

FText FPixel2DTileMapEditor::GetToolkitName() const
{
	const bool bDirtyState = TileMapBeingEdited->GetOutermost()->IsDirty();

	FFormatNamedArguments Args;
	Args.Add(TEXT("TileMapName"), FText::FromString(TileMapBeingEdited->GetName()));
	Args.Add(TEXT("DirtyState"), bDirtyState ? FText::FromString(TEXT("*")) : FText::GetEmpty());
	return FText::Format(LOCTEXT("TileMapEditorAppLabel", "{TileMapName}{DirtyState}"), Args);
}

FText FPixel2DTileMapEditor::GetToolkitToolTipText() const
{
	return GetToolTipTextForObject(TileMapBeingEdited);
}

FString FPixel2DTileMapEditor::GetWorldCentricTabPrefix() const
{
	return TEXT("Pixel2DTileMapEditor");
}

FString FPixel2DTileMapEditor::GetDocumentationLink() const
{
	return TEXT("");
}

void FPixel2DTileMapEditor::OnToolkitHostingStarted(const TSharedRef< class IToolkit >& Toolkit)
{
	TSharedPtr<SWidget> InlineContent = Toolkit->GetInlineContent();
	if (InlineContent.IsValid())
	{
		ToolboxPtr->SetContent(InlineContent.ToSharedRef());
	}
}

void FPixel2DTileMapEditor::OnToolkitHostingFinished(const TSharedRef< class IToolkit >& Toolkit)
{
	ToolboxPtr->SetContent(SNullWidget::NullWidget);

	//@TODO: MODETOOLS: How to handle multiple ed modes at once in a standalone asset editor?
#if 0
	bool FoundAnotherToolkit = false;
	const TArray< TSharedPtr< IToolkit > >& HostedToolkits = LevelEditor.Pin()->GetHostedToolkits();
	for (auto HostedToolkitIt = HostedToolkits.CreateConstIterator(); HostedToolkitIt; ++HostedToolkitIt)
	{
		if ((*HostedToolkitIt) != Toolkit)
		{
			UpdateInlineContent((*HostedToolkitIt)->GetInlineContent());
			FoundAnotherToolkit = true;
			break;
		}
	}

	if (!FoundAnotherToolkit)
	{
		UpdateInlineContent(SNullWidget::NullWidget);
	}

#endif
}

FLinearColor FPixel2DTileMapEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor::White;
}

void FPixel2DTileMapEditor::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(TileMapBeingEdited);
}

void FPixel2DTileMapEditor::ExtendMenu()
{
}

void FPixel2DTileMapEditor::ExtendToolbar()
{
	struct Local
	{
		static void FillToolbar(FToolBarBuilder& ToolbarBuilder)
		{
			const FPixel2DTileMapEditorCommands& Commands = FPixel2DTileMapEditorCommands::Get();
			ToolbarBuilder.AddToolBarButton(Commands.SetShowTileGrid);
			ToolbarBuilder.AddToolBarButton(Commands.SetShowLayerGrid);
			ToolbarBuilder.AddToolBarButton(Commands.SetShowTileMapStats);
		}
	};

	TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);

	ToolbarExtender->AddToolBarExtension(
		"Asset",
		EExtensionHook::After,
		ViewportPtr->GetCommandList(),
		FToolBarExtensionDelegate::CreateStatic(&Local::FillToolbar)
		);

	AddToolbarExtender(ToolbarExtender);
}

void FPixel2DTileMapEditor::SetTileMapBeingEdited(UPixel2DTileMap* NewTileMap)
{
	if ((NewTileMap != TileMapBeingEdited) && (NewTileMap != nullptr))
	{
		UPixel2DTileMap* OldTileMap = TileMapBeingEdited;
		TileMapBeingEdited = NewTileMap;

		// Let the viewport know that we are editing something different
		ViewportPtr->NotifyTileMapBeingEditedHasChanged();

		// Let the editor know that are editing something different
		RemoveEditingObject(OldTileMap);
		AddEditingObject(NewTileMap);
	}
}

//////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
