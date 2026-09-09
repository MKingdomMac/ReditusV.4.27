// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Visibility.h"
#include "Input/Reply.h"
#include "PaperTileMap.h"
#include "IDetailCustomization.h"
#include "Pixel2DTileMapComponent.h"

class IDetailLayoutBuilder;

//////////////////////////////////////////////////////////////////////////
// FPixel2DTileMapDetailsCustomization

class FPixel2DTileMapDetailsCustomization : public IDetailCustomization
{
public:
	// Makes a new instance of this detail layout class for a specific detail view requesting it
	static TSharedRef<IDetailCustomization> MakeInstance();

	// IDetailCustomization interface
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;
	// End of IDetailCustomization interface

private:
	TWeakObjectPtr<class UPixel2DTileMap> TileMapPtr;
	TWeakObjectPtr<class UPixel2DTileMapComponent> TileMapComponentPtr;

	IDetailLayoutBuilder* MyDetailLayout;

private:
	FReply EnterTileMapEditingMode();
	FReply OnNewButtonClicked();
	FReply OnPromoteToAssetButtonClicked();
	FReply OnMakeInstanceFromAssetButtonClicked();

	EVisibility GetNonEditModeVisibility() const;

	EVisibility GetVisibilityForInstancedOnlyProperties() const;
	
	EVisibility GetVisibilityForMakeIntoInstance() const;

	EVisibility GetNewButtonVisiblity() const;

	bool GetIsEditModeEnabled() const;

	bool InLevelEditorContext() const;
	bool IsInstanced() const;

	void OnSelectedLayerChanged();

	FText GetLayerSettingsHeadingText() const;
};
