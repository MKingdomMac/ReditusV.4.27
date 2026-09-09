// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "Pixel2DTileMap.h"
#include "PaperFlipbookComponent.h"
#include "Pixel2DTileMapComponent.h"

#define LOCTEXT_NAMESPACE "Pixel2D"

//////////////////////////////////////////////////////////////////////////
// UPixel2DTileMap

UPixel2DTileMap::UPixel2DTileMap(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UPixel2DTileMap::PreSave(const class ITargetPlatform* TargetPlatform)
{
	Super::PreSave(TargetPlatform);

#if WITH_EDITOR
	for (TObjectIterator<UPixel2DTileMapComponent> TileMapIt; TileMapIt; ++TileMapIt)
	{
		if (UPixel2DTileMapComponent* TestComponent = *TileMapIt)
		{
			if (TestComponent->TileMap == this)
			{
				TestComponent->SpawnFlipbooks();
			}
		}
	}
#endif
}

void UPixel2DTileMap::AddFlipbook(UPaperFlipbook* Flipbook, UPaperTileLayer* Layer, FIntPoint Location)
{
	FPixelPaintedFlipbook NewEntry{ Flipbook, Layer, Location };

	FPixelPaintedFlipbook* FoundEntry = Flipbooks.FindByPredicate([&NewEntry](FPixelPaintedFlipbook Entry)
		{ return (Entry.Layer == NewEntry.Layer) && (Entry.Location == NewEntry.Location); });

	if (FoundEntry != nullptr)
	{
		FoundEntry->Source = NewEntry.Source;
	}
	else
	{
		Flipbooks.Add(NewEntry);
	}
}

void UPixel2DTileMap::RemoveFlipbook(UPaperTileLayer* Layer, FIntPoint Location)
{
	Flipbooks.RemoveAll([&](FPixelPaintedFlipbook& Entry)
		{ return ((Entry.Layer == Layer) || (Layer == nullptr)) && (Entry.Location == Location); });
}

UPaperFlipbook* UPixel2DTileMap::GetFlipbook(UPaperTileLayer* Layer, FIntPoint Location)
{
	FPixelPaintedFlipbook* FoundEntry = Flipbooks.FindByPredicate([&](FPixelPaintedFlipbook& Entry)
		{ return ((Entry.Layer == Layer) || (Layer == nullptr)) && (Entry.Location == Location); });

	if (FoundEntry != nullptr)
	{
		return FoundEntry->Source;
	}

	return nullptr;
}

void UPixel2DTileMap::ClearAllFlipbooks()
{
	Flipbooks.Reset();
}

void UPixel2DTileMap::DeleteLayer(int32 DeleteIndex)
{
	check(DeleteIndex <= TileLayers.Num());

	UPaperTileLayer* DeletedLayer = TileLayers[DeleteIndex];
	Flipbooks.RemoveAllSwap([&](FPixelPaintedFlipbook& Entry)
		{
			return (Entry.Layer == DeletedLayer);
		});

	TileLayers.RemoveAt(DeleteIndex);
}

UPaperTileLayer* UPixel2DTileMap::DuplicateLayer(int32 DuplicateIndex)
{
	check(DuplicateIndex <= TileLayers.Num());

	UPaperTileLayer* SourceLayer = TileLayers[DuplicateIndex];
	UPaperTileLayer* NewLayer = DuplicateObject<UPaperTileLayer>(SourceLayer, this);
	TileLayers.Insert(NewLayer, DuplicateIndex);

	const int32 PreNumFlipbooks = Flipbooks.Num();
	for (int32 Index = 0; Index < PreNumFlipbooks; Index++)
	{
		if (Flipbooks[Index].Layer == SourceLayer)
		{
			Flipbooks.Add({ Flipbooks[Index].Source, NewLayer, Flipbooks[Index].Location });
		}
	}

	return NewLayer;
}

#if WITH_EDITOR

void UPixel2DTileMap::PruneOutOfRangeFlipbooks()
{
	Flipbooks.RemoveAllSwap([this](FPixelPaintedFlipbook& Entry)
		{
			return (this->MapHeight <= Entry.Location.Y) || (this->MapWidth <= Entry.Location.X);
		});
}

void UPixel2DTileMap::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	PruneOutOfRangeFlipbooks();

	for (TObjectIterator<UPixel2DTileMapComponent> TileMapIt; TileMapIt; ++TileMapIt)
	{
		if (UPixel2DTileMapComponent* TestComponent = *TileMapIt)
		{
			if (TestComponent->TileMap == this)
			{
				TestComponent->RefreshPreview();
			}
		}
	}

}
#endif

#undef LOCTEXT_NAMESPACE