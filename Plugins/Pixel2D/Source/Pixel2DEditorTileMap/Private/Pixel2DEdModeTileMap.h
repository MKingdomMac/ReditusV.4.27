// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "EditorViewportClient.h"
#include "EdMode.h"

class FCanvas;
class UPaperTileLayer;
class UPixel2DTileMap;
class UPixel2DTileMapComponent;
class UPaperTileSet;
class UPaperFlipbook;

namespace EPixel2DTileMapEditorTool
{
	enum Type
	{
		Paintbrush,
		Eraser,
		PaintBucket,
		EyeDropper,
		TerrainBrush
	};
}

//////////////////////////////////////////////////////////////////////////
// FPixel2DTileMapDirtyRegion

struct FPixel2DTileMapDirtyRegion
{
public:
	TWeakObjectPtr<UPixel2DTileMapComponent> ComponentPtr;
	FBox DirtyRegionInWorldSpace;

public:
	FPixel2DTileMapDirtyRegion()
	{
	}

	FPixel2DTileMapDirtyRegion(UPixel2DTileMapComponent* InComponent, const FBox& DirtyRegionInTileSpace);

	void PushToNavSystem() const;
	
	UPixel2DTileMapComponent* GetComponent() const
	{
		return ComponentPtr.Get();
	}
};

///////////////////////////////////////////////////////////////////////////
// FPixel2DEdModeTileMap

class FPixel2DEdModeTileMap : public FEdMode
{
public:
	static const FEditorModeID EM_TileMap;

public:
	FPixel2DEdModeTileMap();
	virtual ~FPixel2DEdModeTileMap();

	// FSerializableObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	// End of FSerializableObject

	// FEdMode interface
	virtual bool UsesToolkits() const override;
	virtual void Enter() override;
	virtual void Exit() override;
	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override;
	virtual bool MouseEnter(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y) override;
	virtual bool MouseLeave(FEditorViewportClient* ViewportClient, FViewport* Viewport) override;
	virtual bool MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y) override;
	virtual bool CapturedMouseMove(FEditorViewportClient* InViewportClient, FViewport* InViewport, int32 InMouseX, int32 InMouseY) override;
	virtual bool StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override;
	virtual bool EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override;
	virtual bool InputKey(FEditorViewportClient* InViewportClient, FViewport* InViewport, FKey InKey, EInputEvent InEvent) override;
	virtual bool InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale) override;
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
	virtual void DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) override;
	virtual void ActorSelectionChangeNotify() override;
	virtual bool AllowWidgetMove() override;
	virtual bool ShouldDrawWidget() const override;
	virtual bool UsesTransformWidget() const override;
	// End of FEdMode interface

	void SetActiveTool(EPixel2DTileMapEditorTool::Type NewTool);
	EPixel2DTileMapEditorTool::Type GetActiveTool() const;

	void SetActivePaint(UPaperTileSet* TileSet, FIntPoint TopLeft, FIntPoint Dimensions);
	void SetActivePaintFromLayer(UPaperTileLayer* SourceLayer, FIntPoint TopLeft, FIntPoint Dimensions);
	void SetActiveFlipbook(UPaperFlipbook* Flipbook);

	void DestructiveResizePreviewComponent(int32 NewWidth, int32 NewHeight);

	void FlipSelectionHorizontally();
	void FlipSelectionVertically();
	void RotateSelectionCW();
	void RotateSelectionCCW();
	bool HasValidSelection() const;

	void RefreshBrushSize();

protected:
	bool UseActiveToolAtLocation(const FViewportCursorLocation& Ray);

	bool SelectTiles(const FViewportCursorLocation& Ray);
	bool PaintTiles(const FViewportCursorLocation& Ray);
	bool EraseTiles(const FViewportCursorLocation& Ray);
	bool FloodFillTiles(const FViewportCursorLocation& Ray);
	bool PaintTilesWithTerrain(const FViewportCursorLocation& Ray);


	void UpdatePreviewCursor(const FViewportCursorLocation& Ray);

	// Returns the selected layer under the cursor, and the intersection tile coordinates
	// Note: The tile coordinates can be negative if the brush is off the top or left of the tile map, but still overlaps the map!!!
	UPaperTileLayer* GetSelectedLayerUnderCursor(const FViewportCursorLocation& Ray, int32& OutTileX, int32& OutTileY, bool bAllowOutOfBounds = false) const;

	// Compute a world space ray from the screen space mouse coordinates
	FViewportCursorLocation CalculateViewRay(FEditorViewportClient* InViewportClient, FViewport* InViewport);
	
	TSharedRef<FExtender> AddCreationModeExtender(const TSharedRef<FUICommandList> InCommandList);

	void EnableTileMapEditMode();
	bool IsTileMapEditModeActive() const;

	void SynchronizePreviewWithTileMap(UPixel2DTileMap* NewTileMap);

	void RotateTilesInSelection(bool bIsClockwise);

	bool IsToolReadyToBeUsed() const;

	bool BlitLayer(UPaperTileLayer* SourceLayer, UPixel2DTileMapComponent* TargetComponent, UPaperTileLayer* TargetLayer, FBox& OutDirtyRect, int32 OffsetX = 0, int32 OffsetY = 0, bool bBlitEmptyTiles = false);
	bool BlitLayer(UPixel2DTileMapComponent* SourceComponent, UPixel2DTileMapComponent* TargetComponent, UPaperTileLayer* TargetLayer, FBox& OutDirtyRect, int32 OffsetX = 0, int32 OffsetY = 0);

	void FlushPendingDirtyRegions();
public:
	UPixel2DTileMapComponent* FindSelectedComponent() const;

	UPaperTileLayer* GetSourceInkLayer() const;

	int32 GetBrushWidth() const;
	int32 GetBrushHeight() const;

	int32 GetCursorWidth() const;
	int32 GetCursorHeight() const;
protected:
	// Were we previously painting?
	bool bWasPainting;

	// Are we currently painting?
	bool bIsPainting;

	// Ink source
	bool bHasValidInkSource;
	bool bHasFlipbookInk;
	
	// State for eyedropper
	bool bWasHoldingSelectWhenPaintingStarted;
	FIntPoint EyeDropperStart;
	FIntRect LastEyeDropperBounds;

	//
	FTransform DrawPreviewSpace;

	// Center of preview rectangle
	FVector DrawPreviewLocation;

	// Size of rectangle
	int32 LastCursorTileX;
	int32 LastCursorTileY;
	int32 LastCursorTileZ;
	bool bIsLastCursorValid;
	TWeakObjectPtr<UPixel2DTileMap> LastCursorTileMap;

	FVector DrawPreviewDimensionsLS;

	// Top left of the component bounds
	FVector DrawPreviewTopLeft;

	int32 EraseBrushSize;

	UPixel2DTileMapComponent* CursorPreviewComponent;

	// Nav mesh rebuilding
	float TimeUntilNavMeshRebuild;
	TArray<FPixel2DTileMapDirtyRegion> PendingDirtyRegions;

	//
	EPixel2DTileMapEditorTool::Type ActiveTool;
	mutable FTransform ComponentToWorld;
};

