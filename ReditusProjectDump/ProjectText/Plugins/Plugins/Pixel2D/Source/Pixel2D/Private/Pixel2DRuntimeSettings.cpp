// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "Pixel2DRuntimeSettings.h"

//////////////////////////////////////////////////////////////////////////
// UPixel2DRuntimeSettings

UPixel2DRuntimeSettings::UPixel2DRuntimeSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITORONLY_DATA
	, TileReplacerDataTable("/Game/Pixel2D/Blueprints/AnimatedTileReplacer/Pixel2DTileReplacerDataTable.Pixel2DTileReplacerDataTable")
#endif
{
}