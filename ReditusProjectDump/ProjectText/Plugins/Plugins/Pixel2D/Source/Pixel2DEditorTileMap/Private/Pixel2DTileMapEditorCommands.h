// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "Pixel2DMapStyle.h"

class FPixel2DTileMapEditorCommands : public TCommands<FPixel2DTileMapEditorCommands>
{
public:
	FPixel2DTileMapEditorCommands()
		: TCommands<FPixel2DTileMapEditorCommands>(
			TEXT("TileMapEditor"), // Context name for fast lookup
			NSLOCTEXT("Contexts", "Pixel2DTileMapEditor", "Pixel2D Tile Map Editor"), // Localized context name for displaying
			NAME_None, // Parent
			FPixel2DMapStyle::Get()->GetStyleSetName() // Icon Style Set
			)
	{
	}

	// TCommand<> interface
	virtual void RegisterCommands() override;
	// End of TCommand<> interface

public:
	// Edit mode trigger commands
	TSharedPtr<FUICommandInfo> EnterTileMapEditMode;

	// Commands inside tile map editing mode
	TSharedPtr<FUICommandInfo> SelectPaintTool;
	TSharedPtr<FUICommandInfo> SelectEraserTool;
	TSharedPtr<FUICommandInfo> SelectFillTool;
	TSharedPtr<FUICommandInfo> SelectEyeDropperTool;
	TSharedPtr<FUICommandInfo> SelectTerrainTool;

	// Show toggles
	TSharedPtr<FUICommandInfo> SetShowCollision;
	
	TSharedPtr<FUICommandInfo> SetShowPivot;
	TSharedPtr<FUICommandInfo> SetShowTileGrid;
	TSharedPtr<FUICommandInfo> SetShowLayerGrid;

	TSharedPtr<FUICommandInfo> SetShowTileMapStats;

	// Selection actions
	TSharedPtr<FUICommandInfo> FlipSelectionHorizontally;
	TSharedPtr<FUICommandInfo> FlipSelectionVertically;
	TSharedPtr<FUICommandInfo> RotateSelectionCW;
	TSharedPtr<FUICommandInfo> RotateSelectionCCW;

	// Layer actions
	TSharedPtr<FUICommandInfo> AddNewLayerAbove;
	TSharedPtr<FUICommandInfo> AddNewLayerBelow;
	TSharedPtr<FUICommandInfo> DeleteLayer;
	TSharedPtr<FUICommandInfo> MergeLayerDown;
	TSharedPtr<FUICommandInfo> MoveLayerUp;
	TSharedPtr<FUICommandInfo> MoveLayerDown;
	TSharedPtr<FUICommandInfo> MoveLayerToTop;
	TSharedPtr<FUICommandInfo> MoveLayerToBottom;
	TSharedPtr<FUICommandInfo> SelectLayerAbove;
	TSharedPtr<FUICommandInfo> SelectLayerBelow;
};
