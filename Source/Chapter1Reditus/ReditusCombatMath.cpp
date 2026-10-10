#include "ReditusCombatMath.h"
#include "ReditusCombatTuning.h"

namespace
{
	static int32 RollAttackValue(int32 BaseValue, float ConfiguredMin, float ConfiguredMax)
	{
		if (BaseValue <= 0)
		{
			return 0;
		}

		const float MinRoll = FMath::Min(ConfiguredMin, ConfiguredMax);
		const float MaxRoll = FMath::Max(ConfiguredMin, ConfiguredMax);

		return FMath::Max(
			0,
			FMath::RoundToInt(
				BaseValue * FMath::FRandRange(MinRoll, MaxRoll)
			)
		);
	}
}

int32 UReditusCombatMath::RollPhysicalAttack(int32 Attack)
{
	return RollAttackValue(
		Attack,
		ReditusCombatTuning::PhysicalAttackRollMin,
		ReditusCombatTuning::PhysicalAttackRollMax);
}

int32 UReditusCombatMath::RollMagicalAttack(int32 MagicalAttack)
{
	return RollAttackValue(
		MagicalAttack,
		ReditusCombatTuning::MagicalAttackRollMin,
		ReditusCombatTuning::MagicalAttackRollMax);
}
