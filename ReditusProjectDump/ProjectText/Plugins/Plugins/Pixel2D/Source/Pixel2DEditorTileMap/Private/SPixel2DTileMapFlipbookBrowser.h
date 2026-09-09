// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Visibility.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Input/Reply.h"
#include "Widgets/SWidget.h"

#include "AssetData.h"
#include "Widgets/SToolTip.h"
#include "Editor/ContentBrowser/Public/ContentBrowserDelegates.h"

class FPixel2DEdModeTileMap;

//////////////////////////////////////////////////////////////////////////
// SPixel2DTileMapFlipbookBrowser

class SPixel2DTileMapFlipbookBrowser : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPixel2DTileMapFlipbookBrowser)
	{}

	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs, TSharedPtr<FPixel2DEdModeTileMap> InTileMapEditor);

	virtual ~SPixel2DTileMapFlipbookBrowser();

	void OnFlipbookSelected(const FAssetData& AssetData);

private:
	// Pointer back to owning tile map editor instance (the keeper of state)
	TWeakPtr<class FPixel2DEdModeTileMap> TileMapEditorPtr;

protected:
	// delegate to sync the asset picker to selected assets
	FSyncToAssetsDelegate SyncToAssetsDelegate;
	FGetCurrentSelectionDelegate GetCurrentSelectionDelegate;


};
