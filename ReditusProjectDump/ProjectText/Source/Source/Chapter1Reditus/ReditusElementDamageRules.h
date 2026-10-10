#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Data/ReditusEnemyDefinition.h"
#include "ReditusElementDamageRules.generated.h"

USTRUCT(BlueprintType)
struct CHAPTER1REDITUS_API FReditusElementDamageResult
{
	GENERATED_BODY()

public:

	UPROPERTY(BlueprintReadOnly, Category = "Reditus|Combat|Element")
	EReditusElementResponse Response = EReditusElementResponse::Normal;

	// Damage that should be applied to the target.
	UPROPERTY(BlueprintReadOnly, Category = "Reditus|Combat|Element")
	int32 Damage = 0;

	// Healing that should be applied instead when Response == Absorb.
	UPROPERTY(BlueprintReadOnly, Category = "Reditus|Combat|Element")
	int32 Healing = 0;
};

/**
 * Central interpretation of Weak / Normal / Resist / Null / Absorb.
 */
UCLASS()
class CHAPTER1REDITUS_API UReditusElementDamageRules : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 * Missing element entries default to Normal.
	 */
	UFUNCTION(BlueprintPure, Category = "Reditus|Combat|Element")
	static EReditusElementResponse GetElementResponse(
		const TMap<EReditusElementAffinity, EReditusElementResponse>& ElementResponses,
		EReditusElementAffinity Element
	);

	/**
	 * Returns the numeric damage multiplier for normal damage-producing responses.
	 * Null and Absorb return 0 here because they are handled explicitly.
	 */
	UFUNCTION(BlueprintPure, Category = "Reditus|Combat|Element")
	static float GetElementDamageMultiplier(
		EReditusElementResponse Response
	);

	/**
	 * Resolves already-calculated incoming elemental damage.
	 *
	 * Weak   -> 1.5x damage
	 * Normal -> 1.0x damage
	 * Resist -> 0.5x damage
	 * Null   -> 0 damage
	 * Absorb -> 0 damage + healing
	 */
	UFUNCTION(BlueprintPure, Category = "Reditus|Combat|Element")
	static FReditusElementDamageResult ResolveElementalDamage(
		int32 IncomingDamage,
		EReditusElementResponse Response
	);

	/**
	 * Convenience function: looks up the element in the map and resolves damage.
	 * Missing entries are Normal.
	 */
	UFUNCTION(BlueprintPure, Category = "Reditus|Combat|Element")
	static FReditusElementDamageResult ResolveElementalDamageFromMap(
		int32 IncomingDamage,
		const TMap<EReditusElementAffinity, EReditusElementResponse>& ElementResponses,
		EReditusElementAffinity Element
	);
};
