// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/EngineTypes.h"
#include "PaperFlipbookComponent.h"
#include "Pixel2DFlipbookComponent.generated.h"


UCLASS(ShowCategories = (Mobility, ComponentReplication), meta = (BlueprintSpawnableComponent))
class PIXEL2D_API UPixel2DFlipbookComponent : public UPaperFlipbookComponent
{
	GENERATED_UCLASS_BODY()

public:
	UPROPERTY(Category=Bounds, EditAnywhere)
	bool bAdjustBounds;
};