// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Pixel2DTileMap.h"
#include "ComponentAssetBroker.h"
#include "Pixel2DTileMapComponent.h"

//////////////////////////////////////////////////////////////////////////
// FPixel2DTileMapAssetBroker

class FPixel2DTileMapAssetBroker : public IComponentAssetBroker
{
public:
	UClass* GetSupportedAssetClass() override
	{
		return UPixel2DTileMap::StaticClass();
	}

	virtual bool AssignAssetToComponent(UActorComponent* InComponent, UObject* InAsset) override
	{
		if (UPixel2DTileMapComponent* RenderComp = Cast<UPixel2DTileMapComponent>(InComponent))
		{
			UPixel2DTileMap* TileMap = Cast<UPixel2DTileMap>(InAsset);

			if ((TileMap != nullptr) || (InAsset == nullptr))
			{
				RenderComp->TileMap = TileMap;
				return true;
			}
		}

		return false;
	}

	virtual UObject* GetAssetFromComponent(UActorComponent* InComponent) override
	{
		if (UPixel2DTileMapComponent* RenderComp = Cast<UPixel2DTileMapComponent>(InComponent))
		{
			if ((RenderComp->TileMap != nullptr) && (RenderComp->TileMap->IsAsset()))
			{
				return RenderComp->TileMap;
			}
		}

		return nullptr;
	}
};

