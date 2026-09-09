// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPixel2DTileMapEditorViewportToolbar.h"
#include "Pixel2DTileMapEditorCommands.h"
#include "SEditorViewport.h"

#define LOCTEXT_NAMESPACE "SPixel2DTileMapEditorViewportToolbar"

///////////////////////////////////////////////////////////
// SPixel2DTileMapEditorViewportToolbar

void SPixel2DTileMapEditorViewportToolbar::Construct(const FArguments& InArgs, TSharedPtr<class ICommonEditorViewportToolbarInfoProvider> InInfoProvider)
{
	SCommonEditorViewportToolbarBase::Construct(SCommonEditorViewportToolbarBase::FArguments(), InInfoProvider);
}

TSharedRef<SWidget> SPixel2DTileMapEditorViewportToolbar::GenerateShowMenu() const
{
	GetInfoProvider().OnFloatingButtonClicked();

	TSharedRef<SEditorViewport> ViewportRef = GetInfoProvider().GetViewportWidget();

	const FPixel2DTileMapEditorCommands& Commands = FPixel2DTileMapEditorCommands::Get();

	const bool bInShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder ShowMenuBuilder(bInShouldCloseWindowAfterMenuSelection, ViewportRef->GetCommandList());
	{
		ShowMenuBuilder.AddMenuEntry(Commands.SetShowPivot);

		ShowMenuBuilder.AddMenuSeparator();

		ShowMenuBuilder.AddMenuEntry(Commands.SetShowTileGrid);
		ShowMenuBuilder.AddMenuEntry(Commands.SetShowLayerGrid);
		ShowMenuBuilder.AddMenuEntry(Commands.SetShowTileMapStats);

		ShowMenuBuilder.AddMenuSeparator();

		ShowMenuBuilder.AddMenuEntry(Commands.SetShowCollision);
	}

	return ShowMenuBuilder.MakeWidget();
}


#undef LOCTEXT_NAMESPACE
