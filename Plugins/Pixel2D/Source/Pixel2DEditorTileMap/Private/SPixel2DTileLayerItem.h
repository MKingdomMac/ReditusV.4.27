// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SlateFwd.h"
#include "Styling/SlateColor.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Input/Reply.h"
#include "Widgets/SCompoundWidget.h"
#include "Framework/SlateDelegates.h"
#include "Pixel2DTileMap.h"

class SButton;
class UPaperTileLayer;
struct FSlateBrush;

//////////////////////////////////////////////////////////////////////////
// SPixel2DTileLayerItem

class SPixel2DTileLayerItem : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPixel2DTileLayerItem) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, int32 Index, class UPixel2DTileMap* InMap, FIsSelected InIsSelectedDelegate);

	void BeginEditingName();

protected:
	int32 MyIndex;
	UPixel2DTileMap* MyMap;
	UPaperTileLayer* GetMyLayer() const { return MyMap->TileLayers[MyIndex]; }

	TSharedPtr<SButton> VisibilityButton;

	const FSlateBrush* EyeClosed;
	const FSlateBrush* EyeOpened;

	TSharedPtr<SInlineEditableTextBlock> LayerNameWidget;

protected:
	FText GetLayerDisplayName() const;
	void OnLayerNameCommitted(const FText& NewText, ETextCommit::Type CommitInfo);

	const FSlateBrush* GetVisibilityBrushForLayer() const;
	FSlateColor GetForegroundColorForVisibilityButton() const;
	FReply OnToggleVisibility();
};
