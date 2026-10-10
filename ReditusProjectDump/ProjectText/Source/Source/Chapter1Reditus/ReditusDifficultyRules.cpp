#include "ReditusDifficultyRules.h"
#include "ReditusCombatTuning.h"

FReditusUnitStats UReditusDifficultyRules::ApplyEnemyDifficultyToStats(
	const FReditusUnitStats& BaseStats,
	bool bEasyDifficulty,
	bool bIgnoreDifficultyScaling)
{
	if (!bEasyDifficulty || bIgnoreDifficultyScaling)
	{
		return BaseStats;
	}

	FReditusUnitStats Result = BaseStats;

	Result.MaxHP = FMath::CeilToInt(BaseStats.MaxHP * ReditusCombatTuning::EasyMaxHPMultiplier);
	Result.MaxMP = FMath::CeilToInt(BaseStats.MaxMP * ReditusCombatTuning::EasyMaxMPMultiplier);
	Result.Attack = FMath::CeilToInt(BaseStats.Attack * ReditusCombatTuning::EasyAttackMultiplier);
	Result.MagicalAttack = FMath::CeilToInt(BaseStats.MagicalAttack * ReditusCombatTuning::EasyMagicalAttackMultiplier);
	Result.Defense = FMath::CeilToInt(BaseStats.Defense * ReditusCombatTuning::EasyDefenseMultiplier);
	Result.Speed = FMath::CeilToInt(BaseStats.Speed * ReditusCombatTuning::EasySpeedMultiplier);
	Result.Hit = FMath::CeilToInt(BaseStats.Hit * ReditusCombatTuning::EasyHitMultiplier);
	Result.MagicDefense = FMath::CeilToInt(BaseStats.MagicDefense * ReditusCombatTuning::EasyMagicDefenseMultiplier);
	Result.ActionTime = BaseStats.ActionTime * ReditusCombatTuning::EasyActionTimeMultiplier;
	Result.CriticalHit = BaseStats.CriticalHit * ReditusCombatTuning::EasyCriticalHitMultiplier;
	Result.MaxOverdrivePoints = BaseStats.MaxOverdrivePoints * ReditusCombatTuning::EasyMaxOverdrivePointsMultiplier;
	Result.MaxExhaustionPoints = FMath::CeilToInt(
		BaseStats.MaxExhaustionPoints *
		ReditusCombatTuning::EasyMaxExhaustionPointsMultiplier);

	return Result;
}

bool UReditusDifficultyRules::IsEnemyAMPEnabledForDifficulty(bool bEasyDifficulty)
{
	return bEasyDifficulty
		? ReditusCombatTuning::bEnemyAMPEnabledOnEasy
		: ReditusCombatTuning::bEnemyAMPEnabledOnHard;
}

bool UReditusDifficultyRules::CanEnemyUseAMP(bool bCanUseAMP, bool bEasyDifficulty)
{
	return bCanUseAMP && IsEnemyAMPEnabledForDifficulty(bEasyDifficulty);
}
