#include "ReditusDamageCalculator.h"
#include "ReditusCombatMath.h"
#include "ReditusCombatTuning.h"

int32 UReditusDamageCalculator::RollPhysicalAttack(int32 Attack)
{
	return UReditusCombatMath::RollPhysicalAttack(Attack);
}

int32 UReditusDamageCalculator::RollMagicalAttack(int32 MagicalAttack)
{
	return UReditusCombatMath::RollMagicalAttack(MagicalAttack);
}

EReditusElementResponse UReditusDamageCalculator::GetElementResponse(
	const TMap<EReditusElementAffinity, EReditusElementResponse>& Responses,
	EReditusElementAffinity Element)
{
	if (const EReditusElementResponse* Found = Responses.Find(Element))
	{
		return *Found;
	}

	// Missing map entry = normal damage.
	return EReditusElementResponse::Normal;
}

FReditusDamageResult UReditusDamageCalculator::CalculateDamage(
	const FReditusDamageRequest& Request)
{
	FReditusDamageResult Result;

	Result.bHit = Request.bAttackHit;
	Result.bTargetWasStaggered = Request.bTargetStaggered;
	Result.bBlocked = Request.bTargetDefending;

	// ============================================================
	// 1. GATHER MODIFIERS
	// ============================================================

	TArray<FReditusCombatModifier> CombinedAttackerModifiers;
	CombinedAttackerModifiers.Reserve(
		Request.AttackerModifiers.Num() +
		Request.ActionModifiers.Num());

	CombinedAttackerModifiers.Append(Request.AttackerModifiers);
	CombinedAttackerModifiers.Append(Request.ActionModifiers);

	Result.AttackerModifierTotals =
		UReditusCombatModifierLibrary::AccumulateCombatModifiers(
			CombinedAttackerModifiers);

	Result.TargetModifierTotals =
		UReditusCombatModifierLibrary::AccumulateCombatModifiers(
			Request.TargetModifiers);

	// ============================================================
	// 2. MISS
	// ============================================================

	if (!Request.bAttackHit)
	{
		Result.FinalDamage = 0;
		Result.Healing = 0;
		return Result;
	}

	// ============================================================
	// 3. BASE OFFENSE
	// ============================================================

	switch (Request.DamageKind)
	{
	case EReditusDamageKind::Physical:
		Result.RolledOffense =
			RollPhysicalAttack(Request.Attack);
		break;

	case EReditusDamageKind::Magical:
		Result.RolledOffense =
			RollMagicalAttack(Request.MagicalAttack);
		break;

	case EReditusDamageKind::Pure:
	case EReditusDamageKind::Healing:
		Result.RolledOffense =
			FMath::Max(0, Request.FlatAmount);
		break;

	default:
		Result.RolledOffense = 0;
		break;
	}

	Result.RolledOffense = FMath::Max(
		0,
		FMath::RoundToInt(
			static_cast<float>(Result.RolledOffense) *
			FMath::Max(0.0f, Request.PowerMultiplier)
		)
	);

	// ============================================================
	// 4. HEALING
	// ============================================================

	if (Request.DamageKind == EReditusDamageKind::Healing)
	{
		const float HealingMultiplier = FMath::Max(
			0.0f,
			1.0f +
			Result.AttackerModifierTotals.HealingBonus);

		Result.FinalMultiplier = HealingMultiplier;

		Result.Healing = FMath::Max(
			0,
			FMath::RoundToInt(
				static_cast<float>(Result.RolledOffense) *
				HealingMultiplier
			)
		);

		return Result;
	}

	// ============================================================
	// 5. DEFENSE
	// ============================================================

	switch (Request.DamageKind)
	{
	case EReditusDamageKind::Physical:
		Result.DamageAfterDefense = FMath::Max(
			1,
			Result.RolledOffense -
			FMath::Max(0, Request.TargetDefense)
		);
		break;

	case EReditusDamageKind::Magical:
		Result.DamageAfterDefense = FMath::Max(
			1,
			Result.RolledOffense -
			FMath::Max(0, Request.TargetMagicDefense)
		);
		break;

	case EReditusDamageKind::Pure:
		Result.DamageAfterDefense =
			FMath::Max(0, Result.RolledOffense);
		break;

	default:
		Result.DamageAfterDefense = 0;
		break;
	}

	// ============================================================
	// 6. ELEMENT RESPONSE
	// ============================================================

	Result.ElementResponse =
		Request.bApplyElementResponse
		? GetElementResponse(
			Request.TargetElementResponses,
			Request.Element)
		: EReditusElementResponse::Normal;

	Result.bWeakness =
		Result.ElementResponse == EReditusElementResponse::Weak;

	Result.bResisted =
		Result.ElementResponse == EReditusElementResponse::Resist;

	Result.bNullified =
		Result.ElementResponse == EReditusElementResponse::Null;

	Result.bAbsorbed =
		Result.ElementResponse == EReditusElementResponse::Absorb;

	if (Result.bNullified)
	{
		Result.FinalDamage = 0;
		Result.Healing = 0;
		Result.FinalMultiplier = 0.0f;
		return Result;
	}

	// ============================================================
	// 7. CRITICAL CHANCE
	// ============================================================

	Result.EffectiveCriticalChancePercent = FMath::Clamp(
		Request.CriticalChancePercent +
		Result.AttackerModifierTotals.CriticalChanceBonus +
		Result.TargetModifierTotals.IncomingCriticalChanceBonus,
		0.0f,
		100.0f);

	if (Request.bCanCritical)
	{
		// Core Reditus stagger rule:
		// staggered target = guaranteed critical hit.
		Result.bCritical =
			Request.bTargetStaggered ||
			Request.bForceCritical ||
			(FMath::FRandRange(0.0f, 100.0f) <
			 Result.EffectiveCriticalChancePercent);
	}

	// ============================================================
	// 8. ADDITIVE DAMAGE BONUSES
	//
	// Weak + Crit ADDS:
	// 1.0 + 0.5 Weak + 0.5 Crit = 2.0
	// ============================================================

	float AdditiveDamageBonus =
		Result.AttackerModifierTotals.OutgoingDamageBonus +
		Result.TargetModifierTotals.IncomingDamageBonus -
		Result.TargetModifierTotals.IncomingDamageReduction;

	switch (Request.DamageKind)
	{
	case EReditusDamageKind::Physical:
		AdditiveDamageBonus +=
			Result.AttackerModifierTotals.PhysicalDamageBonus;
		break;

	case EReditusDamageKind::Magical:
		AdditiveDamageBonus +=
			Result.AttackerModifierTotals.MagicalDamageBonus;
		break;

	case EReditusDamageKind::Pure:
		AdditiveDamageBonus +=
			Result.AttackerModifierTotals.PureDamageBonus;
		break;

	default:
		break;
	}

	if (Result.bWeakness)
	{
		AdditiveDamageBonus +=
			ReditusCombatTuning::ElementWeakDamageBonus +
			Result.AttackerModifierTotals.WeaknessDamageBonus;
	}

	if (Result.bCritical)
	{
		AdditiveDamageBonus +=
			ReditusCombatTuning::BaseCriticalDamageBonus +
			Result.AttackerModifierTotals.CriticalDamageBonus;
	}

	const float AdditiveDamageMultiplier =
		FMath::Max(0.0f, 1.0f + AdditiveDamageBonus);

	// ============================================================
	// 9. RESIST
	// ============================================================

	const float ElementMultiplier =
		Result.bResisted
		? ReditusCombatTuning::ElementResistDamageMultiplier
		: ReditusCombatTuning::ElementNormalDamageMultiplier;

	// ============================================================
	// 10. DEFENDING
	// ============================================================

	const float DefendingMultiplier =
		Request.bTargetDefending
		? ReditusCombatTuning::DefendingDamageMultiplier
		: 1.0f;

	Result.FinalMultiplier =
		AdditiveDamageMultiplier *
		ElementMultiplier *
		DefendingMultiplier;

	const int32 ResolvedAmount = FMath::Max(
		0,
		FMath::RoundToInt(
			static_cast<float>(Result.DamageAfterDefense) *
			Result.FinalMultiplier
		)
	);

	// ============================================================
	// 11. ABSORB OR DAMAGE
	// ============================================================

	if (Result.bAbsorbed)
	{
		Result.FinalDamage = 0;

		Result.Healing = FMath::Max(
			0,
			FMath::RoundToInt(
				static_cast<float>(ResolvedAmount) *
				ReditusCombatTuning::ElementAbsorbHealingMultiplier
			)
		);

		return Result;
	}

	Result.FinalDamage =
		Result.DamageAfterDefense > 0
		? FMath::Max(1, ResolvedAmount)
		: 0;

	Result.Healing = 0;
	return Result;
}
