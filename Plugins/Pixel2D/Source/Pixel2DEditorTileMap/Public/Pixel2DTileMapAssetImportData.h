// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/Texture.h"
#include "EditorFramework/AssetImportData.h"
#include "Pixel2DTileMapAssetImportData.generated.h"

class UPaperTileSet;

USTRUCT()
struct FPixel2DTileSetImportMapping
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	FString SourceName;

	UPROPERTY()
	TWeakObjectPtr<class UPaperTileSet> ImportedTileSet;

	UPROPERTY()
	TWeakObjectPtr<class UTexture> ImportedTexture;
};

/**
 * Base class for import data and options used when importing a tile map
 */
UCLASS()
class PIXEL2DEDITORTILEMAP_API UPixel2DTileMapAssetImportData : public UAssetImportData
{
	GENERATED_UCLASS_BODY()

	UPROPERTY()
	TArray<FPixel2DTileSetImportMapping> TileSetMap;

	static UPixel2DTileMapAssetImportData* GetImportDataForTileMap(class UPixel2DTileMap* TileMap);
};
