// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Math/IntPoint.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "PaperTileMap.h"
#include "PaperSprite.h"
#include "PaperFlipbook.h"
#include "PaperTileLayer.h"
#include "Pixel2DTileMap.generated.h"

/**
* Implements Pixel2DTileMap
*/

USTRUCT()
struct FPixelPaintedFlipbook
{
public:
	GENERATED_USTRUCT_BODY()

	UPROPERTY(Instanced, EditAnywhere, Category = PaintedFlipbook)
	UPaperFlipbook* Source;

	UPROPERTY()
	UPaperTileLayer* Layer;

	UPROPERTY()
	FIntPoint Location;

	int32 GetId() const
	{
		ensure(Layer != nullptr);

		const uint32 LayerHash = FCrc::StrCrc32<TCHAR>(*Layer->GetName(), 0);
		const int32 MaxWidth = Layer->GetLayerWidth();
		const int32 MaxHeight = Layer->GetLayerHeight();

		return LayerHash * MaxWidth * MaxHeight + Location.X * MaxHeight + Location.Y;
	}
};

class UPaperFlipbookComponent;

UCLASS(BlueprintType)
class PIXEL2D_API UPixel2DTileMap : public UPaperTileMap
{
	GENERATED_UCLASS_BODY()

public:
	// The set of key frames for this flipbook animation (each one has a duration and a sprite to display)
	UPROPERTY(Category = Sprite, EditAnywhere)
	TArray<FPixelPaintedFlipbook> Flipbooks;

public:
	// UObject
	virtual void PreSave(const class ITargetPlatform* TargetPlatform) override;

	void AddFlipbook(UPaperFlipbook* Flipbook, UPaperTileLayer* Layer, FIntPoint Location);
	void RemoveFlipbook(UPaperTileLayer* Layer, FIntPoint Location);
	UPaperFlipbook* GetFlipbook(UPaperTileLayer* Layer, FIntPoint Location);
	void ClearAllFlipbooks();

	int NumFlipbooks() { return Flipbooks.Num(); }

	template <class Predicate>
	void ForEachFlipbook(Predicate&& Action)
	{
		for (FPixelPaintedFlipbook& Flipbook : Flipbooks)
		{
			Action(Flipbook);
		}
	}

	void DeleteLayer(int32 DeleteIndex);
	UPaperTileLayer* DuplicateLayer(int32 DuplicateIndex);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	void PruneOutOfRangeFlipbooks();
#endif
};
