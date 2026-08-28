// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdMode.h"

class ATileMapTerrainActor;
class FEditorViewportClient;
class FPrimitiveDrawInterface;
class FSceneView;
class FScopedTransaction;
class FViewport;
class HHitProxy;

struct FHitResult;
struct FViewportClick;

enum class ETileMapCursorTool : uint8
{
	None,
	AddBlock,
	PaintTile,
	PaintPath,
	ErasePath,
	RotateTile,
	SetSlant,
	RaiseBlock,
	LowerBlock,
	DeleteBlock
};

enum class ETileMapSlantMode : uint8
{
	Ramp,
	Stairs,
	DiagonalEdge
};

enum class ETileMapSlantDirection : uint8
{
	PositiveX,
	PositiveY,
	NegativeX,
	NegativeY
};

class FTileMapEdMode : public FEdMode
{
public:

	void SetSlantMode(ETileMapSlantMode NewMode)
	{
		SlantMode = NewMode;
	}

	ETileMapSlantMode GetSlantMode() const
	{
		return SlantMode;
	}

	void SetSlantAngle(float NewAngle)
	{
		SlantAngle = FMath::Clamp(NewAngle, 5.0f, 45.0f);
	}

	float GetSlantAngle() const
	{
		return SlantAngle;
	}

	int32 GetRampSegmentCount() const
	{
		const float SafeTangent = FMath::Max(
			FMath::Tan(FMath::DegreesToRadians(SlantAngle)),
			KINDA_SMALL_NUMBER
		);

		return FMath::Clamp(
			FMath::RoundToInt(1.0f / SafeTangent),
			1,
			8
		);
	}

	float GetEffectiveRampAngle() const
	{
		return FMath::RadiansToDegrees(
			FMath::Atan(1.0f / GetRampSegmentCount())
		);
	}

	int32 GetSlantSegmentCount() const
	{
		return SlantMode == ETileMapSlantMode::Stairs
			? 2
			: (
				SlantMode == ETileMapSlantMode::Ramp
					? GetRampSegmentCount()
					: 1
			);
	}

	float GetFixedStairAngle() const
	{
		// One 100-unit rise over a 150-unit stepped run. The two selected
		// grid cells retain 25-unit landings at the low and high ends.
		return FMath::RadiansToDegrees(FMath::Atan(2.0f / 3.0f));
	}

	void SetSlantDirection(
		ETileMapSlantDirection NewDirection
	)
	{
		SlantDirection = NewDirection;
	}

	ETileMapSlantDirection GetSlantDirection() const
	{
		return SlantDirection;
	}

	static const FEditorModeID EM_TileMapEdModeId;

	FTileMapEdMode();
	virtual ~FTileMapEdMode();

	virtual void Enter() override;
	virtual void Exit() override;

	virtual bool UsesToolkits() const override
	{
		return true;
	}

	virtual bool HandleClick(
		FEditorViewportClient* InViewportClient,
		HHitProxy* HitProxy,
		const FViewportClick& Click
	) override;

	virtual bool InputKey(
		FEditorViewportClient* ViewportClient,
		FViewport* Viewport,
		FKey Key,
		EInputEvent Event
	) override;

	virtual bool MouseMove(
		FEditorViewportClient* ViewportClient,
		FViewport* Viewport,
		int32 MouseX,
		int32 MouseY
	) override;

	virtual bool CapturedMouseMove(
		FEditorViewportClient* ViewportClient,
		FViewport* Viewport,
		int32 MouseX,
		int32 MouseY
	) override;

	virtual bool MouseLeave(
		FEditorViewportClient* ViewportClient,
		FViewport* Viewport
	) override;

	virtual void Render(
		const FSceneView* View,
		FViewport* Viewport,
		FPrimitiveDrawInterface* PDI
	) override;

	void SetActiveTool(
		ETileMapCursorTool NewTool
	);

	ETileMapCursorTool GetActiveTool() const;

	void SetActiveTileType(
		int32 NewTileType
	);

	int32 GetActiveTileType() const;

private:
	ETileMapCursorTool ActiveTool;
	int32 ActiveTileType;

	bool bHasCursorPreview;
	bool bCursorPreviewValid;
	bool bIsPainting;
	bool bStrokeChanged;

	FIntVector CursorPreviewGridPosition;

	TWeakObjectPtr<ATileMapTerrainActor>
		CursorPreviewTerrain;

	TSet<FIntVector> PaintedCellsThisStroke;

	TSet<ATileMapTerrainActor*>
		ModifiedTerrainsThisStroke;

	TUniquePtr<FScopedTransaction>
		PaintTransaction;

	bool TraceTileMapTerrain(
		const FVector& TraceOrigin,
		const FVector& TraceDirection,
		FHitResult& OutHitResult,
		ATileMapTerrainActor*& OutTerrainActor
	) const;

	bool UpdateCursorPreview(
		FEditorViewportClient* ViewportClient
	);

	bool ApplyActiveToolAtCursor(
		FEditorViewportClient* ViewportClient
	);

	bool ApplyActiveToolAtRay(
		const FVector& TraceOrigin,
		const FVector& TraceDirection
	);

	void BeginPaintStroke(
		FEditorViewportClient* ViewportClient
	);

	void EndPaintStroke();

	void ModifyTerrainForCurrentStroke(
		ATileMapTerrainActor* TerrainActor
	);

	FText GetPaintTransactionDescription() const;

	void ClearCursorPreview();

	void DrawCursorPreview(
		FPrimitiveDrawInterface* PDI
	) const;

	ETileMapSlantMode SlantMode =
		ETileMapSlantMode::Ramp;

	ETileMapSlantDirection SlantDirection =
		ETileMapSlantDirection::PositiveX;

	float SlantAngle = 26.565f;
};
