// Copyright Epic Games, Inc. All Rights Reserved.

#include "Pixel2DTileMapFactory.h"
#include "Pixel2DTileMap.h"
#include "Pixel2DImporterSettings.h"


#define LOCTEXT_NAMESPACE "Pixel2D"

/////////////////////////////////////////////////////
// UPixel2DTileMapFactory

UPixel2DTileMapFactory::UPixel2DTileMapFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UPixel2DTileMap::StaticClass();
}

UObject* UPixel2DTileMapFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UPixel2DTileMap* NewTileMap = NewObject<UPixel2DTileMap>(InParent, Class, Name, Flags | RF_Transactional);

	GetDefault<UPixel2DImporterSettings>()->ApplySettingsForTileMapInit(NewTileMap, InitialTileSet);

	return NewTileMap;
}

#undef LOCTEXT_NAMESPACE
