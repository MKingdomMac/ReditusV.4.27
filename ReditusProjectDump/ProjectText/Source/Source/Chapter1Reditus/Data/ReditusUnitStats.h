#pragma once

#include "CoreMinimal.h"
#include "ReditusUnitStats.generated.h"

/**
 * Static/base unit stats.
 *
 * Player-facing offense uses one value for each type:
 * - Attack
 * - MagicalAttack
 *
 * Random damage variation is applied at attack time.
 * MaxStamina is not part of this struct and is never difficulty-scaled.
 */
USTRUCT(BlueprintType)
struct CHAPTER1REDITUS_API FReditusUnitStats
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")
	int32 MaxHP = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")
	int32 MaxMP = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")
	int32 Attack = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")
	int32 MagicalAttack = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")
	int32 Defense = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")
	int32 Speed = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")
	int32 Hit = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")
	int32 MagicDefense = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")
	float ActionTime = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")
	float CriticalHit = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")
	float MaxOverdrivePoints = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")
	int32 MaxExhaustionPoints = 0;
};
