// Copyright Epic Games, Inc. All Rights Reserved.

#include "Pixel2DTileMapDetailsCustomization.h"
#include "Layout/Margin.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SBoxPanel.h"
#include "Framework/Commands/UICommandList.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "EditorStyleSet.h"
#include "EditorModeManager.h"

#include "PropertyHandle.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailPropertyRow.h"
#include "DetailCategoryBuilder.h"
#include "PaperTileSet.h"
#include "Pixel2DEdModeTileMap.h"

#include "IDetailsView.h"
#include "IAssetTools.h"
#include "AssetToolsModule.h"
#include "Pixel2DTileMapPromotionFactory.h"
#include "Pixel2DImporterSettings.h"

#include "SPixel2DTileLayerList.h"
#include "ScopedTransaction.h"
#include "IPropertyUtilities.h"
#include "Subsystems/AssetEditorSubsystem.h"

#define LOCTEXT_NAMESPACE "Pixel2D"

//////////////////////////////////////////////////////////////////////////
// FPixel2DTileMapDetailsCustomization

TSharedRef<IDetailCustomization> FPixel2DTileMapDetailsCustomization::MakeInstance()
{
	return MakeShareable(new FPixel2DTileMapDetailsCustomization);
}

void FPixel2DTileMapDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	const TArray< TWeakObjectPtr<UObject> >& SelectedObjects = DetailLayout.GetSelectedObjects();
	MyDetailLayout = nullptr;
	
	FNotifyHook* NotifyHook = DetailLayout.GetPropertyUtilities()->GetNotifyHook();

	bool bEditingActor = false;

	DetailLayout.HideCategory(FName("Tile Map"));

	UPixel2DTileMap* TileMap = nullptr;
	UPixel2DTileMapComponent* TileComponent = nullptr;
	for (int32 ObjectIndex = 0; ObjectIndex < SelectedObjects.Num(); ++ObjectIndex)
	{
		UObject* TestObject = SelectedObjects[ObjectIndex].Get();
		if (AActor* CurrentActor = Cast<AActor>(TestObject))
		{
			if (UPixel2DTileMapComponent* CurrentComponent = CurrentActor->FindComponentByClass<UPixel2DTileMapComponent>())
			{
				bEditingActor = true;
				TileComponent = CurrentComponent;
				TileMap = CurrentComponent->GetTileMap();
				break;
			}
		}
		else if (UPixel2DTileMapComponent* TestComponent = Cast<UPixel2DTileMapComponent>(TestObject))
		{
			TileComponent = TestComponent;
			TileMap = TestComponent->GetTileMap();
			break;
		}
		else if (UPixel2DTileMap* TestTileMap = Cast<UPixel2DTileMap>(TestObject))
		{
			TileMap = TestTileMap;
			break;
		}
	}
	TileMapPtr = TileMap;
	TileMapComponentPtr = TileComponent;

	// Make sure the Tile Map category is right below the Transform
	IDetailCategoryBuilder& TileMapCategory = DetailLayout.EditCategory("Pixel Tile Map", FText::GetEmpty(), ECategoryPriority::Important);

	// Add the 'instanced' versus 'asset' indicator to the tile map header
	TileMapCategory.HeaderContent
	(
		SNew(SBox)
		.HAlign(HAlign_Right)
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.Padding(FMargin(5.0f, 0.0f))
			.AutoWidth()
			[
				SNew(STextBlock)
				.Font(FEditorStyle::GetFontStyle("TinyText"))
				.Text_Lambda([this] { return IsInstanced() ? LOCTEXT("Instanced", "Instanced") : LOCTEXT("Asset", "Asset"); })
				.ToolTipText(LOCTEXT("InstancedVersusAssetTooltip", "Tile map components can either own a unique tile map instance, or reference a shareable tile map asset"))
			]
		]
	);


	TAttribute<EVisibility> InternalInstanceVis = TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FPixel2DTileMapDetailsCustomization::GetVisibilityForInstancedOnlyProperties));

	TSharedRef<SWrapBox> ButtonBox = SNew(SWrapBox).UseAllottedSize(true);

	const float MinButtonSize = 120.0f;
	const FMargin ButtonPadding(0.0f, 2.0f, 2.0f, 0.0f);

	// Edit tile map button
	ButtonBox->AddSlot()
	.Padding(ButtonPadding)
	[
		SNew(SBox)
		.MinDesiredWidth(MinButtonSize)
		[
			SNew(SButton)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			.OnClicked(this, &FPixel2DTileMapDetailsCustomization::EnterTileMapEditingMode)
			.Visibility(this, &FPixel2DTileMapDetailsCustomization::GetNonEditModeVisibility)
			.IsEnabled(this, &FPixel2DTileMapDetailsCustomization::GetIsEditModeEnabled)
			.Text(LOCTEXT("EditAsset", "Edit Map"))
			.ToolTipText(LOCTEXT("EditAssetToolTip", "Edit this tile map"))
		]
	];

	// Create new tile map button
	ButtonBox->AddSlot()
	.Padding(ButtonPadding)
	[
		SNew(SBox)
		.MinDesiredWidth(MinButtonSize)
		[
			SNew(SButton)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			.OnClicked(this, &FPixel2DTileMapDetailsCustomization::OnNewButtonClicked)
			.Visibility(this, &FPixel2DTileMapDetailsCustomization::GetNewButtonVisiblity)
			.Text(LOCTEXT("CreateNewInstancedMap", "New Empty Map"))
			.ToolTipText(LOCTEXT("CreateNewInstancedMapToolTip", "Create a new (instanced) tile map"))
		]
	];

	// Promote to asset button
	ButtonBox->AddSlot()
	.Padding(ButtonPadding)
	[
		SNew(SBox)
		.MinDesiredWidth(MinButtonSize)
		[
			SNew(SButton)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			.OnClicked(this, &FPixel2DTileMapDetailsCustomization::OnPromoteToAssetButtonClicked)
			.Visibility(InternalInstanceVis)
			.Text(LOCTEXT("PromoteToAsset", "Promote To Asset"))
			.ToolTipText(LOCTEXT("PromoteToAssetToolTip", "Save this tile map as a reusable asset"))
		]
	];

	// Convert to instance button
	ButtonBox->AddSlot()
	.Padding(ButtonPadding)
	[
		SNew(SBox)
		.MinDesiredWidth(MinButtonSize)
		[
			SNew(SButton)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			.OnClicked(this, &FPixel2DTileMapDetailsCustomization::OnMakeInstanceFromAssetButtonClicked)
			.Visibility(this, &FPixel2DTileMapDetailsCustomization::GetVisibilityForMakeIntoInstance)
			.Text(LOCTEXT("ConvertToInstance", "Convert To Instance"))
			.ToolTipText(LOCTEXT("ConvertToInstanceToolTip", "Copy the asset referenced by this tile map component into a unique instance that can be locally edited"))
		]
	];

	if (TileComponent != nullptr)
	{
		TileMapCategory
		.AddCustomRow(LOCTEXT( "TileMapInstancingControlsSearchText", "Edit Map New Empty Map Promote Asset"))
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				ButtonBox
			]
		];

		TileMapCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UPixel2DTileMapComponent, TileMap));
	}

	// Try to get the hosting command list from the details view
	TSharedPtr<FUICommandList> CommandList = DetailLayout.GetDetailsView()->GetHostCommandList();
	if (!CommandList.IsValid())
	{
		CommandList = MakeShareable(new FUICommandList);
	}

	// Add the layer browser
	if (TileMap != nullptr)
	{
		TAttribute<EVisibility> LayerBrowserVis;
		LayerBrowserVis.Set(EVisibility::Visible);
		if (TileComponent != nullptr)
		{
			LayerBrowserVis = InternalInstanceVis;
		}

		FText TileLayerListText = LOCTEXT("TileLayerList", "Tile layer list");
		TileMapCategory.AddCustomRow(TileLayerListText)
		.Visibility(LayerBrowserVis)
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Font(DetailLayout.GetDetailFont())
				.Text(TileLayerListText)
			]
			+SVerticalBox::Slot()
			[
				SNew(SPixel2DTileLayerList, TileMap, NotifyHook, CommandList)
				.OnSelectedLayerChanged(this, &FPixel2DTileMapDetailsCustomization::OnSelectedLayerChanged)
			]
		];
	}

	// Hide the layers since they'll get visualized directly
	TSharedRef<IPropertyHandle> TileLayersProperty = DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UPixel2DTileMap, TileLayers));
	DetailLayout.HideProperty(TileLayersProperty);

	// Add properties for the currently selected layer
	if ((TileMap != nullptr) && ((TileComponent == nullptr) || (TileComponent->OwnsTileMap())))
	{
		if (TileMap->TileLayers.IsValidIndex(TileMap->SelectedLayerIndex))
		{
			UPaperTileLayer* SelectedLayer = TileMap->TileLayers[TileMap->SelectedLayerIndex];

			const FText LayerCategoryDisplayName = LOCTEXT("LayerCategoryHeading", "Selected Layer");
			IDetailCategoryBuilder& LayerCategory = DetailLayout.EditCategory(TEXT("PixelSelectedLayer"), LayerCategoryDisplayName, ECategoryPriority::Important);

			LayerCategory.HeaderContent
			(
				SNew(SBox)
				.HAlign(HAlign_Right)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.Padding(FMargin(5.0f, 0.0f))
					.AutoWidth()
					[
						SNew(STextBlock)
						.Font(FEditorStyle::GetFontStyle("TinyText"))
						.Text(this, &FPixel2DTileMapDetailsCustomization::GetLayerSettingsHeadingText)
						.ToolTipText(LOCTEXT("LayerSettingsTooltip", "Properties specific to the currently selected layer"))
					]
				]
			);

			TArray<UObject*> ListOfSelectedLayers;
			ListOfSelectedLayers.Add(SelectedLayer);

			for (const FProperty* TestProperty : TFieldRange<FProperty>(SelectedLayer->GetClass()))
			{
				if (TestProperty->HasAnyPropertyFlags(CPF_Edit))
				{
					const bool bAdvancedDisplay = TestProperty->HasAnyPropertyFlags(CPF_AdvancedDisplay);
					const EPropertyLocation::Type PropertyLocation = bAdvancedDisplay ? EPropertyLocation::Advanced : EPropertyLocation::Common;

					LayerCategory.AddExternalObjectProperty(ListOfSelectedLayers, TestProperty->GetFName(), PropertyLocation);
				}
			}
		}
	}

	// Make sure the setup category is near the top (just below the layer browser and layer-specific stuff)
	IDetailCategoryBuilder& SetupCategory = DetailLayout.EditCategory("Setup", FText::GetEmpty(), ECategoryPriority::Important);

	// Add all of the properties from the inline tilemap
	if ((TileComponent != nullptr) && (TileComponent->OwnsTileMap()))
	{
		TArray<UObject*> ListOfTileMaps;
		ListOfTileMaps.Add(TileMap);

		for (const FProperty* TestProperty : TFieldRange<FProperty>(TileMap->GetClass()))
		{
			if (TestProperty->HasAnyPropertyFlags(CPF_Edit))
			{
				const bool bAdvancedDisplay = TestProperty->HasAnyPropertyFlags(CPF_AdvancedDisplay);
				const EPropertyLocation::Type PropertyLocation = bAdvancedDisplay ? EPropertyLocation::Advanced : EPropertyLocation::Common;

				const FName CategoryName(*TestProperty->GetMetaData(TEXT("Category")));
				IDetailCategoryBuilder& Category = DetailLayout.EditCategory(CategoryName);

				if (IDetailPropertyRow* ExternalRow = Category.AddExternalObjectProperty(ListOfTileMaps, TestProperty->GetFName(), PropertyLocation))
				{
					ExternalRow->Visibility(InternalInstanceVis);
				}
			}
		}
	}

	MyDetailLayout = &DetailLayout;
}

FReply FPixel2DTileMapDetailsCustomization::EnterTileMapEditingMode()
{
	if (UPixel2DTileMapComponent* TileMapComponent = TileMapComponentPtr.Get())
	{
		if (TileMapComponent->OwnsTileMap())
		{
			GLevelEditorModeTools().ActivateMode(FPixel2DEdModeTileMap::EM_TileMap);
		}
		else
		{
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(TileMapComponent->TileMap);
		}
	}
	return FReply::Handled();
}

FReply FPixel2DTileMapDetailsCustomization::OnNewButtonClicked()
{
	if (UPixel2DTileMapComponent* TileMapComponent = TileMapComponentPtr.Get())
	{
		UPaperTileSet* OldTileSet = (TileMapComponent->GetTileMap() != nullptr) ? TileMapComponent->TileMap->SelectedTileSet.Get() : nullptr;

		const FScopedTransaction Transaction(LOCTEXT("CreateNewTileMapTransaction", "New Tile Map"));
		TileMapComponent->Modify();
		TileMapComponent->CreateNewOwnedTileMap();

		// Add a layer and set things up
		GetDefault<UPixel2DImporterSettings>()->ApplySettingsForTileMapInit(TileMapComponent->GetTileMap(), OldTileSet);

		MyDetailLayout->ForceRefreshDetails();
	}

	return FReply::Handled();
}

FReply FPixel2DTileMapDetailsCustomization::OnPromoteToAssetButtonClicked()
{
	if (UPixel2DTileMapComponent* TileMapComponent = TileMapComponentPtr.Get())
	{
		if (TileMapComponent->OwnsTileMap())
		{
			if (TileMapComponent->TileMap != nullptr)
			{
				const FScopedTransaction Transaction(LOCTEXT("PromoteToAssetTransactionTransaction", "Convert Tile Map instance to an asset"));

				// Try promoting the tile map to be an asset (prompts for a name&path, creates a package and then calls the factory, which renames the existing asset and sets RF_Public)
				UPixel2DTileMapPromotionFactory* PromotionFactory = NewObject<UPixel2DTileMapPromotionFactory>();
				PromotionFactory->AssetToRename = TileMapComponent->GetTileMap();

				FAssetToolsModule& AssetToolsModule = FAssetToolsModule::GetModule();
				UObject* NewAsset = AssetToolsModule.Get().CreateAssetWithDialog(PromotionFactory->GetSupportedClass(), PromotionFactory);
			
				// Show it in the content browser
				TArray<UObject*> ObjectsToSync;
				ObjectsToSync.Add(NewAsset);
				GEditor->SyncBrowserToObjects(ObjectsToSync);
			}
		}
	}

	MyDetailLayout->ForceRefreshDetails();

	return FReply::Handled();
}

FReply FPixel2DTileMapDetailsCustomization::OnMakeInstanceFromAssetButtonClicked()
{
	if (UPixel2DTileMapComponent* TileMapComponent = TileMapComponentPtr.Get())
	{
		if (!TileMapComponent->OwnsTileMap())
		{
			const FScopedTransaction Transaction(LOCTEXT("ConvertToInstanceTransactionTransaction", "Convert Tile Map asset to unique instance"));

			TileMapComponent->Modify();
			TileMapComponent->MakeTileMapEditable();
		}
	}

	MyDetailLayout->ForceRefreshDetails();

	return FReply::Handled();
}

bool FPixel2DTileMapDetailsCustomization::GetIsEditModeEnabled() const
{
	if (UPixel2DTileMapComponent* TileMapComponent = TileMapComponentPtr.Get())
	{
		return (TileMapComponent->TileMap != nullptr);
	}
	
	return false;
}

EVisibility FPixel2DTileMapDetailsCustomization::GetNonEditModeVisibility() const
{
	return InLevelEditorContext() ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility FPixel2DTileMapDetailsCustomization::GetNewButtonVisiblity() const
{
	return (TileMapComponentPtr.Get() != nullptr) ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility FPixel2DTileMapDetailsCustomization::GetVisibilityForInstancedOnlyProperties() const
{
	return IsInstanced() ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility FPixel2DTileMapDetailsCustomization::GetVisibilityForMakeIntoInstance() const
{
	return (!IsInstanced() && InLevelEditorContext()) ? EVisibility::Visible : EVisibility::Collapsed;
}

bool FPixel2DTileMapDetailsCustomization::InLevelEditorContext() const
{
	if (UPixel2DTileMapComponent* TileMapComponent = TileMapComponentPtr.Get())
	{
		return TileMapComponent->GetOwner() != nullptr;
	}

	return false;

	//@TODO: This isn't the right question, we should instead look and see if we're a customization for an actor versus a component
	//return GLevelEditorModeTools().IsModeActive(FEdModeTileMap::EM_TileMap);
}

bool FPixel2DTileMapDetailsCustomization::IsInstanced() const
{
	if (UPixel2DTileMapComponent* TileMapComponent = TileMapComponentPtr.Get())
	{
		return TileMapComponent->OwnsTileMap();
	}

	return false;
}

void FPixel2DTileMapDetailsCustomization::OnSelectedLayerChanged()
{
	if (MyDetailLayout != nullptr)
	{
		IDetailLayoutBuilder* OldLayout = MyDetailLayout;
		MyDetailLayout = nullptr;

		OldLayout->ForceRefreshDetails();
	}
}

FText FPixel2DTileMapDetailsCustomization::GetLayerSettingsHeadingText() const
{
	if (UPixel2DTileMap* TileMap = TileMapPtr.Get())
	{
		if (TileMap->TileLayers.IsValidIndex(TileMap->SelectedLayerIndex))
		{
			UPaperTileLayer* SelectedLayer = TileMap->TileLayers[TileMap->SelectedLayerIndex];
			return SelectedLayer->LayerName;
		}
	}

	return FText::GetEmpty();
}

//////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
