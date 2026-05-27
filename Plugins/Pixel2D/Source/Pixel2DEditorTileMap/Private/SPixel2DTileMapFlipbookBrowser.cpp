// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "SPixel2DTileMapFlipbookBrowser.h"
#include "Pixel2DEdModeTileMap.h"

#include "Framework/Commands/UIAction.h"
#include "Framework/Commands/UICommandList.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Toolkits/AssetEditorManager.h"
#include "EditorStyleSet.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SViewport.h"
#include "FileHelpers.h"

#include "ARFilter.h"
#include "Editor/ContentBrowser/Public/ContentBrowserDelegates.h"
#include "AssetData.h"
#include "Widgets/SToolTip.h"
#include "Layout/Visibility.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Input/Reply.h"
#include "Widgets/SWidget.h"

#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "AssetRegistryModule.h"
#include "Toolkits/GlobalEditorCommonCommands.h"
#include "FrontendFilterBase.h"
#include "ObjectEditorUtils.h"
#include "Misc/ConfigCacheIni.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/SceneViewport.h"

#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#include "Paper2DModule.h"

#define LOCTEXT_NAMESPACE "Pixel2DAnimAssetBrowser"


SPixel2DTileMapFlipbookBrowser::~SPixel2DTileMapFlipbookBrowser()
{
}

void SPixel2DTileMapFlipbookBrowser::Construct(const FArguments& InArgs, TSharedPtr<FPixel2DEdModeTileMap> InTileMapEditor)
{
	TileMapEditorPtr = InTileMapEditor;

	FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	// Configure filter for asset picker
	FARFilter Filter;
	Filter.bRecursiveClasses = true;
	Filter.ClassNames.Add(UPaperFlipbook::StaticClass()->GetFName());

	FAssetPickerConfig Config;
	Config.Filter = Filter;
	Config.bAddFilterUI = true;
	Config.bShowPathInColumnView = true;
	Config.bSortByPathInColumnView = true;

	//// Configure response to click and double-click
	Config.OnAssetSelected = FOnAssetSelected::CreateSP(this, &SPixel2DTileMapFlipbookBrowser::OnFlipbookSelected);
	//Config.OnAssetDoubleClicked = FOnAssetDoubleClicked::CreateSP(this, &SPixel2DAnimAssetBrowser::OnRequestOpenAsset, false);
	//Config.OnGetAssetContextMenu = FOnGetAssetContextMenu::CreateSP(this, &SPixel2DAnimAssetBrowser::OnGetAssetContextMenu);
	//Config.SyncToAssetsDelegates.Add(&SyncToAssetsDelegate);
	Config.GetCurrentSelectionDelegates.Add(&GetCurrentSelectionDelegate);
	//Config.SetFilterDelegates.Add(&SetFilterDelegate);
	//Config.bFocusSearchBoxWhenOpened = false;
	//Config.DefaultFilterMenuExpansion = EAssetTypeCategories::Animation;

	//Config.SaveSettingsName = SettingsIniSection;

	//TSharedPtr<FFrontendFilterCategory> FolderCategory = MakeShareable(new FFrontendFilterCategory(LOCTEXT("FolderFilters", "Folder Filters"), LOCTEXT("FolderFiltersTooltip", "Filter by folders.")));
	//const uint32 NumFilters = 1;
	//for (uint32 FilterIndex = 0; FilterIndex < NumFilters; ++FilterIndex)
	//{
	//	TSharedRef<FFrontendFilter_Folder> FolderFilter = MakeShared<FFrontendFilter_Folder>(FolderCategory, FilterIndex,
	//		FSimpleDelegate::CreateLambda([this]()
	//			{
	//				Filter.PackagePaths.Empty();

	//				for (TSharedPtr<FFrontendFilter_Folder> CurrentFolderFilter : FolderFilters)
	//				{
	//					if (CurrentFolderFilter->bEnabled)
	//					{
	//						Filter.PackagePaths.Add(*CurrentFolderFilter->Folder);
	//					}
	//				}

	//				SetFilterDelegate.ExecuteIfBound(Filter);
	//			}));
	//	FolderFilters.Add(FolderFilter);
	//	Config.ExtraFrontendFilters.Add(FolderFilter);
	//}

	//Config.OnIsAssetValidForCustomToolTip = FOnIsAssetValidForCustomToolTip::CreateLambda([](const FAssetData& AssetData) {return AssetData.IsAssetLoaded(); });
	//Config.OnGetCustomAssetToolTip = FOnGetCustomAssetToolTip::CreateSP(this, &SPixel2DAnimAssetBrowser::CreateCustomAssetToolTip);
	//Config.OnVisualizeAssetToolTip = FOnVisualizeAssetToolTip::CreateSP(this, &SPixel2DAnimAssetBrowser::OnVisualizeAssetToolTip);
	//Config.OnAssetToolTipClosing = FOnAssetToolTipClosing::CreateSP(this, &SPixel2DAnimAssetBrowser::OnAssetToolTipClosing);


	this->ChildSlot
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.FillHeight(1.f)
			[
				SNew(SBorder)
				.Padding(FMargin(3))
				.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					ContentBrowserModule.Get().CreateAssetPicker(Config)
				]
			]
		];
}

void SPixel2DTileMapFlipbookBrowser::OnFlipbookSelected(const FAssetData& AssetData)
{
	if (TileMapEditorPtr.IsValid())
	{
		UClass* AssetClass = FindObject<UClass>(ANY_PACKAGE, *AssetData.AssetClass.ToString());
		if (AssetClass->IsChildOf(UPaperFlipbook::StaticClass()) && AssetData.GetAsset())
		{
			UPaperFlipbook* Asset = StaticCast<UPaperFlipbook*>(AssetData.GetAsset());
			TileMapEditorPtr.Pin()->SetActiveFlipbook(Asset);
		}
	}
}

#undef LOCTEXT_NAMESPACE