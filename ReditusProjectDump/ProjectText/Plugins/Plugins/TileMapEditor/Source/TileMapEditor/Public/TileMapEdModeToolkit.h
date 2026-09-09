// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/BaseToolkit.h"

class ATileMapTerrainActor;
enum class ETileMapSlantMode : uint8;

class FTileMapEdModeToolkit : public FModeToolkit
{
public:

	static int32 EnsureSlantTile(
		ATileMapTerrainActor* TerrainActor,
		ETileMapSlantMode SlantMode,
		float SlantAngle,
		int32 MaterialTileType,
		int32 RampSegmentIndex = 0,
		int32 RampSegmentCount = 1
	);

	virtual void Init(
		const TSharedPtr<IToolkitHost>& InitToolkitHost
	) override;

	virtual FName GetToolkitFName() const override;

	virtual FText GetBaseToolkitName() const override;

	virtual FEdMode* GetEditorMode() const override;

	virtual TSharedPtr<SWidget> GetInlineContent() const override;

private:

	TSharedPtr<SWidget> InlineWidget;
	TWeakObjectPtr<ATileMapTerrainActor> MergeTarget;
};
