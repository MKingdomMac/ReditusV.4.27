// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPixel2DTileLayerList.h"
#include "PaperTileLayer.h"
#include "UObject/PropertyPortFlags.h"
#include "Misc/NotifyHook.h"
#include "Framework/MultiBox/MultiBoxDefs.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Views/SListView.h"
#include "Exporters/Exporter.h"
#include "Editor.h"
#include "Pixel2DTileMapComponent.h"
#include "SPixel2DTileLayerItem.h"
#include "Pixel2DMapStyle.h"
#include "HAL/PlatformApplicationMisc.h"

#include "ScopedTransaction.h"

#include "Pixel2DTileMapEditorCommands.h"
#include "Framework/Commands/GenericCommands.h"

#include "UnrealExporter.h"
#include "Factories.h"

#define LOCTEXT_NAMESPACE "Pixel2D"

//////////////////////////////////////////////////////////////////////////
// FLayerTextFactory

// Text object factory for pasting layers
struct FLayerTextFactory : public FCustomizableTextObjectFactory
{
public:
	TArray<UPaperTileLayer*> CreatedLayers;
public:
	FLayerTextFactory()
		: FCustomizableTextObjectFactory(GWarn)
	{
	}

	// FCustomizableTextObjectFactory interface
	virtual bool CanCreateClass(UClass* ObjectClass, bool& bOmitSubObjs) const override
	{
		// Only allow layers to be created
		return ObjectClass->IsChildOf(UPaperTileLayer::StaticClass());
	}

	virtual void ProcessConstructedObject(UObject* NewObject) override
	{
		CreatedLayers.Add(CastChecked<UPaperTileLayer>(NewObject));
	}
	// End of FCustomizableTextObjectFactory interface
};

//////////////////////////////////////////////////////////////////////////
// SPixel2DTileLayerList

void SPixel2DTileLayerList::Construct(const FArguments& InArgs, UPixel2DTileMap* InTileMap, FNotifyHook* InNotifyHook, TSharedPtr<class FUICommandList> InParentCommandList)
{
	OnSelectedLayerChanged = InArgs._OnSelectedLayerChanged;
	TileMapPtr = InTileMap;
	NotifyHook = InNotifyHook;

	FPixel2DTileMapEditorCommands::Register();
	FGenericCommands::Register();
	const FPixel2DTileMapEditorCommands& TileMapCommands = FPixel2DTileMapEditorCommands::Get();
	const FGenericCommands& GenericCommands = FGenericCommands::Get();

	CommandList = MakeShareable(new FUICommandList);
	InParentCommandList->Append(CommandList.ToSharedRef());

	CommandList->MapAction(
		TileMapCommands.AddNewLayerAbove,
		FExecuteAction::CreateSP(this, &SPixel2DTileLayerList::AddNewLayerAbove));

	CommandList->MapAction(
		TileMapCommands.AddNewLayerBelow,
		FExecuteAction::CreateSP(this, &SPixel2DTileLayerList::AddNewLayerBelow));

	CommandList->MapAction(
		GenericCommands.Cut,
		FExecuteAction::CreateSP(this, &SPixel2DTileLayerList::CutLayer),
		FCanExecuteAction::CreateSP(this, &SPixel2DTileLayerList::CanExecuteActionNeedingSelectedLayer));

	CommandList->MapAction(
		GenericCommands.Copy,
		FExecuteAction::CreateSP(this, &SPixel2DTileLayerList::CopyLayer),
		FCanExecuteAction::CreateSP(this, &SPixel2DTileLayerList::CanExecuteActionNeedingSelectedLayer));

	CommandList->MapAction(
		GenericCommands.Paste,
		FExecuteAction::CreateSP(this, &SPixel2DTileLayerList::PasteLayerAbove),
		FCanExecuteAction::CreateSP(this, &SPixel2DTileLayerList::CanPasteLayer));

	CommandList->MapAction(
		GenericCommands.Duplicate,
		FExecuteAction::CreateSP(this, &SPixel2DTileLayerList::DuplicateLayer),
		FCanExecuteAction::CreateSP(this, &SPixel2DTileLayerList::CanExecuteActionNeedingSelectedLayer));

	CommandList->MapAction(
		GenericCommands.Delete,
		FExecuteAction::CreateSP(this, &SPixel2DTileLayerList::DeleteLayer),
		FCanExecuteAction::CreateSP(this, &SPixel2DTileLayerList::CanExecuteActionNeedingSelectedLayer));

	CommandList->MapAction(
		GenericCommands.Rename,
		FExecuteAction::CreateSP(this, &SPixel2DTileLayerList::RenameLayer),
		FCanExecuteAction::CreateSP(this, &SPixel2DTileLayerList::CanExecuteActionNeedingSelectedLayer));

	CommandList->MapAction(
		TileMapCommands.MergeLayerDown,
		FExecuteAction::CreateSP(this, &SPixel2DTileLayerList::MergeLayerDown),
		FCanExecuteAction::CreateSP(this, &SPixel2DTileLayerList::CanExecuteActionNeedingLayerBelow));

	CommandList->MapAction(
		TileMapCommands.MoveLayerUp,
		FExecuteAction::CreateSP(this, &SPixel2DTileLayerList::MoveLayerUp, /*bForceToTop=*/ false),
		FCanExecuteAction::CreateSP(this, &SPixel2DTileLayerList::CanExecuteActionNeedingLayerAbove));

	CommandList->MapAction(
		TileMapCommands.MoveLayerDown,
		FExecuteAction::CreateSP(this, &SPixel2DTileLayerList::MoveLayerDown, /*bForceToBottom=*/ false),
		FCanExecuteAction::CreateSP(this, &SPixel2DTileLayerList::CanExecuteActionNeedingLayerBelow));

	CommandList->MapAction(
		TileMapCommands.MoveLayerToTop,
		FExecuteAction::CreateSP(this, &SPixel2DTileLayerList::MoveLayerUp, /*bForceToTop=*/ true),
		FCanExecuteAction::CreateSP(this, &SPixel2DTileLayerList::CanExecuteActionNeedingLayerAbove));

	CommandList->MapAction(
		TileMapCommands.MoveLayerToBottom,
		FExecuteAction::CreateSP(this, &SPixel2DTileLayerList::MoveLayerDown, /*bForceToBottom=*/ true),
		FCanExecuteAction::CreateSP(this, &SPixel2DTileLayerList::CanExecuteActionNeedingLayerBelow));

	CommandList->MapAction(
		TileMapCommands.SelectLayerAbove,
		FExecuteAction::CreateSP(this, &SPixel2DTileLayerList::SelectLayerAbove, /*bTopmost=*/ false));

	CommandList->MapAction(
		TileMapCommands.SelectLayerBelow,
		FExecuteAction::CreateSP(this, &SPixel2DTileLayerList::SelectLayerBelow, /*bBottommost=*/ false));

	//
	FToolBarBuilder ToolbarBuilder(CommandList, FMultiBoxCustomization("TileLayerBrowserToolbar"), TSharedPtr<FExtender>(), /*InForceSmallIcons=*/ true);
	ToolbarBuilder.SetLabelVisibility(EVisibility::Collapsed);

	ToolbarBuilder.AddToolBarButton(TileMapCommands.AddNewLayerAbove);
	ToolbarBuilder.AddToolBarButton(TileMapCommands.MoveLayerUp);
	ToolbarBuilder.AddToolBarButton(TileMapCommands.MoveLayerDown);

	FSlateIcon DuplicateIcon(FPixel2DMapStyle::GetStyleSetName(), "TileMapEditor.DuplicateLayer");
	ToolbarBuilder.AddToolBarButton(GenericCommands.Duplicate, NAME_None, TAttribute<FText>(), TAttribute<FText>(), DuplicateIcon);

	FSlateIcon DeleteIcon(FPixel2DMapStyle::GetStyleSetName(), "TileMapEditor.DeleteLayer");
	ToolbarBuilder.AddToolBarButton(GenericCommands.Delete, NAME_None, TAttribute<FText>(), TAttribute<FText>(), DeleteIcon);

	TSharedRef<SWidget> Toolbar = ToolbarBuilder.MakeWidget();

	ListViewWidget = SNew(SPaperLayerListView)
		.SelectionMode(ESelectionMode::Single)
		.ClearSelectionOnClick(false)
		.ListItemsSource(&MirrorList)
		.OnSelectionChanged(this, &SPixel2DTileLayerList::OnSelectionChanged)
		.OnGenerateRow(this, &SPixel2DTileLayerList::OnGenerateLayerListRow)
		.OnContextMenuOpening(this, &SPixel2DTileLayerList::OnConstructContextMenu);

	RefreshMirrorList();

	// Restore the selection
	InTileMap->ValidateSelectedLayerIndex();
	if (InTileMap->TileLayers.IsValidIndex(InTileMap->SelectedLayerIndex))
	{
		UPaperTileLayer* SelectedLayer = InTileMap->TileLayers[InTileMap->SelectedLayerIndex];
		SetSelectedLayer(SelectedLayer);
	}

	ChildSlot
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		[
			SNew(SBox)
			.HeightOverride(115.0f)
			[
				ListViewWidget.ToSharedRef()
			]
		]
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			Toolbar
		]
	];

	GEditor->RegisterForUndo(this);
}

TSharedRef<ITableRow> SPixel2DTileLayerList::OnGenerateLayerListRow(FMirrorEntry Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	typedef STableRow<FMirrorEntry> RowType;

	TSharedRef<RowType> NewRow = SNew(RowType, OwnerTable)
		.Style(&FPixel2DMapStyle::Get()->GetWidgetStyle<FTableRowStyle>("TileMapEditor.LayerBrowser.TableViewRow"));

	FIsSelected IsSelectedDelegate = FIsSelected::CreateSP(NewRow, &RowType::IsSelectedExclusively);
	NewRow->SetContent(SNew(SPixel2DTileLayerItem, *Item, TileMapPtr.Get(), IsSelectedDelegate));

	return NewRow;
}

UPaperTileLayer* SPixel2DTileLayerList::GetSelectedLayer() const
{
	if (UPaperTileMap* TileMap = TileMapPtr.Get())
	{
		if (ListViewWidget->GetNumItemsSelected() > 0)
		{
			FMirrorEntry SelectedItem = ListViewWidget->GetSelectedItems()[0];
			const int32 SelectedIndex = *SelectedItem;
			if (TileMap->TileLayers.IsValidIndex(SelectedIndex))
			{
				return TileMap->TileLayers[SelectedIndex];
			}
		}
	}

	return nullptr;
}

FText SPixel2DTileLayerList::GenerateDuplicatedLayerName(const FString& InputNameRaw, UPaperTileMap* TileMap)
{
	// Create a set of existing names
	bool bFoundName = false;
	TSet<FString> ExistingNames;
	for (UPaperTileLayer* ExistingLayer : TileMap->TileLayers)
	{
		const FString LayerName = ExistingLayer->LayerName.ToString();
		ExistingNames.Add(LayerName);

		if (LayerName == InputNameRaw)
		{
			bFoundName = true;
		}
	}

	// If the name doesn't already exist, then we're done (can happen when pasting a cut layer, etc...)
	if (!bFoundName)
	{
		return FText::FromString(InputNameRaw);
	}

	FString BaseName = InputNameRaw;
	int32 TestIndex = 0;
	bool bAddNumber = false;

	// See if this is the result of a previous duplication operation, and change the desired name accordingly
	int32 SpaceIndex;
	if (InputNameRaw.FindLastChar(' ', /*out*/ SpaceIndex))
	{
		FString PossibleDuplicationSuffix = InputNameRaw.Mid(SpaceIndex + 1);

		if (PossibleDuplicationSuffix == TEXT("copy"))
		{
			bAddNumber = true;
			BaseName = InputNameRaw.Left(SpaceIndex);
			TestIndex = 2;
		}
		else
		{
			int32 ExistingIndex = FCString::Atoi(*PossibleDuplicationSuffix);

			const FString TestSuffix = FString::Printf(TEXT(" copy %d"), ExistingIndex);

			if (InputNameRaw.EndsWith(TestSuffix))
			{
				bAddNumber = true;
				BaseName = InputNameRaw.Left(InputNameRaw.Len() - TestSuffix.Len());
				TestIndex = ExistingIndex + 1;
			}
		}
	}

	// Find a good name
	FString TestLayerName = BaseName + TEXT(" copy");

	if (bAddNumber || ExistingNames.Contains(TestLayerName))
	{
		do
		{
			TestLayerName = FString::Printf(TEXT("%s copy %d"), *BaseName, TestIndex++);
		} while (ExistingNames.Contains(TestLayerName));
	}

	return FText::FromString(TestLayerName);
}

UPaperTileLayer* SPixel2DTileLayerList::AddLayer(int32 InsertionIndex)
{
	UPaperTileLayer* NewLayer = nullptr;

	if (UPaperTileMap* TileMap = TileMapPtr.Get())
	{
		const FScopedTransaction Transaction(LOCTEXT("TileMapAddLayerTransaction", "Add New Layer"));
		TileMap->SetFlags(RF_Transactional);
		TileMap->Modify();

		NewLayer = TileMap->AddNewLayer(InsertionIndex);

		PostEditNotfications();

		// Change the selection set to select it
		SetSelectedLayer(NewLayer);
	}

	return NewLayer;
}

void SPixel2DTileLayerList::ChangeLayerOrdering(int32 OldIndex, int32 NewIndex)
{
	if (UPaperTileMap* TileMap = TileMapPtr.Get())
	{
		if (TileMap->TileLayers.IsValidIndex(OldIndex) && TileMap->TileLayers.IsValidIndex(NewIndex))
		{
			const FScopedTransaction Transaction(LOCTEXT("TileMapReorderLayerTransaction", "Reorder Layer"));
			TileMap->SetFlags(RF_Transactional);
			TileMap->Modify();

			UPaperTileLayer* LayerToMove = TileMap->TileLayers[OldIndex];
			TileMap->TileLayers.RemoveAt(OldIndex);
			TileMap->TileLayers.Insert(LayerToMove, NewIndex);

			if (TileMap->SelectedLayerIndex == OldIndex)
			{
				TileMap->SelectedLayerIndex = NewIndex;
				SetSelectedLayer(LayerToMove);
			}

			PostEditNotfications();
		}
	}
}

void SPixel2DTileLayerList::AddNewLayerAbove()
{
	AddLayer(GetSelectionIndex());
}

void SPixel2DTileLayerList::AddNewLayerBelow()
{
	AddLayer(GetSelectionIndex() + 1);
}

int32 SPixel2DTileLayerList::GetSelectionIndex() const
{
	int32 SelectionIndex = INDEX_NONE;

	if (UPaperTileMap* TileMap = TileMapPtr.Get())
	{
		if (UPaperTileLayer* SelectedLayer = GetSelectedLayer())
		{
			TileMap->TileLayers.Find(SelectedLayer, /*out*/ SelectionIndex);
		}
		else
		{
			SelectionIndex = TileMap->TileLayers.Num() - 1;
		}
	}

	return SelectionIndex;
}

void SPixel2DTileLayerList::DeleteSelectedLayerWithNoTransaction()
{
	if (UPixel2DTileMap* TileMap = TileMapPtr.Get())
	{
		const int32 DeleteIndex = GetSelectionIndex();
		if (DeleteIndex != INDEX_NONE)
		{
			TileMap->DeleteLayer(DeleteIndex);

			PostEditNotfications();

			// Select the item below the one that just got deleted
			const int32 NewSelectionIndex = FMath::Min<int32>(DeleteIndex, TileMap->TileLayers.Num() - 1);
			if (TileMap->TileLayers.IsValidIndex(NewSelectionIndex))
			{
				SetSelectedLayer(TileMap->TileLayers[NewSelectionIndex]);
			}
		}
	}
}

void SPixel2DTileLayerList::DeleteLayer()
{
	if (UPaperTileMap* TileMap = TileMapPtr.Get())
	{
		const FScopedTransaction Transaction(LOCTEXT("TileMapDeleteLayerTransaction", "Delete Layer"));
		TileMap->SetFlags(RF_Transactional);
		TileMap->Modify();

		DeleteSelectedLayerWithNoTransaction();
	}
}

void SPixel2DTileLayerList::RenameLayer()
{
	if (UPaperTileMap* TileMap = TileMapPtr.Get())
	{
		const int32 RenameIndex = GetSelectionIndex();
		if (MirrorList.IsValidIndex(RenameIndex))
		{
			TSharedPtr<ITableRow> LayerRowWidget = ListViewWidget->WidgetFromItem(MirrorList[RenameIndex]);
			if (LayerRowWidget.IsValid())
			{
				TSharedPtr<SWidget> RowContent = LayerRowWidget->GetContent();
				if (RowContent.IsValid())
				{
					TSharedPtr<SPixel2DTileLayerItem> LayerWidget = StaticCastSharedPtr<SPixel2DTileLayerItem>(RowContent);
					LayerWidget->BeginEditingName();
				}
			}
		}
	}
}


void SPixel2DTileLayerList::DuplicateLayer()
{
	if (UPixel2DTileMap* TileMap = TileMapPtr.Get())
	{
		const int32 DuplicateIndex = GetSelectionIndex();
		if (DuplicateIndex != INDEX_NONE)
		{
			const FScopedTransaction Transaction(LOCTEXT("TileMapDuplicateLayerTransaction", "Duplicate Layer"));
			TileMap->SetFlags(RF_Transactional);
			TileMap->Modify();

			UPaperTileLayer* NewLayer = TileMap->DuplicateLayer(DuplicateIndex);
			NewLayer->LayerName = GenerateDuplicatedLayerName(NewLayer->LayerName.ToString(), TileMap);

			PostEditNotfications();

			// Select the duplicated layer
			SetSelectedLayer(NewLayer);
		}
	}
}

void SPixel2DTileLayerList::MergeLayerDown()
{
	if (UPixel2DTileMap* TileMap = TileMapPtr.Get())
	{
		const int32 SourceIndex = GetSelectionIndex();
		const int32 TargetIndex = SourceIndex + 1;
		if ((SourceIndex != INDEX_NONE) && (TargetIndex != INDEX_NONE))
		{
			const FScopedTransaction Transaction(LOCTEXT("TileMapMergeLayerDownTransaction", "Merge Layer Down"));
			TileMap->SetFlags(RF_Transactional);
			TileMap->Modify();

			UPaperTileLayer* SourceLayer = TileMap->TileLayers[SourceIndex];
			UPaperTileLayer* TargetLayer = TileMap->TileLayers[TargetIndex];

			TargetLayer->SetFlags(RF_Transactional);
			TargetLayer->Modify();

			// Copy the non-empty tiles from the source to the target layer
			for (int32 Y = 0; Y < SourceLayer->GetLayerHeight(); ++Y)
			{
				for (int32 X = 0; X < SourceLayer->GetLayerWidth(); ++X)
				{
					FPaperTileInfo TileInfo = SourceLayer->GetCell(X, Y);
					if (TileInfo.IsValid())
					{
						TargetLayer->SetCell(X, Y, TileInfo);
					}
				}
			}

			TileMap->ForEachFlipbook([&](FPixelPaintedFlipbook& Entry)
				{
					if (Entry.Layer == SourceLayer)
					{
						Entry.Layer = TargetLayer;
					}
				});

			// Remove the source layer
			TileMap->TileLayers.RemoveAt(SourceIndex);

			// Update viewers
			PostEditNotfications();
		}
	}
}

void SPixel2DTileLayerList::MoveLayerUp(bool bForceToTop)
{
	const int32 SelectedIndex = GetSelectionIndex();
	const int32 NewIndex = bForceToTop ? 0 : (SelectedIndex - 1);
	ChangeLayerOrdering(SelectedIndex, NewIndex);
}

void SPixel2DTileLayerList::MoveLayerDown(bool bForceToBottom)
{
	const int32 SelectedIndex = GetSelectionIndex();
	const int32 NewIndex = bForceToBottom ? (GetNumLayers() - 1) : (SelectedIndex + 1);
	ChangeLayerOrdering(SelectedIndex, NewIndex);
}

void SPixel2DTileLayerList::SelectLayerAbove(bool bTopmost)
{
	const int32 SelectedIndex = GetSelectionIndex();
	const int32 NumLayers = GetNumLayers();
	const int32 NewIndex = bTopmost ? 0 : ((NumLayers + SelectedIndex - 1) % NumLayers);
	SetSelectedLayerIndex(NewIndex);
}

void SPixel2DTileLayerList::SelectLayerBelow(bool bBottommost)
{
	const int32 SelectedIndex = GetSelectionIndex();
	const int32 NumLayers = GetNumLayers();
	const int32 NewIndex = bBottommost ? (NumLayers - 1) : ((SelectedIndex + 1) % NumLayers);
	SetSelectedLayerIndex(NewIndex);
}

void SPixel2DTileLayerList::CutLayer()
{
	CopyLayer();

	if (UPaperTileMap* TileMap = TileMapPtr.Get())
	{
		const FScopedTransaction Transaction(LOCTEXT("TileMapCutLayerTransaction", "Cut Layer"));
		TileMap->SetFlags(RF_Transactional);
		TileMap->Modify();

		DeleteSelectedLayerWithNoTransaction();
	}
}

void SPixel2DTileLayerList::CopyLayer()
{
	if (UPaperTileLayer* SelectedLayer = GetSelectedLayer())
	{
		UnMarkAllObjects(EObjectMark(OBJECTMARK_TagExp | OBJECTMARK_TagImp));
		FStringOutputDevice ExportArchive;
		const FExportObjectInnerContext Context;
		UExporter::ExportToOutputDevice(&Context, SelectedLayer, nullptr, ExportArchive, TEXT("copy"), 0, PPF_Copy, false, nullptr);

		FPlatformApplicationMisc::ClipboardCopy(*ExportArchive);
	}
}

void SPixel2DTileLayerList::PasteLayerAbove()
{
	if (UPaperTileMap* TileMap = TileMapPtr.Get())
	{
		FString ClipboardContent;
		FPlatformApplicationMisc::ClipboardPaste(ClipboardContent);

		if (!ClipboardContent.IsEmpty())
		{
			const FScopedTransaction Transaction(LOCTEXT("TileMapPasteLayerTransaction", "Paste Layer"));
			TileMap->SetFlags(RF_Transactional);
			TileMap->Modify();

			// Turn the text buffer into objects
			FLayerTextFactory Factory;
			Factory.ProcessBuffer(TileMap, RF_Transactional, ClipboardContent);

			// Add them to the map and select them (there will currently only ever be 0 or 1)
			for (UPaperTileLayer* NewLayer : Factory.CreatedLayers)
			{
				NewLayer->LayerName = GenerateDuplicatedLayerName(NewLayer->LayerName.ToString(), TileMap);
				TileMap->AddExistingLayer(NewLayer, GetSelectionIndex());
				PostEditNotfications();
				SetSelectedLayer(NewLayer);
			}
		}
	}
}

bool SPixel2DTileLayerList::CanPasteLayer() const
{
	FString ClipboardContent;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardContent);

	return !ClipboardContent.IsEmpty();
}

void SPixel2DTileLayerList::SetSelectedLayerIndex(int32 NewIndex)
{
	if (UPaperTileMap* TileMap = TileMapPtr.Get())
	{
		if (TileMap->TileLayers.IsValidIndex(NewIndex))
		{
			SetSelectedLayer(TileMap->TileLayers[NewIndex]);
			PostEditNotfications();
		}
	}
}

int32 SPixel2DTileLayerList::GetNumLayers() const
{
	if (UPaperTileMap* TileMap = TileMapPtr.Get())
	{
		return TileMap->TileLayers.Num();
	}
	return 0;
}

bool SPixel2DTileLayerList::CanExecuteActionNeedingLayerAbove() const
{
	return GetSelectionIndex() > 0;
}

bool SPixel2DTileLayerList::CanExecuteActionNeedingLayerBelow() const
{
	const int32 SelectedLayer = GetSelectionIndex();
	return (SelectedLayer != INDEX_NONE) && (SelectedLayer + 1 < GetNumLayers());
}

bool SPixel2DTileLayerList::CanExecuteActionNeedingSelectedLayer() const
{
	return GetSelectionIndex() != INDEX_NONE;
}

void SPixel2DTileLayerList::SetSelectedLayer(UPaperTileLayer* SelectedLayer)
{
	if (UPaperTileMap* TileMap = TileMapPtr.Get())
	{
		int32 NewIndex;
		if (TileMap->TileLayers.Find(SelectedLayer, /*out*/ NewIndex))
		{
			if (MirrorList.IsValidIndex(NewIndex))
			{
				ListViewWidget->SetSelection(MirrorList[NewIndex]);
			}
		}
	}
}

void SPixel2DTileLayerList::OnSelectionChanged(FMirrorEntry ItemChangingState, ESelectInfo::Type SelectInfo)
{
	if (UPaperTileMap* TileMap = TileMapPtr.Get())
	{
		TileMap->SelectedLayerIndex = GetSelectionIndex();

		PostEditNotfications();
	}
}

TSharedPtr<SWidget> SPixel2DTileLayerList::OnConstructContextMenu()
{
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, CommandList);

	const FPixel2DTileMapEditorCommands& TileMapCommands = FPixel2DTileMapEditorCommands::Get();
	const FGenericCommands& GenericCommands = FGenericCommands::Get();

	FSlateIcon DummyIcon(NAME_None, NAME_None);

	MenuBuilder.BeginSection("BasicOperations", LOCTEXT("BasicOperationsHeader", "Layer actions"));
 	{
		MenuBuilder.AddMenuEntry(GenericCommands.Cut, NAME_None, TAttribute<FText>(), TAttribute<FText>(), DummyIcon);
		MenuBuilder.AddMenuEntry(GenericCommands.Copy, NAME_None, TAttribute<FText>(), TAttribute<FText>(), DummyIcon);
		MenuBuilder.AddMenuEntry(GenericCommands.Paste, NAME_None, TAttribute<FText>(), TAttribute<FText>(), DummyIcon);
		MenuBuilder.AddMenuEntry(GenericCommands.Duplicate, NAME_None, TAttribute<FText>(), TAttribute<FText>(), DummyIcon);
		MenuBuilder.AddMenuEntry(GenericCommands.Delete, NAME_None, TAttribute<FText>(), TAttribute<FText>(), DummyIcon);
		MenuBuilder.AddMenuEntry(GenericCommands.Rename, NAME_None, TAttribute<FText>(), TAttribute<FText>(), DummyIcon);
		MenuBuilder.AddMenuEntry(TileMapCommands.MergeLayerDown, NAME_None, TAttribute<FText>(), TAttribute<FText>(), DummyIcon);
		MenuBuilder.AddMenuSeparator();
		MenuBuilder.AddMenuEntry(TileMapCommands.SelectLayerAbove, NAME_None, TAttribute<FText>(), TAttribute<FText>(), DummyIcon);
		MenuBuilder.AddMenuEntry(TileMapCommands.SelectLayerBelow, NAME_None, TAttribute<FText>(), TAttribute<FText>(), DummyIcon);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("OrderingOperations", LOCTEXT("OrderingOperationsHeader", "Order actions"));
	{
		MenuBuilder.AddMenuEntry(TileMapCommands.MoveLayerToTop, NAME_None, TAttribute<FText>(), TAttribute<FText>(), DummyIcon);
		MenuBuilder.AddMenuEntry(TileMapCommands.MoveLayerUp, NAME_None, TAttribute<FText>(), TAttribute<FText>(), DummyIcon);
		MenuBuilder.AddMenuEntry(TileMapCommands.MoveLayerDown, NAME_None, TAttribute<FText>(), TAttribute<FText>(), DummyIcon);
		MenuBuilder.AddMenuEntry(TileMapCommands.MoveLayerToBottom, NAME_None, TAttribute<FText>(), TAttribute<FText>(), DummyIcon);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void SPixel2DTileLayerList::PostEditNotfications()
{
	RefreshMirrorList();

	if (UPaperTileMap* TileMap = TileMapPtr.Get())
	{
		TileMap->PostEditChange();
	}

	if (NotifyHook != nullptr)
	{
		FProperty* TileMapProperty = FindFieldChecked<FProperty>(UPixel2DTileMapComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(UPixel2DTileMapComponent, TileMap));
		NotifyHook->NotifyPreChange(TileMapProperty);
		NotifyHook->NotifyPostChange(FPropertyChangedEvent(TileMapProperty), TileMapProperty);
	}

	OnSelectedLayerChanged.Execute();
}

void SPixel2DTileLayerList::RefreshMirrorList()
{
	if (UPaperTileMap* TileMap = TileMapPtr.Get())
	{
		const int32 NumEntriesToAdd = TileMap->TileLayers.Num() - MirrorList.Num();
		if (NumEntriesToAdd < 0)
		{
			MirrorList.RemoveAt(TileMap->TileLayers.Num(), -NumEntriesToAdd);
		}
		else if (NumEntriesToAdd > 0)
		{
			for (int32 Count = 0; Count < NumEntriesToAdd; ++Count)
			{
				TSharedPtr<int32> NewEntry = MakeShareable(new int32);
				*NewEntry = MirrorList.Num();
				MirrorList.Add(NewEntry);
			}
		}
	}
	else
	{
		MirrorList.Empty();
	}

	ListViewWidget->RequestListRefresh();
}

void SPixel2DTileLayerList::PostUndo(bool bSuccess)
{
	RefreshMirrorList();
}

void SPixel2DTileLayerList::PostRedo(bool bSuccess)
{
	RefreshMirrorList();
}

SPixel2DTileLayerList::~SPixel2DTileLayerList()
{
	GEditor->UnregisterForUndo(this);
}

//////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
