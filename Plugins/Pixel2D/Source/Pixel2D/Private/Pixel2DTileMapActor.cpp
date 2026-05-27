// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "Pixel2DTileMapActor.h"
#include "Pixel2DTileMapComponent.h"

#define LOCTEXT_NAMESPACE "Pixel2D"

//////////////////////////////////////////////////////////////////////////
// APixel2DTileMapActor

APixel2DTileMapActor::APixel2DTileMapActor(const FObjectInitializer& ObjectInitializer)
	: AActor(ObjectInitializer)
{
	RenderComponent = CreateDefaultSubobject<UPixel2DTileMapComponent>(TEXT("RenderComponent"));

	RootComponent = RenderComponent;
}

#if WITH_EDITOR
bool APixel2DTileMapActor::GetReferencedContentObjects(TArray<UObject*>& Objects) const
{
	AActor::GetReferencedContentObjects(Objects);

	if (const UObject* Asset = RenderComponent->AdditionalStatObject())
	{
		Objects.Add(const_cast<UObject*>(Asset));
	}
	return true;
}
#endif

#undef LOCTEXT_NAMESPACE