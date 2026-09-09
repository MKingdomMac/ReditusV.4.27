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

	case ETileMapCursorTool::PaintModularTerrain:
		return LOCTEXT(
			"PaintModularTerrainTransaction",
			"Paint Modular Terrain Blocks"
		);

	case ETileMapCursorTool::PaintContinuousTerrain:
		return LOCTEXT(
			"PaintContinuousTerrainTransaction",
			"Restore Continuous Terrain Blocks"
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
		ActiveTool == ETileMapCursorTool::PaintModularTerrain ||
		ActiveTool == ETileMapCursorTool::PaintContinuousTerrain ||
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
				TerrainActor->IsContinuousSurfaceBlock(PreviewPosition) &&
				!TerrainActor->HasBlock(
					PreviewPosition + FIntVector(0, 0, 1)
				) &&
				LocalHitNormal.Z > 0.25f;
		}

		if (
			bCursorPreviewValid &&
			(
				ActiveTool ==
					ETileMapCursorTool::PaintModularTerrain ||
				ActiveTool ==
					ETileMapCursorTool::PaintContinuousTerrain
			)
			)
		{
			const bool bCurrentlyExcluded =
				TerrainActor->IsBlockExcludedFromContinuousTerrain(
					PreviewPosition
				);
			const bool bWantExcluded =
				ActiveTool ==
				ETileMapCursorTool::PaintModularTerrain;

			bCursorPreviewValid =
				TerrainActor->IsContinuousSurfaceBlock(
					PreviewPosition
				) &&
				bCurrentlyExcluded != bWantExcluded;
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

	if (
		ActiveTool == ETileMapCursorTool::PaintModularTerrain ||
		ActiveTool == ETileMapCursorTool::PaintContinuousTerrain
		)
	{
		if (PaintedCellsThisStroke.Contains(CurrentPosition))
		{
			return false;
		}

		PaintedCellsThisStroke.Add(CurrentPosition);

		const bool bUseContinuousTerrain =
			ActiveTool ==
			ETileMapCursorTool::PaintContinuousTerrain;
		const bool bCurrentlyExcluded =
			TerrainActor->IsBlockExcludedFromContinuousTerrain(
				CurrentPosition
			);

		if (
			!TerrainActor->IsContinuousSurfaceBlock(CurrentPosition) ||
			bCurrentlyExcluded == !bUseContinuousTerrain
			)
		{
			return false;
		}

		ModifyTerrainForCurrentStroke(TerrainActor);

		const bool bChanged =
			TerrainActor->SetBlockContinuousTerrain(
				CurrentPosition,
				bUseContinuousTerrain
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

void FTileMapEdMode::DrawSlantCursorPreview(
	FPrimitiveDrawInterface* PDI,
	ATileMapTerrainActor* TerrainActor,
	const FLinearColor& PreviewColor
) const
{
	if (!PDI || !TerrainActor)
	{
		return;
	}

	const float Size = FMath::Max(TerrainActor->GridSize, 1.0f);
	const float HalfSize = Size * 0.5f;
	const float BottomZ = CursorPreviewGridPosition.Z * Size;
	const float TopZ = BottomZ + Size;
	const FVector2D CellCenter(
		(CursorPreviewGridPosition.X + 0.5f) * Size,
		(CursorPreviewGridPosition.Y + 0.5f) * Size
	);
	FVector2D RiseAxis(1.0f, 0.0f);

	switch (SlantDirection)
	{
	case ETileMapSlantDirection::PositiveY:
		RiseAxis = FVector2D(0.0f, 1.0f);
		break;

	case ETileMapSlantDirection::NegativeX:
		RiseAxis = FVector2D(-1.0f, 0.0f);
		break;

	case ETileMapSlantDirection::NegativeY:
		RiseAxis = FVector2D(0.0f, -1.0f);
		break;

	case ETileMapSlantDirection::PositiveX:
	default:
		break;
	}

	const FVector2D SideAxis(-RiseAxis.Y, RiseAxis.X);
	const FTransform TerrainTransform =
		TerrainActor->GetActorTransform();
	const FLinearColor ArrowColor =
		bCursorPreviewValid
		? FLinearColor(0.0f, 1.0f, 1.0f, 1.0f)
		: PreviewColor;

	auto ToWorld =
		[&](const FVector& LocalPosition)
		{
			return TerrainTransform.TransformPosition(LocalPosition);
		};

	auto DrawLocalLine =
		[&](
			const FVector& Start,
			const FVector& End,
			const FLinearColor& Color,
			float Thickness
		)
		{
			PDI->DrawLine(
				ToWorld(Start),
				ToWorld(End),
				Color,
				SDPG_Foreground,
				Thickness
			);
		};

	auto DrawDirectionArrow =
		[&](const FVector& Start, const FVector& End)
		{
			const FVector Direction = (End - Start).GetSafeNormal();

			if (Direction.IsNearlyZero())
			{
				return;
			}

			const FVector ArrowSide(
				SideAxis.X,
				SideAxis.Y,
				0.0f
			);
			const float HeadLength = FMath::Min(
				Size * 0.28f,
				(End - Start).Size() * 0.35f
			);
			const float HeadWidth = Size * 0.16f;
			const FVector HeadBase = End - (Direction * HeadLength);

			DrawLocalLine(Start, End, ArrowColor, 6.0f);
			DrawLocalLine(
				End,
				HeadBase + (ArrowSide * HeadWidth),
				ArrowColor,
				6.0f
			);
			DrawLocalLine(
				End,
				HeadBase - (ArrowSide * HeadWidth),
				ArrowColor,
				6.0f
			);
		};

	auto MakePoint =
		[](const FVector2D& Plan, float Height)
		{
			return FVector(Plan.X, Plan.Y, Height);
		};

	if (SlantMode == ETileMapSlantMode::DiagonalEdge)
	{
		// The generated diagonal tile retains the triangular half pointing
		// in the selected cut direction. Preview that exact prism footprint.
		const FVector2D PlanPoints[3] =
		{
			CellCenter - (RiseAxis * HalfSize) - (SideAxis * HalfSize),
			CellCenter + (RiseAxis * HalfSize) - (SideAxis * HalfSize),
			CellCenter + (RiseAxis * HalfSize) + (SideAxis * HalfSize)
		};

		for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
		{
			const int32 NextIndex = (EdgeIndex + 1) % 3;

			DrawLocalLine(
				MakePoint(PlanPoints[EdgeIndex], BottomZ),
				MakePoint(PlanPoints[NextIndex], BottomZ),
				PreviewColor,
				4.0f
			);
			DrawLocalLine(
				MakePoint(PlanPoints[EdgeIndex], TopZ),
				MakePoint(PlanPoints[NextIndex], TopZ),
				PreviewColor,
				4.0f
			);
			DrawLocalLine(
				MakePoint(PlanPoints[EdgeIndex], BottomZ),
				MakePoint(PlanPoints[EdgeIndex], TopZ),
				PreviewColor,
				4.0f
			);
		}

		const float ArrowHeight = TopZ + (Size * 0.12f);
		DrawDirectionArrow(
			MakePoint(
				CellCenter - (RiseAxis * Size * 0.28f),
				ArrowHeight
			),
			MakePoint(
				CellCenter + (RiseAxis * Size * 0.36f),
				ArrowHeight
			)
		);
		return;
	}

	const int32 SegmentCount = GetSlantSegmentCount();
	const float RunLength = SegmentCount * Size;
	const FVector2D LowCenter =
		CellCenter - (RiseAxis * HalfSize);
	const FVector2D HighCenter =
		LowCenter + (RiseAxis * RunLength);
	const FVector2D LowNegative =
		LowCenter - (SideAxis * HalfSize);
	const FVector2D LowPositive =
		LowCenter + (SideAxis * HalfSize);
	const FVector2D HighNegative =
		HighCenter - (SideAxis * HalfSize);
	const FVector2D HighPositive =
		HighCenter + (SideAxis * HalfSize);

	// Draw the complete affected footprint, rather than only the cell under
	// the cursor, so the user can see every block the slant will convert.
	DrawLocalLine(
		MakePoint(LowNegative, BottomZ),
		MakePoint(LowPositive, BottomZ),
		PreviewColor,
		4.0f
	);
	DrawLocalLine(
		MakePoint(LowNegative, BottomZ),
		MakePoint(HighNegative, BottomZ),
		PreviewColor,
		4.0f
	);
	DrawLocalLine(
		MakePoint(LowPositive, BottomZ),
		MakePoint(HighPositive, BottomZ),
		PreviewColor,
		4.0f
	);
	DrawLocalLine(
		MakePoint(HighNegative, BottomZ),
		MakePoint(HighPositive, BottomZ),
		PreviewColor,
		4.0f
	);

	if (SlantMode == ETileMapSlantMode::Stairs)
	{
		const int32 StepCount = 12;
		const float LowLandingLength = Size * 0.25f;
		const float StepDepth = (Size * 1.5f) / StepCount;
		const float StepRise = Size / StepCount;

		for (int32 StepIndex = 0;
			StepIndex <= StepCount;
			++StepIndex)
		{
			const float StartDistance =
				StepIndex == 0
				? 0.0f
				: LowLandingLength + ((StepIndex - 1) * StepDepth);
			const float EndDistance =
				StepIndex == StepCount
				? RunLength
				: LowLandingLength + (StepIndex * StepDepth);
			const float Height = BottomZ + (StepIndex * StepRise);
			const FVector2D StartCenter =
				LowCenter + (RiseAxis * StartDistance);
			const FVector2D EndCenter =
				LowCenter + (RiseAxis * EndDistance);
			const FVector2D StartNegative =
				StartCenter - (SideAxis * HalfSize);
			const FVector2D StartPositive =
				StartCenter + (SideAxis * HalfSize);
			const FVector2D EndNegative =
				EndCenter - (SideAxis * HalfSize);
			const FVector2D EndPositive =
				EndCenter + (SideAxis * HalfSize);

			DrawLocalLine(
				MakePoint(StartNegative, Height),
				MakePoint(EndNegative, Height),
				PreviewColor,
				3.0f
			);
			DrawLocalLine(
				MakePoint(StartPositive, Height),
				MakePoint(EndPositive, Height),
				PreviewColor,
				3.0f
			);
			DrawLocalLine(
				MakePoint(StartNegative, Height),
				MakePoint(StartPositive, Height),
				PreviewColor,
				3.0f
			);

			if (StepIndex < StepCount)
			{
				const float NextHeight = Height + StepRise;

				DrawLocalLine(
					MakePoint(EndNegative, Height),
					MakePoint(EndNegative, NextHeight),
					PreviewColor,
					3.0f
				);
				DrawLocalLine(
					MakePoint(EndPositive, Height),
					MakePoint(EndPositive, NextHeight),
					PreviewColor,
					3.0f
				);
				DrawLocalLine(
					MakePoint(EndNegative, NextHeight),
					MakePoint(EndPositive, NextHeight),
					PreviewColor,
					3.0f
				);
			}
		}
	}
	else
	{
		DrawLocalLine(
			MakePoint(LowNegative, BottomZ),
			MakePoint(HighNegative, TopZ),
			PreviewColor,
			4.0f
		);
		DrawLocalLine(
			MakePoint(LowPositive, BottomZ),
			MakePoint(HighPositive, TopZ),
			PreviewColor,
			4.0f
		);

		for (int32 SegmentIndex = 0;
			SegmentIndex <= SegmentCount;
			++SegmentIndex)
		{
			const float Alpha =
				static_cast<float>(SegmentIndex) /
				static_cast<float>(SegmentCount);
			const FVector2D SectionCenter =
				FMath::Lerp(LowCenter, HighCenter, Alpha);
			const float SectionHeight =
				FMath::Lerp(BottomZ, TopZ, Alpha);

			DrawLocalLine(
				MakePoint(
					SectionCenter - (SideAxis * HalfSize),
					SectionHeight
				),
				MakePoint(
					SectionCenter + (SideAxis * HalfSize),
					SectionHeight
				),
				PreviewColor,
				3.0f
			);
		}
	}

	DrawLocalLine(
		MakePoint(HighNegative, BottomZ),
		MakePoint(HighNegative, TopZ),
		PreviewColor,
		4.0f
	);
	DrawLocalLine(
		MakePoint(HighPositive, BottomZ),
		MakePoint(HighPositive, TopZ),
		PreviewColor,
		4.0f
	);

	const float ArrowLift = Size * 0.12f;
	DrawDirectionArrow(
		MakePoint(LowCenter, BottomZ + ArrowLift),
		MakePoint(HighCenter, TopZ + ArrowLift)
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
		ActiveTool == ETileMapCursorTool::PaintModularTerrain ||
		ActiveTool == ETileMapCursorTool::PaintContinuousTerrain
		)
	{
		PreviewColor =
			ActiveTool == ETileMapCursorTool::PaintModularTerrain
			? FLinearColor(0.1f, 0.55f, 1.0f, 1.0f)
			: FLinearColor(0.1f, 1.0f, 0.4f, 1.0f);
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

	if (ActiveTool == ETileMapCursorTool::SetSlant)
	{
		DrawSlantCursorPreview(
			PDI,
			TerrainActor,
			PreviewColor
		);
		return;
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
