// Copyright Epic Games, Inc. All Rights Reserved.

#include "Pixel2DTileMapActorFactory.h"
#include "AssetData.h"
#include "Pixel2DTileMapActor.h"
#include "Pixel2DTileMap.h"
#include "Pixel2DImporterSettings.h"
#include "Pixel2DTileMapComponent.h"
#include "PaperTileSet.h"

//////////////////////////////////////////////////////////////////////////
// UPixel2DTileMapActorFactory

UPixel2DTileMapActorFactory::UPixel2DTileMapActorFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DisplayName = NSLOCTEXT("Pixel2D", "Pixel2DTileMapFactoryDisplayName", "Pixel2D Tile Map");
	NewActorClass = APixel2DTileMapActor::StaticClass();
}

void UPixel2DTileMapActorFactory::PostSpawnActor(UObject* Asset, AActor* NewActor)
{
	Super::PostSpawnActor(Asset, NewActor);

	APixel2DTileMapActor* TypedActor = CastChecked<APixel2DTileMapActor>(NewActor);
	UPixel2DTileMapComponent* RenderComponent = TypedActor->GetRenderComponent();
	check(RenderComponent);

	if (UPixel2DTileMap* TileMapAsset = Cast<UPixel2DTileMap>(Asset))
	{
		RenderComponent->UnregisterComponent();
		RenderComponent->TileMap = TileMapAsset;
		RenderComponent->SpawnFlipbooks();
		RenderComponent->RegisterComponent();
	}
	else if (RenderComponent->OwnsTileMap())
	{
		RenderComponent->UnregisterComponent();

		UPixel2DTileMap* OwnedTileMap = RenderComponent->GetTileMap();
		check(OwnedTileMap);

		GetDefault<UPixel2DImporterSettings>()->ApplySettingsForTileMapInit(OwnedTileMap, Cast<UPaperTileSet>(Asset));

		RenderComponent->RegisterComponent();
	}
}

void UPixel2DTileMapActorFactory::PostCreateBlueprint(UObject* Asset, AActor* CDO)
{
	if (APixel2DTileMapActor* TypedActor = Cast<APixel2DTileMapActor>(CDO))
	{
		UPixel2DTileMapComponent* RenderComponent = TypedActor->GetRenderComponent();
		check(RenderComponent);

		if (UPixel2DTileMap* TileMap = Cast<UPixel2DTileMap>(Asset))
		{
			RenderComponent->TileMap = TileMap;
			RenderComponent->SpawnFlipbooks();
		}
		else if (RenderComponent->OwnsTileMap())
		{
			UPixel2DTileMap* OwnedTileMap = RenderComponent->GetTileMap();
			check(OwnedTileMap);

			GetDefault<UPixel2DImporterSettings>()->ApplySettingsForTileMapInit(OwnedTileMap, Cast<UPaperTileSet>(Asset));
		}
	}
}

bool UPixel2DTileMapActorFactory::CanCreateActorFrom(const FAssetData& AssetData, FText& OutErrorMsg)
{
	if (AssetData.IsValid())
	{
		UClass* AssetClass = AssetData.GetClass();
		if ((AssetClass != nullptr) && (AssetClass->IsChildOf(UPixel2DTileMap::StaticClass()) || AssetClass->IsChildOf(UPaperTileSet::StaticClass())))
		{
			return true;
		}
		else
		{
			OutErrorMsg = NSLOCTEXT("Pixel2D", "CanCreateActorFrom_NoTileMap", "No tile map was specified.");
			return false;
		}
	}
	else
	{
		return true;
	}
}
