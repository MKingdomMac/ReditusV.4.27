// Copyright Epic Games, Inc. All Rights Reserved.

#include "Pixel2DTileMapAssetImportData.h"
#include "Pixel2DTileMap.h"

///////////////////////////////////////////////////////////////////////////
// UPixel2DTileMapAssetImportData

UPixel2DTileMapAssetImportData::UPixel2DTileMapAssetImportData(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UPixel2DTileMapAssetImportData* UPixel2DTileMapAssetImportData::GetImportDataForTileMap(UPixel2DTileMap* TileMap/*, UFbxSkeletalMeshImportData* TemplateForCreation*/)
{
	check(TileMap);

	UPixel2DTileMapAssetImportData* ImportData = Cast<UPixel2DTileMapAssetImportData>(TileMap->AssetImportData);
	if (ImportData == nullptr)
	{
		ImportData = NewObject<UPixel2DTileMapAssetImportData>(TileMap, NAME_None, RF_NoFlags/*, TemplateForCreation*/);

		// Try to preserve the source file path if possible
		if (TileMap->AssetImportData != nullptr)
		{
			ImportData->SourceData = TileMap->AssetImportData->SourceData;
		}

		TileMap->AssetImportData = ImportData;
	}

	return ImportData;
}
