// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "PaperTileMapActor.h"
#include "Pixel2DTileMapComponent.h"
#include "Pixel2DTileMapActor.generated.h"

/**
* Implements Pixel2DTileMapActor
*/

UCLASS(ComponentWrapperClass, HideCategories = ("Tile Map"))
class PIXEL2D_API APixel2DTileMapActor : public AActor//APaperTileMapActor
{
//	GENERATED_UCLASS_BODY()
//
//public:
//	FORCEINLINE class UPixel2DTileMapComponent* GetRenderComponent() const
//	{
//		return CastChecked<class UPixel2DTileMapComponent>(APaperTileMapActor::GetRenderComponent());
//	}

	GENERATED_UCLASS_BODY()

private:
	UPROPERTY(Category = PixelTileMapActor, VisibleAnywhere, BlueprintReadOnly, meta = (ExposeFunctionCategories = "Sprite,Rendering,Physics,Components|Sprite", AllowPrivateAccess = "true"))
	class UPixel2DTileMapComponent* RenderComponent;
public:

	// AActor interface
#if WITH_EDITOR
	virtual bool GetReferencedContentObjects(TArray<UObject*>& Objects) const override;
#endif
	// End of AActor interface

	/** Returns RenderComponent subobject **/
	FORCEINLINE class UPixel2DTileMapComponent* GetRenderComponent() const { return RenderComponent; }

};
