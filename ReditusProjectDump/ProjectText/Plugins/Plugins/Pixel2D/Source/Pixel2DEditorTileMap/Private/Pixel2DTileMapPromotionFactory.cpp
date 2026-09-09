// Copyright Epic Games, Inc. All Rights Reserved.

#include "Pixel2DTileMapPromotionFactory.h"
#include "Pixel2DTileMap.h"

#define LOCTEXT_NAMESPACE "Pixel2D"

/////////////////////////////////////////////////////
// UPixel2DTileMapPromotionFactory

UPixel2DTileMapPromotionFactory::UPixel2DTileMapPromotionFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = false;
	bEditAfterNew = true;
	SupportedClass = UPixel2DTileMap::StaticClass();
}

UObject* UPixel2DTileMapPromotionFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	AssetToRename->SetFlags(Flags | RF_Transactional);
	AssetToRename->Modify();
	AssetToRename->Rename(*Name.ToString(), InParent);

	return AssetToRename;
}

#undef LOCTEXT_NAMESPACE
