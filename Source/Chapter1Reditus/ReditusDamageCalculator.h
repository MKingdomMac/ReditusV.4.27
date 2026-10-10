#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Data/ReditusEnemyDefinition.h"
#include "ReditusCombatModifiers.h"
#include "ReditusDamageCalculator.generated.h"

UENUM(BlueprintType)
enum class EReditusDamageKind : uint8
{
	Physical	UMETA(DisplayName = "Physical"),
	Magical		UMETA(DisplayName = "Magical"),
	Pure		UMETA(DisplayName = "Pure"),
	Healing		UMETA(DisplayName = "Healing")
};

/**
 * Everything needed to resolve ONE damage/healing event.
 *
 * Important architecture rule:
 * The calculator does NOT search the attacker for statuses/equipment/etc.
 * Those systems contribute FReditusCombatModifier entries before this call.
 */
USTRUCT(BlueprintType)
struct CHAPTER1REDITUS_API FReditusDamageRequest
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	EReditusDamageKind DamageKind = EReditusDamageKind::Physical;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	EReditusElementAffinity Element = EReditusElementAffinity::Phys;

	// ============================================================
	// STATS
	// ============================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage|Stats")
	int32 Attack = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage|Stats")
	int32 MagicalAttack = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage|Stats")
	int32 TargetDefense = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage|Stats")
	int32 TargetMagicDefense = 0;

	/**
	 * Used by Pure and Healing.
	 * Physical/Magical derive offense from Attack/MagicalAttack.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage|Stats")
	int32 FlatAmount = 0;

	/**
	 * Skill/attack power.
	 * 1.0 = 100%, 1.5 = 150%, etc.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage|Skill",
		meta = (ClampMin = "0.0"))
	float PowerMultiplier = 1.0f;

	// ============================================================
	// HIT / CRITICAL
	// ============================================================

	/**
	 * For the first migration pass, your existing IsHit logic can feed this.
	 * Later we can move hit calculation into C++ too.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage|Hit")
	bool bAttackHit = true;

	/**
	 * Percentage from 0-100.
	 * Example: 12.5 = 12.5% critical chance.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage|Critical")
	float CriticalChancePercent = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage|Critical")
	bool bCanCritical = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage|Critical")
	bool bForceCritical = false;

	// ============================================================
	// TARGET STATE
	// ============================================================

	/**
	 * Stagger has no separate damage multiplier.
	 * It guarantees a critical hit when bCanCritical is true.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage|State")
	bool bTargetStaggered = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage|State")
	bool bTargetDefending = false;

	// ============================================================
	// ELEMENT
	// ============================================================

	/**
	 * Pure damage can set this false when it should ignore affinity entirely.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage|Element")
	bool bApplyElementResponse = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage|Element")
	TMap<EReditusElementAffinity, EReditusElementResponse>
		TargetElementResponses;

	// ============================================================
	// MODIFIERS
	// ============================================================

	/**
	 * Statuses/equipment/passives/etc. affecting the attacker.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage|Modifiers")
	TArray<FReditusCombatModifier> AttackerModifiers;

	/**
	 * Vulnerability/protection/etc. affecting the target.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage|Modifiers")
	TArray<FReditusCombatModifier> TargetModifiers;

	/**
	 * THIS action only:
	 * skill modifier, QTE result, AMP spend, Overdrive, etc.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage|Modifiers")
	TArray<FReditusCombatModifier> ActionModifiers;
};

USTRUCT(BlueprintType)
struct CHAPTER1REDITUS_API FReditusDamageResult
{
	GENERATED_BODY()

public:

	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	int32 FinalDamage = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	int32 Healing = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	int32 RolledOffense = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	int32 DamageAfterDefense = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	float FinalMultiplier = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	float EffectiveCriticalChancePercent = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	EReditusElementResponse ElementResponse =
		EReditusElementResponse::Normal;

	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	bool bHit = true;

	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	bool bCritical = false;

	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	bool bWeakness = false;

	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	bool bResisted = false;

	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	bool bNullified = false;

	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	bool bAbsorbed = false;

	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	bool bTargetWasStaggered = false;

	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	bool bBlocked = false;

	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	FReditusCombatModifierTotals AttackerModifierTotals;

	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	FReditusCombatModifierTotals TargetModifierTotals;
};

UCLASS()
class CHAPTER1REDITUS_API UReditusDamageCalculator
	: public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "Reditus|Combat|Damage")
	static FReditusDamageResult CalculateDamage(
		const FReditusDamageRequest& Request
	);

private:

	static int32 RollPhysicalAttack(int32 Attack);
	static int32 RollMagicalAttack(int32 MagicalAttack);

	static EReditusElementResponse GetElementResponse(
		const TMap<EReditusElementAffinity, EReditusElementResponse>& Responses,
		EReditusElementAffinity Element
	);
};
