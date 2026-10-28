#include "ReditusEnemyVisualLibrary.h"

#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"

bool UReditusEnemyVisualLibrary::ApplyEnemyPixel2DZOffset(
    USceneComponent* Pixel2DComponent,
    float ZOffset,
    FString& OutError)
{
    OutError.Reset();
    if (!IsValid(Pixel2DComponent) || Pixel2DComponent->IsTemplate())
    {
        OutError = TEXT("Pixel2D Z offset: supply the spawned enemy's visual component.");
        return false;
    }
    if (!FMath::IsFinite(ZOffset))
    {
        OutError = TEXT("Pixel2D Z offset: the row's offset must be finite.");
        return false;
    }

    const AActor* Owner = Pixel2DComponent->GetOwner();
    if (Owner && Owner->GetRootComponent() == Pixel2DComponent)
    {
        OutError = TEXT("Pixel2D Z offset: use the visual child component, not the actor's root.");
        return false;
    }

    // A spawned Blueprint component's archetype retains its authored relative
    // transform. Reading that baseline avoids adding the offset again whenever
    // the enemy is initialized/refreshed more than once.
    const USceneComponent* Template = Cast<USceneComponent>(Pixel2DComponent->GetArchetype());
    if (!IsValid(Template))
    {
        OutError = TEXT("Pixel2D Z offset: the visual component has no scene-component template.");
        return false;
    }

    FVector Location = Pixel2DComponent->GetRelativeLocation();
    Location.Z = Template->GetRelativeLocation().Z + ZOffset;
    if (Location.ContainsNaN())
    {
        OutError = TEXT("Pixel2D Z offset: the resulting relative location is invalid.");
        return false;
    }

    Pixel2DComponent->SetRelativeLocation(
        Location, false, nullptr, ETeleportType::TeleportPhysics);
    return true;
}
