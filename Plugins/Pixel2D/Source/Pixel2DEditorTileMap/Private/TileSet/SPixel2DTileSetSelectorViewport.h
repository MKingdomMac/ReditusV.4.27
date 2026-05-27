// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "PaperTileSet.h"
#include "SPixel2DEditorViewport.h"
#include "TileSet/Pixel2DTileSetEditorViewportClient.h"

struct FMarqueeOperation;

//////////////////////////////////////////////////////////////////////////
// SPixel2DTileSetSelectorViewport

class SPixel2DTileSetSelectorViewport : public SPixel2DEditorViewport
{
public:
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnTileViewportSelectionChanged, const FIntPoint& /*TopLeft*/, const FIntPoint& /*Dimensions*/);

	SLATE_BEGIN_ARGS(SPixel2DTileSetSelectorViewport) {}
	SLATE_END_ARGS()

	~SPixel2DTileSetSelectorViewport();

	void Construct(const FArguments& InArgs, UPaperTileSet* InTileSet, class FPixel2DEdModeTileMap* InTileMapEditor);

	void ChangeTileSet(UPaperTileSet* InTileSet);

	FOnTileViewportSelectionChanged& GetTileSelectionChanged()
	{
		return OnTileSelectionChanged;
	}

	void RefreshSelectionRectangle();

protected:
	// SWidget interface
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	// End of SWidget interface

	// SPaperEditorViewport interface
	virtual FText GetTitleText() const override;
	// End of SPaperEditorViewport interface

	// SEditorViewport interface
	virtual void BindCommands() override;
	virtual void OnFocusViewportToSelection() override;
	// End of SEditorViewport interface

private:
	void OnSelectionChanged(FMarqueeOperation Marquee, bool bIsPreview);

private:
	TWeakObjectPtr<class UPaperTileSet> TileSetPtr;
	TSharedPtr<class FPixel2DTileSetEditorViewportClient> TypedViewportClient;
	class FPixel2DEdModeTileMap* TileMapEditor;
	FIntPoint SelectionTopLeft;
	FIntPoint SelectionDimensions;
	bool bPendingZoom;

	FOnTileViewportSelectionChanged OnTileSelectionChanged;
};
