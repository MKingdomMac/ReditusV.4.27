// Copyright Epic Games, Inc. All Rights Reserved.

#include "TileMapEditorCommands.h"

#define LOCTEXT_NAMESPACE "FTileMapEditorModule"

void FTileMapEditorCommands::RegisterCommands()
{
	UI_COMMAND(PluginAction, "TileMapEditor", "Execute TileMapEditor action", EUserInterfaceActionType::Button, FInputGesture());
}

#undef LOCTEXT_NAMESPACE
