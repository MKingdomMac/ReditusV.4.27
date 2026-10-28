#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ReditusEnemyVisualLibrary.generated.h"

class USceneComponent;

UCLASS()
class CHAPTER1REDITUS_API UReditusEnemyVisualLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * Apply after assigning the enemy's Pixel2D animation.
     * Z is the component template's relative Z plus ZOffset; current X/Y stay
     * unchanged. Repeated calls do not accumulate the offset. The actor's
     * root/collision position is not moved by this function.
     */
    UFUNCTION(BlueprintCallable, Category = "Reditus|Enemies|Animation",
        meta = (DisplayName = "Apply Enemy Pixel2D Z Offset"))
    static bool ApplyEnemyPixel2DZOffset(
        USceneComponent* Pixel2DComponent,
        float ZOffset,
        FString& OutError
    );
};
