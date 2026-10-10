#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ReditusCombatModifiers.generated.h"

/**
 * Generic combat modifiers.
 *
 * A modifier can come from:
 * - status effects
 * - equipment
 * - passives
 * - skill properties
 * - QTE results
 * - AMP
 * - Overdrive
 * - enemy phases
 * - temporary battle rules
 *
 * The damage calculator does not need to know where the modifier came from.
 */
UENUM(BlueprintType)
enum class EReditusCombatModifierType : uint8
{
	OutgoingDamageBonus		UMETA(DisplayName = "Outgoing Damage Bonus"),
	PhysicalDamageBonus		UMETA(DisplayName = "Physical Damage Bonus"),
	MagicalDamageBonus		UMETA(DisplayName = "Magical Damage Bonus"),
	PureDamageBonus			UMETA(DisplayName = "Pure Damage Bonus"),
	HealingBonus			UMETA(DisplayName = "Healing Bonus"),

	CriticalChanceBonus		UMETA(DisplayName = "Critical Chance Bonus"),
	CriticalDamageBonus		UMETA(DisplayName = "Critical Damage Bonus"),

	WeaknessDamageBonus		UMETA(DisplayName = "Weakness Damage Bonus"),

	IncomingDamageBonus		UMETA(DisplayName = "Incoming Damage Bonus"),
	IncomingDamageReduction UMETA(DisplayName = "Incoming Damage Reduction"),

	IncomingCriticalChanceBonus
		UMETA(DisplayName = "Incoming Critical Chance Bonus")
};

/**
 * One modifier entry.
 *
 * For damage/healing bonuses, Value is a decimal:
 *   +25% = 0.25
 *   -20% = -0.20
 *
 * For CriticalChanceBonus / IncomingCriticalChanceBonus,
 * Value is percentage POINTS:
 *   +10% critical chance = 10.0
 *
 * SourceID is optional but recommended for debugging.
 */
USTRUCT(BlueprintType)
struct CHAPTER1REDITUS_API FReditusCombatModifier
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Modifier")
	FName SourceID = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Modifier")
	EReditusCombatModifierType Type =
		EReditusCombatModifierType::OutgoingDamageBonus;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Modifier")
	float Value = 0.0f;
};

USTRUCT(BlueprintType)
struct CHAPTER1REDITUS_API FReditusCombatModifierTotals
{
	GENERATED_BODY()

public:

	UPROPERTY(BlueprintReadOnly, Category = "Combat Modifier")
	float OutgoingDamageBonus = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Modifier")
	float PhysicalDamageBonus = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Modifier")
	float MagicalDamageBonus = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Modifier")
	float PureDamageBonus = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Modifier")
	float HealingBonus = 0.0f;

	// Percentage points.
	UPROPERTY(BlueprintReadOnly, Category = "Combat Modifier")
	float CriticalChanceBonus = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Modifier")
	float CriticalDamageBonus = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Modifier")
	float WeaknessDamageBonus = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Modifier")
	float IncomingDamageBonus = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Modifier")
	float IncomingDamageReduction = 0.0f;

	// Percentage points.
	UPROPERTY(BlueprintReadOnly, Category = "Combat Modifier")
	float IncomingCriticalChanceBonus = 0.0f;
};

UCLASS()
class CHAPTER1REDITUS_API UReditusCombatModifierLibrary
	: public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintPure, Category = "Reditus|Combat|Modifiers")
	static FReditusCombatModifierTotals AccumulateCombatModifiers(
		const TArray<FReditusCombatModifier>& Modifiers
	);

	UFUNCTION(BlueprintPure, Category = "Reditus|Combat|Modifiers")
	static TArray<FReditusCombatModifier> CombineCombatModifierArrays(
		const TArray<FReditusCombatModifier>& A,
		const TArray<FReditusCombatModifier>& B
	);
};
