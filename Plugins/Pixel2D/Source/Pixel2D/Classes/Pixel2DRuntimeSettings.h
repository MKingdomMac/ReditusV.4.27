// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Pixel2DRuntimeSettings.generated.h"

/**
* Implements the settings for the Pixel2D plugin.
*/
UCLASS(config = Engine, defaultconfig)
class PIXEL2D_API UPixel2DRuntimeSettings : public UObject
{
	GENERATED_UCLASS_BODY()

public:

#if WITH_EDITORONLY_DATA
	/** Tile replacer data table. Note: You need to restart the editor when enabling this setting for the change to fully take effect. **/
	UPROPERTY(config, noclear, EditAnywhere, Category = TileReplacer, meta = (AllowedClasses = "DataTable", ConfigRestartRequired = true))
	FSoftObjectPath TileReplacerDataTable;

#endif

};
