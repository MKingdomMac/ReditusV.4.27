// Copyright Epic Games, Inc. All Rights Reserved.

#include "Pixel2DTileMapEdModeToolkit.h"
#include "SPixel2DTileMapFlipbookBrowser.h"

#include "Framework/MultiBox/MultiBoxDefs.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SButton.h"
#include "EditorStyleSet.h"
#include "Pixel2DTileMapComponent.h"
#include "Pixel2DTileMap.h"

#include "Toolkits/AssetEditorToolkit.h"
#include "TileSet/SPixel2DTileSetSelectorViewport.h"
#include "WorkflowOrientedApp/SContentReference.h"
#include "Pixel2DMapStyle.h"
#include "Pixel2DTileMapEditorCommands.h"
#include "SAssetDropTarget.h"

#define LOCTEXT_NAMESPACE "Pixel2D"

//////////////////////////////////////////////////////////////////////////
// FPixel2DTileMapEdModeToolkit

FPixel2DTileMapEdModeToolkit::FPixel2DTileMapEdModeToolkit(class FPixel2DEdModeTileMap* InOwningMode)
{
	TileMapEditor = InOwningMode;
}

void FPixel2DTileMapEdModeToolkit::RegisterTabSpawners(const TSharedRef<class FTabManager>& TabManager)
{
}

void FPixel2DTileMapEdModeToolkit::UnregisterTabSpawners(const TSharedRef<class FTabManager>& TabManager)
{
}

FName FPixel2DTileMapEdModeToolkit::GetToolkitFName() const
{
	return FName("Pixel2DTileMapToolkit");
}

FText FPixel2DTileMapEdModeToolkit::GetBaseToolkitName() const
{
	return LOCTEXT( "TileMapAppLabel", "Pixel2D Tile Map Editor" );
}

FText FPixel2DTileMapEdModeToolkit::GetToolkitName() const
{
	if (CurrentTileSetPtr.IsValid())
	{
		return FText::FromString(CurrentTileSetPtr->GetName());
	}
	return GetBaseToolkitName();
}

FText FPixel2DTileMapEdModeToolkit::GetToolkitToolTipText() const
{
	if (CurrentTileSetPtr.IsValid())
	{
		return FAssetEditorToolkit::GetToolTipTextForObject(CurrentTileSetPtr.Get());
	}
	return GetBaseToolkitName();
}

FEdMode* FPixel2DTileMapEdModeToolkit::GetEditorMode() const
{
	return TileMapEditor;
}

TSharedPtr<SWidget> FPixel2DTileMapEdModeToolkit::GetInlineContent() const
{
	return MyWidget;
}

void FPixel2DTileMapEdModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost)
{
	const float ContentRefWidth = 140.0f;

	BindCommands();

	// Try to determine a good default tile set based on the current selection set
	if (UPixel2DTileMapComponent* TargetComponent = TileMapEditor->FindSelectedComponent())
	{
		if (TargetComponent->TileMap != nullptr)
		{
			CurrentTileSetPtr = TargetComponent->TileMap->SelectedTileSet.Get();
		}
	}

	TileSetPalette = SNew(SPixel2DTileSetSelectorViewport, CurrentTileSetPtr.Get(), TileMapEditor);
	FlipbookBrowserWidget = SNew(SPixel2DTileMapFlipbookBrowser, SharedThis(TileMapEditor));

	TSharedRef<SWidget> TileSetPaletteWidget = SNew(SOverlay)
		// The palette widget
		+SOverlay::Slot()
		[
			SNew(SAssetDropTarget)
			.OnIsAssetAcceptableForDrop(this, &FPixel2DTileMapEdModeToolkit::OnAssetDraggedOver)
			.OnAssetDropped(this, &FPixel2DTileMapEdModeToolkit::OnChangeTileSet)
			[
				TileSetPalette.ToSharedRef()
			]
		]
		// The no tile set selected warning text/button
		+SOverlay::Slot()
		.Padding(8.0f)
		.VAlign(VAlign_Bottom)
		.HAlign(HAlign_Right)
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "NoBorder")
			.Visibility(this, &FPixel2DTileMapEdModeToolkit::GetTileSetPaletteCornerTextVisibility)
			.OnClicked(this, &FPixel2DTileMapEdModeToolkit::ClickedOnTileSetPaletteCornerText)
			.Content()
			[
				SNew(STextBlock)
				.TextStyle(FPixel2DMapStyle::Get(), "TileMapEditor.TileSetPalette.NothingSelectedText")
				.Text(LOCTEXT("NoTileSetSelected", "Pick a tile set"))
				.ToolTipText(LOCTEXT("NoTileSetSelectedTooltip", "A tile set must be selected before painting the tile map.\nClick here to select one."))
			]
		];

	TileSetAssetReferenceWidget = SNew(SContentReference)
		.WidthOverride(ContentRefWidth)
		.AssetReference(this, &FPixel2DTileMapEdModeToolkit::GetCurrentTileSet)
		.OnSetReference(this, &FPixel2DTileMapEdModeToolkit::OnChangeTileSet)
		.AllowedClass(UPaperTileSet::StaticClass())
		.AllowSelectingNewAsset(true)
		.AllowClearingReference(false);

	// Create the contents of the editor mode toolkit
	MyWidget = 
		SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush( "ToolPanel.GroupBorder" ))
		.Content()
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Fill)
			.Padding(4.0f)
			[
				BuildToolBar()
			]

			+SVerticalBox::Slot()
			.FillHeight(1.0f)
			.VAlign(VAlign_Fill)
			[
				SNew(SVerticalBox)

				+SVerticalBox::Slot()
				.AutoHeight()
				.Padding(4.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Center)
					.Padding(FMargin(0.0f, 0.0f, 4.0f, 0.0f))
					[
						SNew(STextBlock)
						.Text(LOCTEXT("CurrentTileSetAssetToPaintWith", "Active Tile Set"))
					]
					+SHorizontalBox::Slot()
					.HAlign(HAlign_Right)
					.AutoWidth()
					[
						TileSetAssetReferenceWidget.ToSharedRef()
					]
				]

				+SVerticalBox::Slot()
				.FillHeight(1.0f)
				.VAlign(VAlign_Fill)
				.Padding(4.0f)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.HAlign(HAlign_Fill)
					[
						TileSetPaletteWidget
					]
				]
				+SVerticalBox::Slot()
				.FillHeight(1.0f)
				.VAlign(VAlign_Fill)
				.Padding(4.0f)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.HAlign(HAlign_Fill)
					[
						FlipbookBrowserWidget.ToSharedRef()
					]
				]
			]
		];

	FModeToolkit::Init(InitToolkitHost);
}

void FPixel2DTileMapEdModeToolkit::OnChangeTileSet(UObject* NewAsset)
{
	if (UPaperTileSet* NewTileSet = Cast<UPaperTileSet>(NewAsset))
	{
		if (CurrentTileSetPtr.Get() != NewTileSet)
		{
			CurrentTileSetPtr = NewTileSet;
			if (TileSetPalette.IsValid())
			{
				TileSetPalette->ChangeTileSet(NewTileSet);
			}

			// Save the newly selected tile set in the asset so it can be restored next time we edit it
			if (UPixel2DTileMapComponent* TargetComponent = TileMapEditor->FindSelectedComponent())
			{
				if (TargetComponent->TileMap != nullptr)
				{
					TargetComponent->TileMap->SelectedTileSet = NewTileSet;
				}
			}
		}
	}
}

UObject* FPixel2DTileMapEdModeToolkit::GetCurrentTileSet() const
{
	return CurrentTileSetPtr.Get();
}

void FPixel2DTileMapEdModeToolkit::BindCommands()
{
	FPixel2DTileMapEditorCommands::Register();
	const FPixel2DTileMapEditorCommands& Commands = FPixel2DTileMapEditorCommands::Get();

	ToolkitCommands->MapAction(
		Commands.SelectPaintTool,
		FExecuteAction::CreateSP(this, &FPixel2DTileMapEdModeToolkit::OnSelectTool, EPixel2DTileMapEditorTool::Paintbrush),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FPixel2DTileMapEdModeToolkit::IsToolSelected, EPixel2DTileMapEditorTool::Paintbrush) );
	ToolkitCommands->MapAction(
		Commands.SelectEraserTool,
		FExecuteAction::CreateSP(this, &FPixel2DTileMapEdModeToolkit::OnSelectTool, EPixel2DTileMapEditorTool::Eraser),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FPixel2DTileMapEdModeToolkit::IsToolSelected, EPixel2DTileMapEditorTool::Eraser) );
	ToolkitCommands->MapAction(
		Commands.SelectFillTool,
		FExecuteAction::CreateSP(this, &FPixel2DTileMapEdModeToolkit::OnSelectTool, EPixel2DTileMapEditorTool::PaintBucket),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FPixel2DTileMapEdModeToolkit::IsToolSelected, EPixel2DTileMapEditorTool::PaintBucket) );
	ToolkitCommands->MapAction(
		Commands.SelectEyeDropperTool,
		FExecuteAction::CreateSP(this, &FPixel2DTileMapEdModeToolkit::OnSelectTool, EPixel2DTileMapEditorTool::EyeDropper),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FPixel2DTileMapEdModeToolkit::IsToolSelected, EPixel2DTileMapEditorTool::EyeDropper),
		FIsActionButtonVisible::CreateSP(this, &FPixel2DTileMapEdModeToolkit::IsToolSelected, EPixel2DTileMapEditorTool::EyeDropper));
	ToolkitCommands->MapAction(
		Commands.SelectTerrainTool,
		FExecuteAction::CreateSP(this, &FPixel2DTileMapEdModeToolkit::OnSelectTool, EPixel2DTileMapEditorTool::TerrainBrush),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FPixel2DTileMapEdModeToolkit::IsToolSelected, EPixel2DTileMapEditorTool::TerrainBrush),
		FIsActionButtonVisible::CreateSP(this, &FPixel2DTileMapEdModeToolkit::DoesSelectedTileSetHaveTerrains));

	// Selection actions
	ToolkitCommands->MapAction(
		Commands.FlipSelectionHorizontally,
		FExecuteAction::CreateSP(TileMapEditor, &FPixel2DEdModeTileMap::FlipSelectionHorizontally),
		FCanExecuteAction::CreateSP(TileMapEditor, &FPixel2DEdModeTileMap::HasValidSelection));
	ToolkitCommands->MapAction(
		Commands.FlipSelectionVertically,
		FExecuteAction::CreateSP(TileMapEditor, &FPixel2DEdModeTileMap::FlipSelectionVertically),
		FCanExecuteAction::CreateSP(TileMapEditor, &FPixel2DEdModeTileMap::HasValidSelection));
	ToolkitCommands->MapAction(
		Commands.RotateSelectionCW,
		FExecuteAction::CreateSP(TileMapEditor, &FPixel2DEdModeTileMap::RotateSelectionCW),
		FCanExecuteAction::CreateSP(TileMapEditor, &FPixel2DEdModeTileMap::HasValidSelection));
	ToolkitCommands->MapAction(
		Commands.RotateSelectionCCW,
		FExecuteAction::CreateSP(TileMapEditor, &FPixel2DEdModeTileMap::RotateSelectionCCW),
		FCanExecuteAction::CreateSP(TileMapEditor, &FPixel2DEdModeTileMap::HasValidSelection));
}

void FPixel2DTileMapEdModeToolkit::OnSelectTool(EPixel2DTileMapEditorTool::Type NewTool)
{
	TileMapEditor->SetActiveTool(NewTool);
}

bool FPixel2DTileMapEdModeToolkit::IsToolSelected(EPixel2DTileMapEditorTool::Type QueryTool) const
{
	return (TileMapEditor->GetActiveTool() == QueryTool);
}

bool FPixel2DTileMapEdModeToolkit::DoesSelectedTileSetHaveTerrains() const
{
	if (UPaperTileSet* CurrentTileSet = CurrentTileSetPtr.Get())
	{
		return CurrentTileSet->GetNumTerrains() > 0;
	}
	else
	{
		return false;
	}
}

TSharedRef<SWidget> FPixel2DTileMapEdModeToolkit::BuildToolBar() const
{
	const FPixel2DTileMapEditorCommands& Commands = FPixel2DTileMapEditorCommands::Get();

	FToolBarBuilder SelectionFlipToolsToolbar(ToolkitCommands, FMultiBoxCustomization::None, TSharedPtr<FExtender>(), /*bForceSmallIcons=*/ true);
	{
		SelectionFlipToolsToolbar.AddToolBarButton(Commands.FlipSelectionHorizontally, NAME_None, LOCTEXT("FlipHorizontalShortLabel", "|X"));
		SelectionFlipToolsToolbar.AddToolBarButton(Commands.FlipSelectionVertically, NAME_None, LOCTEXT("FlipVerticalShortLabel", "|Y"));
		SelectionFlipToolsToolbar.AddToolBarButton(Commands.RotateSelectionCW, NAME_None, LOCTEXT("RotateClockwiseShortLabel", "CW"));
		SelectionFlipToolsToolbar.AddToolBarButton(Commands.RotateSelectionCCW, NAME_None, LOCTEXT("RotateCounterclockwiseShortLabel", "CCW"));
	}

	FToolBarBuilder ToolsToolbar(ToolkitCommands, FMultiBoxCustomization::None);
	{
		ToolsToolbar.AddToolBarButton(Commands.SelectEyeDropperTool);
		ToolsToolbar.AddToolBarButton(Commands.SelectPaintTool);
		ToolsToolbar.AddToolBarButton(Commands.SelectEraserTool);
		ToolsToolbar.AddToolBarButton(Commands.SelectFillTool);
		ToolsToolbar.AddToolBarButton(Commands.SelectTerrainTool);

		//@TODO: TileMapTerrain: Ugly styling
		FUIAction TerrainTypeDropdownAction;
		TerrainTypeDropdownAction.IsActionVisibleDelegate = FIsActionButtonVisible::CreateSP(this, &FPixel2DTileMapEdModeToolkit::DoesSelectedTileSetHaveTerrains);
		ToolsToolbar.AddComboButton(TerrainTypeDropdownAction, FOnGetContent::CreateSP(const_cast<FPixel2DTileMapEdModeToolkit*>(this), &FPixel2DTileMapEdModeToolkit::GenerateTerrainMenu));
	}

	return
		SNew(SHorizontalBox)

		+SHorizontalBox::Slot()
		.FillWidth(1.f)
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f)
		[
			SNew(SBorder)
			.Padding(0)
			.BorderImage(FEditorStyle::GetBrush("NoBorder"))
			.IsEnabled( FSlateApplication::Get().GetNormalExecutionAttribute() )
			[
				SelectionFlipToolsToolbar.MakeWidget()
			]
		]

		+SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Right)
		.Padding(4.0f, 0.0f)
		[
			SNew(SBorder)
			.Padding(0)
			.BorderImage(FEditorStyle::GetBrush("NoBorder"))
			.IsEnabled( FSlateApplication::Get().GetNormalExecutionAttribute() )
			[
				ToolsToolbar.MakeWidget()
			]
		];
}

TSharedRef<SWidget> FPixel2DTileMapEdModeToolkit::GenerateTerrainMenu()
{
	FMenuBuilder TerrainMenu(/*bInShouldCloseWindowAfterMenuSelection=*/ true, ToolkitCommands);

	if (UPaperTileSet* TileSet = CurrentTileSetPtr.Get())
	{
		const FText MenuHeading = FText::Format(LOCTEXT("TerrainMenu", "Terrain types for {0}"), FText::AsCultureInvariant(TileSet->GetName()));
		TerrainMenu.BeginSection(NAME_None, MenuHeading);

		for (int32 TerrainIndex = 0; TerrainIndex < TileSet->GetNumTerrains(); ++TerrainIndex)
		{
			FPaperTileSetTerrain TerrainInfo = TileSet->GetTerrain(TerrainIndex);

			const FText TerrainName = FText::AsCultureInvariant(TerrainInfo.TerrainName);
			const FText TerrainLabel = FText::Format(LOCTEXT("TerrainLabel", "Terrain '{0}'"), TerrainName);
			const FText TerrainTooltip = FText::Format(LOCTEXT("TerrainTooltip", "Change the active terrain brush type to '{0}'"), TerrainName);
			FUIAction TerrainSwitchAction(FExecuteAction::CreateSP(this, &FPixel2DTileMapEdModeToolkit::SetTerrainBrush, TerrainIndex));

			TerrainMenu.AddMenuEntry(TerrainLabel, TerrainTooltip, FSlateIcon(), TerrainSwitchAction);
		}

		TerrainMenu.EndSection();
	}

	return TerrainMenu.MakeWidget();
}

void FPixel2DTileMapEdModeToolkit::SetTerrainBrush(int32 NewTerrainTypeIndex)
{
	//@TODO: TileMapTerrain: Do something here...
	UE_LOG(LogInit, Warning, TEXT("Set terrain brush to %d"), NewTerrainTypeIndex);
}

EVisibility FPixel2DTileMapEdModeToolkit::GetTileSetPaletteCornerTextVisibility() const
{
	return (GetCurrentTileSet() != nullptr) ? EVisibility::Collapsed : EVisibility::Visible;
}

FReply FPixel2DTileMapEdModeToolkit::ClickedOnTileSetPaletteCornerText()
{
	TileSetAssetReferenceWidget->OpenAssetPickerMenu();

	return FReply::Handled();
}

bool FPixel2DTileMapEdModeToolkit::OnAssetDraggedOver(const UObject* InObject) const
{
	return Cast<UPaperTileSet>(InObject) != nullptr;
}

//////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
