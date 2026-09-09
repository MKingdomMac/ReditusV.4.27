// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "PaperTileMapComponent.h"
#include "Pixel2DTileMap.h"
#include "DynamicMeshBuilder.h"
#include "Pixel2DTileMapComponent.generated.h"

class FPixel2DTileMapRenderSceneProxy;
struct FSpriteRenderSection;
class UPixel2DFlipbookComponent;

/**
* Implements Pixel2DTileMapComponent
*/

UCLASS(BlueprintType, HideCategories=("Tile Map"))
class PIXEL2D_API UPixel2DTileMapComponent : public UPaperTileMapComponent
{
	GENERATED_UCLASS_BODY()

public:
	~UPixel2DTileMapComponent();

	class UPixel2DTileMap* GetTileMap() { return CastChecked<class UPixel2DTileMap>(TileMap); }
	class UPixel2DTileMap* GetTileMap() const { return CastChecked<class UPixel2DTileMap>(TileMap); }

	void SpawnFlipbooks();
	void ClearSpawnedFlipbooks();

	// Creates a new tile map internally, replacing the TileMap reference (or dropping the previous owned one)
	void CreateNewOwnedTileMap();

	/**
	 * Creates a new tile map of the specified size, replacing the TileMap reference (or dropping the previous owned one)
	 *
	 * @param MapWidth Width of the map (in tiles)
	 * @param MapHeight Height of the map (in tiles)
	 * @param TileWidth Width of one tile (in pixels)
	 * @param TileHeight Height of one tile (in pixels)
	 * @param bCreateLayer Should an empty layer be created?
	 */
	//UFUNCTION(BlueprintCallable, Category = "Sprite")
	void CreateNewTileMap(int32 MapWidth = 4, int32 MapHeight = 4, int32 TileWidth = 32, int32 TileHeight = 32, float PixelsPerUnrealUnit = 1.0f, bool bCreateLayer = true);

public:
	// UObject interface
	virtual void PostInitProperties() override;
	// End of UObject interface

	void AddFlipbook(UPaperFlipbook* Flipbook, UPaperTileLayer* Layer, FIntPoint Location);
	void RemoveFlipbook(UPaperTileLayer* Layer, FIntPoint Location);
	UPaperFlipbook* GetFlipbook(UPaperTileLayer* Layer, FIntPoint Location);

	void ClearAllFlipbooks();

#if WITH_EDITOR
	bool CanPreview();
	void RebuildPreview();
	void LoadPreviewComponents();
	void RefreshPreviewVisibility();
	void RefreshPreview();
	void DestroyPreview();

	void RegisterComponentWithWorld(UWorld* InWorld, FRegisterComponentContext* Context = nullptr);
#endif

	UPROPERTY()
	TArray<UPixel2DFlipbookComponent*> SpawnedFlipbooks;

private:

#if WITH_EDITOR
	TArray<UPixel2DFlipbookComponent*> PreviewComponents;
	TMap<int32, UPixel2DFlipbookComponent*> PreviewComponentMap;
	UWorld* PreviewWorld;
#endif

protected:
	friend FPixel2DTileMapRenderSceneProxy;

};
