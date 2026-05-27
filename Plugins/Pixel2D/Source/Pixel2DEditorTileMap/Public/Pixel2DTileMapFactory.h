// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

/**
 * Factory for tile maps
 */

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Factories/Factory.h"
#include "Pixel2DTileMapFactory.generated.h"

UCLASS()
class UPixel2DTileMapFactory : public UFactory
{
	GENERATED_UCLASS_BODY()

	// Initial tile set to create the tile map from (Can be nullptr)
	class UPaperTileSet* InitialTileSet;

	// UFactory interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	// End of UFactory interface
};
