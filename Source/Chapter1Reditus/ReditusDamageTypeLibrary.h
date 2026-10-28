#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ReditusDamageCalculator.h"
#include "ReditusDamageTypeLibrary.generated.h"

/** Adapter for the existing Blueprint D_DamageType asset.
 * Keep that asset and the native request struct unchanged.
 */
UCLASS()
class CHAPTER1REDITUS_API UReditusDamageTypeLibrary
	: public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Explicit mapping verified against the 2026-10-26 project dump:
	 * 0 Physical, 1 Magical, 2 Pure, 3 Healing.
	 * Feed a byte converted from D_DamageType, not its display-name string.
	 * ReturnValue must be true before using DamageKind. Invalid values produce
	 * a deterministic Physical output plus false, rather than an invalid enum.
	 */
	UFUNCTION(BlueprintPure, Category = "Reditus|Combat|Damage",
		meta = (DisplayName = "Try Convert D_DamageType"))
	static bool TryConvertLegacyDamageType(
		uint8 LegacyDamageTypeValue,
		EReditusDamageKind& DamageKind
	);

	/**
	 * Alternative entry point when the caller has D_DamageType rather than a
	 * native damage kind. Copies Request, overrides only DamageKind, and calls
	 * the existing calculator exactly once. Request itself is not modified.
	 *
	 * An invalid type returns bValidDamageType=false and a missed, zero-amount
	 * result. The caller must handle that output before applying battle effects.
	 */
	UFUNCTION(BlueprintCallable, Category = "Reditus|Combat|Damage",
		meta = (DisplayName = "Calculate Damage From D_DamageType"))
	static FReditusDamageResult CalculateDamageFromLegacyType(
		uint8 LegacyDamageTypeValue,
		const FReditusDamageRequest& Request,
		bool& bValidDamageType
	);
};
