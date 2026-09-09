// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Pixel2DEditorViewportClient.h"

class FCanvas;
class UPaperTileSet;

//////////////////////////////////////////////////////////////////////////
// FPixel2DTileSetEditorViewportClient

class FPixel2DTileSetEditorViewportClient : public FPixel2DEditorViewportClient
{
public:
	FPixel2DTileSetEditorViewportClient(UPaperTileSet* InTileSet);

	// FViewportClient interface
	virtual void Draw(FViewport* Viewport, FCanvas* Canvas) override;
	// End of FViewportClient interface

	// FEditorViewportClient interface
	virtual FLinearColor GetBackgroundColor() const override;
	// End of FEditorViewportClient interface

	void ToggleShowTilesWithCollision();
	bool IsShowTilesWithCollisionChecked() const;

	void ToggleShowTilesWithMetaData();
	bool IsShowTilesWithMetaDataChecked() const;
public:
	// Tile set
	TWeakObjectPtr<UPaperTileSet> TileSetBeingEdited;

	bool bShowTilesWithCollision;
	bool bShowTilesWithMetaData;
	bool bHasValidPaintRectangle;
	FViewportSelectionRectangle ValidPaintRectangle;
	int32 CurrentSelectedTileIndex;
};
