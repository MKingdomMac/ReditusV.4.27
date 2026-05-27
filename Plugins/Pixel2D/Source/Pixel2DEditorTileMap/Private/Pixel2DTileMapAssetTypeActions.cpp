// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "Pixel2DTileMapAssetTypeActions.h"
#include "Pixel2DTileMap.h"
#include "Pixel2DTileMapEditor.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

//////////////////////////////////////////////////////////////////////////
// FPixel2DTileMapAssetTypeActions

FPixel2DTileMapAssetTypeActions::FPixel2DTileMapAssetTypeActions(EAssetTypeCategories::Type InAssetCategory)
	: MyAssetCategory(InAssetCategory)
{
}

FText FPixel2DTileMapAssetTypeActions::GetName() const
{
	return LOCTEXT("FPixel2DTileMapAssetTypeActionsName", "Pixel Tile Map");
}

FColor FPixel2DTileMapAssetTypeActions::GetTypeColor() const
{
	return FColor::Turquoise;
}

UClass* FPixel2DTileMapAssetTypeActions::GetSupportedClass() const
{
	return UPixel2DTileMap::StaticClass();
}

void FPixel2DTileMapAssetTypeActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<class IToolkitHost> EditWithinLevelEditor)
{
	EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

	for (auto ObjIt = InObjects.CreateConstIterator(); ObjIt; ++ObjIt)
	{
		if (UPixel2DTileMap* TileMap = Cast<UPixel2DTileMap>(*ObjIt))
		{
			TSharedRef<FPixel2DTileMapEditor> NewTileMapEditor(new FPixel2DTileMapEditor());
			NewTileMapEditor->InitTileMapEditor(Mode, EditWithinLevelEditor, TileMap);
		}
	}
}

uint32 FPixel2DTileMapAssetTypeActions::GetCategories()
{
	return MyAssetCategory;
}

#undef LOCTEXT_NAMESPACE
