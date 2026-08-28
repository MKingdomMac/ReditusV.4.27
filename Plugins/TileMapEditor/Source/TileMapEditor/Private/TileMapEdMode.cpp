// Copyright Epic Games, Inc. All Rights Reserved.

#include "TileMapEdMode.h"
#include "TileMapEdModeToolkit.h"
#include "TileMapTerrainActor.h"

#include "Editor.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "SceneManagement.h"
#include "ScopedTransaction.h"
#include "UnrealClient.h"

#define LOCTEXT_NAMESPACE "FTileMapEdMode"

const FEditorModeID FTileMapEdMode::EM_TileMapEdModeId =
TEXT("EM_TileMapEdMode");

FTileMapEdMode::FTileMapEdMode()
	:
	ActiveTool(ETileMapCursorTool::None),
	ActiveTileType(0),
	bHasCursorPreview(false),
	bCursorPreviewValid(false),
	bIsPainting(false),
	bStrokeChanged(false),
	CursorPreviewGridPosition(FIntVector::ZeroValue)
{
}

FTileMapEdMode::~FTileMapEdMode()
{
	EndPaintStroke();
}

void FTileMapEdMode::Enter()
{
	FEdMode::Enter();

	ActiveTool = ETileMapCursorTool::None;
	ActiveTileType = 0;
	bIsPainting = false;
	ClearCursorPreview();

	if (!Toolkit.IsValid() && UsesToolkits())
	{
		Toolkit = MakeShareable(
			new FTileMapEdModeToolkit
		);

		Toolkit->Init(
			Owner->GetToolkitHost()
		);
	}
}

void FTileMapEdMode::Exit()
{
	EndPaintStroke();

	ActiveTool = ETileMapCursorTool::None;
	ClearCursorPreview();

	Toolkit.Reset();

	FEdMode::Exit();
}

void FTileMapEdMode::SetActiveTool(
	ETileMapCursorTool NewTool
)
{
	EndPaintStroke();

	ActiveTool = NewTool;
	ClearCursorPreview();
}

ETileMapCursorTool
FTileMapEdMode::GetActiveTool() const
{
	return ActiveTool;
}

void FTileMapEdMode::SetActiveTileType(
	int32 NewTileType
)
{
	ActiveTileType = FMath::Max(NewTileType, 0);
}

int32 FTileMapEdMode::GetActiveTileType() const
{
	return ActiveTileType;
}

FText FTileMapEdMode::GetPaintTransactionDescription() const
{
	switch (ActiveTool)
	{
	case ETileMapCursorTool::AddBlock:
		return LOCTEXT(
			"PaintAddBlocksTransaction",
			"Paint Tile Map Blocks"
		);

	case ETileMapCursorTool::PaintTile:
		return LOCTEXT(
			"PaintTileTypesTransaction",
			"Replace Tile Map Block Types"
		);

	case ETileMapCursorTool::PaintPath:
		return LOCTEXT(
			"PaintPathTransaction",
			"Paint Tile Map Path"
		);

	case ETileMapCursorTool::ErasePath:
		return LOCTEXT(
			"ErasePathTransaction",
			"Erase Tile Map Path"
		);
	
	case ETileMapCursorTool::SetSlant:
		return LOCTEXT(
			"SetSlantTransaction",
			"Set Tile Map Slant"
		);
	case ETileMapCursorTool::RotateTile:
		return LOCTEXT(
			"RotateTilesTransaction",
			"Rotate Tile Map Tiles"
		);

	case ETileMapCursorTool::RaiseBlock:
		return LOCTEXT(
			"PaintRaiseBlocksTransaction",
			"Raise Tile Map Blocks"
		);

	case ETileMapCursorTool::LowerBlock:
		return LOCTEXT(
			"PaintLowerBlocksTransaction",
			"Lower Tile Map Blocks"
		);

	case ETileMapCursorTool::DeleteBlock:
		return LOCTEXT(
			"PaintDeleteBlocksTransaction",
			"Delete Tile Map Blocks"
		);

	default:
		return LOCTEXT(
			"PaintBlocksTransaction",
			"Edit Tile Map Blocks"
		);
	}
}

void FTileMapEdMode::BeginPaintStroke(
	FEditorViewportClient* ViewportClient
)
{
	if (
		bIsPainting ||
		!ViewportClient ||
		ActiveTool == ETileMapCursorTool::None
		)
	{
		return;
	}

	bIsPainting = true;
	bStrokeChanged = false;
	PaintedCellsThisStroke.Reset();
	ModifiedTerrainsThisStroke.Reset();

	PaintTransaction =
		MakeUnique<FScopedTransaction>(
			GetPaintTransactionDescription()
		);

	ApplyActiveToolAtCursor(ViewportClient);
}

void FTileMapEdMode::EndPaintStroke()
{
	bIsPainting = false;
	PaintedCellsThisStroke.Reset();
	ModifiedTerrainsThisStroke.Reset();

	if (PaintTransaction && !bStrokeChanged)
	{
		PaintTransaction->Cancel();
	}

	// Destroying the scoped transaction closes it.
	PaintTransaction.Reset();
	bStrokeChanged = false;
}

void FTileMapEdMode::ModifyTerrainForCurrentStroke(
	ATileMapTerrainActor* TerrainActor
)
{
	if (
		!TerrainActor ||
		ModifiedTerrainsThisStroke.Contains(TerrainActor)
		)
	{
		return;
	}

	// Capture the actor exactly once. Every block changed before mouse-up is
	// therefore one native Unreal transaction and one Ctrl+Z history step.
	TerrainActor->Modify();
	ModifiedTerrainsThisStroke.Add(TerrainActor);
}

bool FTileMapEdMode::InputKey(
	FEditorViewportClient* ViewportClient,
	FViewport* Viewport,
	FKey Key,
	EInputEvent Event
)
{
	if (
		ActiveTool != ETileMapCursorTool::None &&
		Key == EKeys::LeftMouseButton
		)
	{
		if (Event == IE_Pressed)
		{
			BeginPaintStroke(ViewportClient);
			return true;
		}

		if (Event == IE_Released)
		{
			EndPaintStroke();
			return true;
		}

		return true;
	}

	return FEdMode::InputKey(
		ViewportClient,
		Viewport,
		Key,
		Event
	);
}

void FTileMapEdMode::ClearCursorPreview()
{
	bHasCursorPreview = false;
	bCursorPreviewValid = false;
	CursorPreviewGridPosition = FIntVector::ZeroValue;
	CursorPreviewTerrain.Reset();
}

bool FTileMapEdMode::TraceTileMapTerrain(
	const FVector& TraceOrigin,
	const FVector& TraceDirection,
	FHitResult& OutHitResult,
	ATileMapTerrainActor*& OutTerrainActor
) const
{
	OutTerrainActor = nullptr;
	OutHitResult = FHitResult();

	if (!GEditor)
	{
		return false;
	}

	UWorld* EditorWorld =
		GEditor->GetEditorWorldContext().World();

	if (!EditorWorld)
	{
		return false;
	}

	const FVector SafeDirection =
		TraceDirection.GetSafeNormal();

	if (SafeDirection.IsNearlyZero())
	{
		return false;
	}

	FCollisionQueryParams QueryParameters;
	QueryParameters.bTraceComplex = false;

	const bool bHit =
		EditorWorld->LineTraceSingleByChannel(
			OutHitResult,
			TraceOrigin,
			TraceOrigin +
			(SafeDirection * 1000000.0f),
			ECC_Visibility,
			QueryParameters
		);

	if (!bHit)
	{
		return false;
	}

	OutTerrainActor =
		Cast<ATileMapTerrainActor>(
			OutHitResult.GetActor()
		);

	return OutTerrainActor != nullptr;
}

bool FTileMapEdMode::UpdateCursorPreview(
	FEditorViewportClient* ViewportClient
)
{
	ClearCursorPreview();

	if (
		!ViewportClient ||
		ActiveTool == ETileMapCursorTool::None
		)
	{
		return false;
	}

	const FViewportCursorLocation CursorRay =
		ViewportClient
		->GetCursorWorldLocationFromMousePos();

	FHitResult HitResult;
	ATileMapTerrainActor* TerrainActor = nullptr;

	if (
		!TraceTileMapTerrain(
			CursorRay.GetOrigin(),
			CursorRay.GetDirection(),
			HitResult,
			TerrainActor
		)
		)
	{
		return false;
	}

	FIntVector PreviewPosition;

	if (ActiveTool == ETileMapCursorTool::AddBlock)
	{
		if (
			!TerrainActor
			->GetAdjacentGridPositionFromHit(
				HitResult,
				PreviewPosition
			)
			)
		{
			return false;
		}

		bCursorPreviewValid =
			!TerrainActor->HasBlock(
				PreviewPosition
			);
	}
	else if (
		ActiveTool == ETileMapCursorTool::DeleteBlock ||
		ActiveTool == ETileMapCursorTool::PaintTile ||
		ActiveTool == ETileMapCursorTool::PaintPath ||
		ActiveTool == ETileMapCursorTool::ErasePath ||
		ActiveTool == ETileMapCursorTool::RotateTile ||
		ActiveTool == ETileMapCursorTool::SetSlant
		)
	{
		if (
			!TerrainActor->GetGridPositionFromHit(
				HitResult,
				PreviewPosition
			)
			)
		{
			return false;
		}

		bCursorPreviewValid =
			TerrainActor->HasBlock(PreviewPosition);

		if (
			bCursorPreviewValid &&
			(
				ActiveTool == ETileMapCursorTool::PaintPath ||
				ActiveTool == ETileMapCursorTool::ErasePath
			)
			)
		{
			const FVector LocalHitNormal =
				TerrainActor->GetActorTransform()
				.InverseTransformVectorNoScale(
					HitResult.ImpactNormal
				)
				.GetSafeNormal();

			bCursorPreviewValid =
				TerrainActor->bUseContinuousTerrainPrototype &&
				TerrainActor->IsContinuousSurfaceBlock(PreviewPosition) &&
				!TerrainActor->HasBlock(
					PreviewPosition + FIntVector(0, 0, 1)
				) &&
				LocalHitNormal.Z > 0.25f;
		}

		if (
			bCursorPreviewValid &&
			ActiveTool == ETileMapCursorTool::SetSlant &&
			SlantMode != ETileMapSlantMode::DiagonalEdge
			)
		{
			FIntVector RampStep(1, 0, 0);

			switch (SlantDirection)
			{
			case ETileMapSlantDirection::PositiveY:
				RampStep = FIntVector(0, 1, 0);
				break;

			case ETileMapSlantDirection::NegativeX:
				RampStep = FIntVector(-1, 0, 0);
				break;

			case ETileMapSlantDirection::NegativeY:
				RampStep = FIntVector(0, -1, 0);
				break;

			case ETileMapSlantDirection::PositiveX:
			default:
				break;
			}

			for (int32 SegmentIndex = 1;
				SegmentIndex < GetSlantSegmentCount();
				++SegmentIndex)
			{
				if (
					!TerrainActor->HasBlock(
						PreviewPosition + RampStep * SegmentIndex
					)
					)
				{
					bCursorPreviewValid = false;
					break;
				}
			}
		}
	}
	else
	{
		FIntVector CurrentPosition;

		if (
			!TerrainActor->GetGridPositionFromHit(
				HitResult,
				CurrentPosition
			)
			)
		{
			return false;
		}

		const int32 Direction =
			ActiveTool ==
			ETileMapCursorTool::RaiseBlock
			? 1
			: -1;

		PreviewPosition =
			CurrentPosition +
			FIntVector(0, 0, Direction);

		bCursorPreviewValid =
			PreviewPosition.Z >= 0 &&
			!TerrainActor->HasBlock(
				PreviewPosition
			);
	}

	CursorPreviewTerrain = TerrainActor;
	CursorPreviewGridPosition = PreviewPosition;
	bHasCursorPreview = true;

	return true;
}

bool FTileMapEdMode::ApplyActiveToolAtCursor(
	FEditorViewportClient* ViewportClient
)
{
	if (
		!ViewportClient ||
		ActiveTool == ETileMapCursorTool::None
		)
	{
		return false;
	}

	const FViewportCursorLocation CursorRay =
		ViewportClient
		->GetCursorWorldLocationFromMousePos();

	const bool bChanged =
		ApplyActiveToolAtRay(
			CursorRay.GetOrigin(),
			CursorRay.GetDirection()
		);

	if (bChanged)
	{
		UpdateCursorPreview(ViewportClient);

		ViewportClient->Invalidate(
			false,
			false
		);

		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports();
		}
	}

	return bChanged;
}

bool FTileMapEdMode::ApplyActiveToolAtRay(
	const FVector& TraceOrigin,
	const FVector& TraceDirection
)
{
	FHitResult HitResult;
	ATileMapTerrainActor* TerrainActor = nullptr;

	if (
		!TraceTileMapTerrain(
			TraceOrigin,
			TraceDirection,
			HitResult,
			TerrainActor
		)
		)
	{
		return false;
	}

	if (ActiveTool == ETileMapCursorTool::AddBlock)
	{
		FIntVector TargetPosition;

		if (
			!TerrainActor
			->GetAdjacentGridPositionFromHit(
				HitResult,
				TargetPosition
			)
			)
		{
			return false;
		}

		if (
			PaintedCellsThisStroke.Contains(
				TargetPosition
			)
			)
		{
			return false;
		}

		PaintedCellsThisStroke.Add(
			TargetPosition
		);

		if (TerrainActor->HasBlock(TargetPosition))
		{
			return false;
		}

		ModifyTerrainForCurrentStroke(TerrainActor);

		const bool bChanged =
			TerrainActor->AddBlock(
				TargetPosition,
				ActiveTileType
			);

		bStrokeChanged |= bChanged;
		return bChanged;
	}

	FIntVector CurrentPosition;

	if (
		!TerrainActor->GetGridPositionFromHit(
			HitResult,
			CurrentPosition
		)
		)
	{
		return false;
	}

	if (ActiveTool == ETileMapCursorTool::DeleteBlock)
	{
		if (
			PaintedCellsThisStroke.Contains(
				CurrentPosition
			)
			)
		{
			return false;
		}

		PaintedCellsThisStroke.Add(CurrentPosition);
		ModifyTerrainForCurrentStroke(TerrainActor);

		const bool bChanged =
			TerrainActor->RemoveBlock(CurrentPosition);

		bStrokeChanged |= bChanged;
		return bChanged;
	}

	if (ActiveTool == ETileMapCursorTool::PaintTile)
	{
		if (
			PaintedCellsThisStroke.Contains(
				CurrentPosition
			)
			)
		{
			return false;
		}

		PaintedCellsThisStroke.Add(CurrentPosition);

		if (
			TerrainActor->GetBlockTileType(
				CurrentPosition
			) ==
			FMath::Clamp(
				ActiveTileType,
				0,
				TerrainActor->GetTileTypeCount() - 1
			)
			)
		{
			return false;
		}

		ModifyTerrainForCurrentStroke(TerrainActor);

		const bool bChanged =
			TerrainActor->SetBlockTileType(
				CurrentPosition,
				ActiveTileType
			);

		bStrokeChanged |= bChanged;
		return bChanged;
	}

	if (
		ActiveTool == ETileMapCursorTool::PaintPath ||
		ActiveTool == ETileMapCursorTool::ErasePath
		)
	{
		if (PaintedCellsThisStroke.Contains(CurrentPosition))
		{
			return false;
		}

		PaintedCellsThisStroke.Add(CurrentPosition);

		const FVector LocalHitNormal =
			TerrainActor->GetActorTransform()
			.InverseTransformVectorNoScale(
				HitResult.ImpactNormal
			)
			.GetSafeNormal();
		const bool bShouldPaint =
			ActiveTool == ETileMapCursorTool::PaintPath;

		if (
			!TerrainActor->IsContinuousSurfaceBlock(CurrentPosition) ||
			TerrainActor->HasBlock(
				CurrentPosition + FIntVector(0, 0, 1)
			) ||
			LocalHitNormal.Z <= 0.25f ||
			TerrainActor->HasPaintedPath(CurrentPosition) == bShouldPaint
			)
		{
			return false;
		}

		ModifyTerrainForCurrentStroke(TerrainActor);

		const bool bChanged = TerrainActor->SetPathPainted(
			CurrentPosition,
			bShouldPaint
		);

		bStrokeChanged |= bChanged;
		return bChanged;
	}

	if (ActiveTool == ETileMapCursorTool::RotateTile)
	{
		if (
			PaintedCellsThisStroke.Contains(
				CurrentPosition
			)
			)
		{
			return false;
		}

		PaintedCellsThisStroke.Add(CurrentPosition);
		ModifyTerrainForCurrentStroke(TerrainActor);

		const bool bChanged =
			TerrainActor->RotateBlock(
				CurrentPosition,
				1
			);

		bStrokeChanged |= bChanged;
		return bChanged;
	}
	
	if (ActiveTool == ETileMapCursorTool::SetSlant)
	{
		if (
			PaintedCellsThisStroke.Contains(
				CurrentPosition
			)
			)
		{
			return false;
		}

		FIntVector RampStep(1, 0, 0);
		uint8 QuarterTurns = 0;

		switch (SlantDirection)
		{
		case ETileMapSlantDirection::PositiveY:
			RampStep = FIntVector(0, 1, 0);
			QuarterTurns = 1;
			break;

		case ETileMapSlantDirection::NegativeX:
			RampStep = FIntVector(-1, 0, 0);
			QuarterTurns = 2;
			break;

		case ETileMapSlantDirection::NegativeY:
			RampStep = FIntVector(0, -1, 0);
			QuarterTurns = 3;
			break;

		case ETileMapSlantDirection::PositiveX:
		default:
			break;
		}

		const int32 SegmentCount = GetSlantSegmentCount();

		TArray<FIntVector> RampPositions;
		RampPositions.Reserve(SegmentCount);

		for (int32 SegmentIndex = 0;
			SegmentIndex < SegmentCount;
			++SegmentIndex)
		{
			const FIntVector SegmentPosition =
				CurrentPosition + RampStep * SegmentIndex;

			// Never leave a partly converted ramp. The requested complete
			// run must already exist at the clicked elevation.
			if (!TerrainActor->HasBlock(SegmentPosition))
			{
				return false;
			}

			RampPositions.Add(SegmentPosition);
		}

		ModifyTerrainForCurrentStroke(
			TerrainActor
		);

		TArray<int32> SlantTileTypes;
		SlantTileTypes.Reserve(SegmentCount);
		const float AppliedSlantAngle =
			SlantMode == ETileMapSlantMode::DiagonalEdge
				? 45.0f
				: (
					SlantMode == ETileMapSlantMode::Stairs
						? GetFixedStairAngle()
						: SlantAngle
				);

		for (int32 SegmentIndex = 0;
			SegmentIndex < SegmentCount;
			++SegmentIndex)
		{
			const int32 SlantTileType =
				FTileMapEdModeToolkit::EnsureSlantTile(
				TerrainActor,
				SlantMode,
				AppliedSlantAngle,
				ActiveTileType,
				SegmentIndex,
				SegmentCount
				);

			if (SlantTileType == INDEX_NONE)
			{
				return false;
			}

			SlantTileTypes.Add(SlantTileType);
		}

		for (const FIntVector& RampPosition : RampPositions)
		{
			PaintedCellsThisStroke.Add(RampPosition);
		}

		bool bRemovedCutSide = false;

		if (SlantMode == ETileMapSlantMode::DiagonalEdge)
		{
			TArray<FIntVector> BlocksToRemove;
			const TArray<FIntVector> ExistingBlocks =
				TerrainActor->OccupiedBlocks;

			// The nearest column behind the selected cut direction only.
			// Never continue through another row or column of the map.
			const FIntVector CutColumn =
				CurrentPosition - RampStep;

			for (const FIntVector& ExistingPosition : ExistingBlocks)
			{
				if (
					ExistingPosition.X == CutColumn.X &&
					ExistingPosition.Y == CutColumn.Y &&
					ExistingPosition.Z >= CurrentPosition.Z
					)
				{
					BlocksToRemove.Add(ExistingPosition);
					PaintedCellsThisStroke.Add(ExistingPosition);
				}
			}

			bRemovedCutSide =
				TerrainActor->RemoveBlocks(BlocksToRemove);
		}

		const bool bChangedVisual =
			TerrainActor->SetBlocksVisual(
				RampPositions,
				SlantTileTypes,
				QuarterTurns
			);

		const bool bChanged =
			bRemovedCutSide || bChangedVisual;

		bStrokeChanged |= bChanged;
		return bChanged;
	}
	// One height change per X/Y column during one stroke.
	const FIntVector PaintedColumnKey(
		CurrentPosition.X,
		CurrentPosition.Y,
		0
	);

	if (
		PaintedCellsThisStroke.Contains(
			PaintedColumnKey
		)
		)
	{
		return false;
	}

	PaintedCellsThisStroke.Add(
		PaintedColumnKey
	);

	const int32 Direction =
		ActiveTool ==
		ETileMapCursorTool::RaiseBlock
		? 1
		: -1;

	const FIntVector TargetPosition =
		CurrentPosition +
		FIntVector(0, 0, Direction);

	if (
		TargetPosition.Z < 0 ||
		TerrainActor->HasBlock(TargetPosition)
		)
	{
		return false;
	}

	ModifyTerrainForCurrentStroke(TerrainActor);

	const bool bChanged = TerrainActor->MoveBlock(
		CurrentPosition,
		TargetPosition
	);

	bStrokeChanged |= bChanged;
	return bChanged;
}

bool FTileMapEdMode::MouseMove(
	FEditorViewportClient* ViewportClient,
	FViewport* Viewport,
	int32 MouseX,
	int32 MouseY
)
{
	ATileMapTerrainActor* PreviousTerrain =
		CursorPreviewTerrain.Get();

	const FIntVector PreviousPosition =
		CursorPreviewGridPosition;

	const bool bPreviousPreview =
		bHasCursorPreview;

	const bool bPreviousValid =
		bCursorPreviewValid;

	UpdateCursorPreview(ViewportClient);

	if (bIsPainting)
	{
		ApplyActiveToolAtCursor(ViewportClient);
	}

	const bool bPreviewChanged =
		PreviousTerrain != CursorPreviewTerrain.Get() ||
		PreviousPosition != CursorPreviewGridPosition ||
		bPreviousPreview != bHasCursorPreview ||
		bPreviousValid != bCursorPreviewValid;

	if (bPreviewChanged && ViewportClient)
	{
		ViewportClient->Invalidate(false, false);
	}

	return FEdMode::MouseMove(
		ViewportClient,
		Viewport,
		MouseX,
		MouseY
	);
}

bool FTileMapEdMode::CapturedMouseMove(
	FEditorViewportClient* ViewportClient,
	FViewport* Viewport,
	int32 MouseX,
	int32 MouseY
)
{
	UpdateCursorPreview(ViewportClient);

	if (bIsPainting)
	{
		ApplyActiveToolAtCursor(ViewportClient);

		if (ViewportClient)
		{
			ViewportClient->Invalidate(
				false,
				false
			);
		}

		return true;
	}

	return FEdMode::CapturedMouseMove(
		ViewportClient,
		Viewport,
		MouseX,
		MouseY
	);
}

bool FTileMapEdMode::MouseLeave(
	FEditorViewportClient* ViewportClient,
	FViewport* Viewport
)
{
	EndPaintStroke();
	ClearCursorPreview();

	if (ViewportClient)
	{
		ViewportClient->Invalidate(false, false);
	}

	return FEdMode::MouseLeave(
		ViewportClient,
		Viewport
	);
}

void FTileMapEdMode::DrawCursorPreview(
	FPrimitiveDrawInterface* PDI
) const
{
	if (!PDI || !bHasCursorPreview)
	{
		return;
	}

	ATileMapTerrainActor* TerrainActor =
		CursorPreviewTerrain.Get();

	if (!TerrainActor)
	{
		return;
	}

	const float Size =
		FMath::Max(
			TerrainActor->GridSize,
			1.0f
		);

	const FVector Minimum(
		CursorPreviewGridPosition.X * Size,
		CursorPreviewGridPosition.Y * Size,
		CursorPreviewGridPosition.Z * Size
	);

	const FVector Maximum =
		Minimum +
		FVector(Size, Size, Size);

	FVector Corners[8] =
	{
		FVector(Minimum.X, Minimum.Y, Minimum.Z),
		FVector(Maximum.X, Minimum.Y, Minimum.Z),
		FVector(Maximum.X, Maximum.Y, Minimum.Z),
		FVector(Minimum.X, Maximum.Y, Minimum.Z),

		FVector(Minimum.X, Minimum.Y, Maximum.Z),
		FVector(Maximum.X, Minimum.Y, Maximum.Z),
		FVector(Maximum.X, Maximum.Y, Maximum.Z),
		FVector(Minimum.X, Maximum.Y, Maximum.Z)
	};

	const FTransform TerrainTransform =
		TerrainActor->GetActorTransform();

	for (FVector& Corner : Corners)
	{
		Corner =
			TerrainTransform.TransformPosition(
				Corner
			);
	}

	FLinearColor PreviewColor;

	if (!bCursorPreviewValid)
	{
		PreviewColor = FLinearColor::Red;
	}
	else if (
		ActiveTool ==
		ETileMapCursorTool::AddBlock
		)
	{
		PreviewColor =
			FLinearColor(
				0.0f,
				0.75f,
				1.0f,
				1.0f
			);
	}
	else if (
		ActiveTool ==
		ETileMapCursorTool::RaiseBlock
		)
	{
		PreviewColor = FLinearColor::Green;
	}
	else if (
		ActiveTool ==
		ETileMapCursorTool::PaintTile
		)
	{
		PreviewColor = FLinearColor(
			0.8f,
			0.15f,
			1.0f,
			1.0f
		);
	}
	else if (
		ActiveTool == ETileMapCursorTool::PaintPath ||
		ActiveTool == ETileMapCursorTool::ErasePath
		)
	{
		PreviewColor =
			ActiveTool == ETileMapCursorTool::PaintPath
			? FLinearColor(0.65f, 0.35f, 0.05f, 1.0f)
			: FLinearColor(1.0f, 0.1f, 0.5f, 1.0f);
	}
	else if (
		ActiveTool ==
		ETileMapCursorTool::RotateTile
		)
	{
		PreviewColor = FLinearColor(
			1.0f,
			0.55f,
			0.0f,
			1.0f
		);
	}
	else if (
		ActiveTool ==
		ETileMapCursorTool::DeleteBlock
		)
	{
		PreviewColor = FLinearColor(
			1.0f,
			0.15f,
			0.0f,
			1.0f
		);
	}
	else
	{
		PreviewColor = FLinearColor::Yellow;
	}

	const int32 Edges[12][2] =
	{
		{ 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
		{ 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
		{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
	};

	for (int32 Index = 0; Index < 12; ++Index)
	{
		PDI->DrawLine(
			Corners[Edges[Index][0]],
			Corners[Edges[Index][1]],
			PreviewColor,
			SDPG_Foreground,
			3.0f
		);
	}
}

void FTileMapEdMode::Render(
	const FSceneView* View,
	FViewport* Viewport,
	FPrimitiveDrawInterface* PDI
)
{
	FEdMode::Render(View, Viewport, PDI);
	DrawCursorPreview(PDI);
}

bool FTileMapEdMode::HandleClick(
	FEditorViewportClient* InViewportClient,
	HHitProxy* HitProxy,
	const FViewportClick& Click
)
{
	// InputKey handles cursor-tool painting.
	if (
		ActiveTool != ETileMapCursorTool::None &&
		Click.GetKey() == EKeys::LeftMouseButton
		)
	{
		return true;
	}

	return FEdMode::HandleClick(
		InViewportClient,
		HitProxy,
		Click
	);
}

#undef LOCTEXT_NAMESPACE
