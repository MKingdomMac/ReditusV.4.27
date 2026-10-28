#pragma once

#include "CoreMinimal.h"

/**
 * CENTRAL REDITUS COMBAT TUNING
 *
 * Global balance numbers live here.
 * Data/status/equipment systems describe WHAT is happening.
 * The calculator reads the values from here.
 */
namespace ReditusCombatTuning
{
	// ============================================================
	// EASY DIFFICULTY
	// ============================================================

	constexpr float EasyMaxHPMultiplier = 0.80f;
	constexpr float EasyMaxMPMultiplier = 1.00f;
	constexpr float EasyAttackMultiplier = 0.80f;
	constexpr float EasyMagicalAttackMultiplier = 1.00f;
	constexpr float EasyDefenseMultiplier = 1.00f;
	constexpr float EasySpeedMultiplier = 1.00f;
	constexpr float EasyHitMultiplier = 1.00f;
	constexpr float EasyMagicDefenseMultiplier = 1.00f;
	constexpr float EasyActionTimeMultiplier = 1.00f;
	constexpr float EasyCriticalHitMultiplier = 1.00f;
	constexpr float EasyMaxOverdrivePointsMultiplier = 1.00f;
	constexpr float EasyMaxExhaustionPointsMultiplier = 1.00f;

	// ============================================================
	// ATTACK RANDOMIZATION
	// ============================================================

	// Variance = Max(minimum, Round(base attack * percentage / 100)).
	// Applies to Attack and MagicalAttack before skill power and defense.
	constexpr int32 AttackRollVariancePercent = 10;
	constexpr int32 MinimumAttackRollVariance = 5;

	// ============================================================
	// ELEMENTAL RESPONSE
	// ============================================================
	//
	// DataTable stores:
	// Normal / Weak / Resist / Null / Absorb
	//
	// Weak is an additive +50% bonus so Weak + Critical can add:
	// 1.0 + 0.5 Weak + 0.5 Crit = 2.0x

	constexpr float ElementWeakDamageBonus = 0.50f;
	constexpr float ElementNormalDamageMultiplier = 1.00f;
	constexpr float ElementResistDamageMultiplier = 0.50f;
	constexpr float ElementAbsorbHealingMultiplier = 1.00f;

	// ============================================================
	// CRITICAL
	// ============================================================

	constexpr float BaseCriticalDamageBonus = 0.50f;

	// ============================================================
	// STAGGER
	// ============================================================
	//
	// IMPORTANT:
	// Stagger does NOT have its own damage multiplier.
	// A staggered target guarantees a critical hit for an attack
	// that is allowed to critically hit.
	//
	// With the default +50% critical bonus:
	// Normal + Stagger = 1.5x
	// Weak + Stagger   = 2.0x
	// because Weak (+0.5) and Critical (+0.5) are additive.

	// ============================================================
	// DEFENDING / BLOCKING
	// ============================================================

	constexpr float DefendingDamageMultiplier = 0.50f;

	// ============================================================
	// ENEMY AMP
	// ============================================================

	constexpr bool bEnemyAMPEnabledOnHard = true;
	constexpr bool bEnemyAMPEnabledOnEasy = false;
}
