#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ReditusCombatMath.generated.h"

UCLASS()
class CHAPTER1REDITUS_API UReditusCombatMath : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "Reditus|Combat|Damage")
	static int32 RollPhysicalAttack(int32 Attack);

	UFUNCTION(BlueprintCallable, Category = "Reditus|Combat|Damage")
	static int32 RollMagicalAttack(int32 MagicalAttack);
};
