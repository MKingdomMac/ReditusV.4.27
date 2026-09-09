// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "TileMapEditorStyle.h"

class FTileMapEditorCommands : public TCommands<FTileMapEditorCommands>
{
public:

	FTileMapEditorCommands()
		: TCommands<FTileMapEditorCommands>(TEXT("TileMapEditor"), NSLOCTEXT("Contexts", "TileMapEditor", "TileMapEditor Plugin"), NAME_None, FTileMapEditorStyle::GetStyleSetName())
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr< FUICommandInfo > PluginAction;
};
