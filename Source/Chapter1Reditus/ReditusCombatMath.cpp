#include "ReditusCombatMath.h"
#include "ReditusCombatTuning.h"

namespace
{
	static int32 RollAttackValue(int32 BaseValue)
	{
		if (BaseValue <= 0)
		{
			return 0;
		}

		// Round(BaseValue * percentage / 100), using exact integer arithmetic.
		// The wider intermediate keeps large attack values safe from overflow.
		const int64 Percentage = FMath::Max<int64>(
			0, ReditusCombatTuning::AttackRollVariancePercent);
		const int64 ScaledVariance =
			(static_cast<int64>(BaseValue) * Percentage + 50) / 100;
		const int64 MinimumVariance = FMath::Max<int64>(
			0, ReditusCombatTuning::MinimumAttackRollVariance);
		const int64 Variance = FMath::Max(MinimumVariance, ScaledVariance);

		const int32 MinRoll = static_cast<int32>(FMath::Clamp<int64>(
			static_cast<int64>(BaseValue) - Variance,
			0,
			static_cast<int64>(MAX_int32)));
		const int32 MaxRoll = static_cast<int32>(FMath::Clamp<int64>(
			static_cast<int64>(BaseValue) + Variance,
			0,
			static_cast<int64>(MAX_int32)));

		// One inclusive integer roll. The calculator applies power and defense next.
		return FMath::RandRange(MinRoll, MaxRoll);
	}
}

int32 UReditusCombatMath::RollPhysicalAttack(int32 Attack)
{
	return RollAttackValue(Attack);
}

int32 UReditusCombatMath::RollMagicalAttack(int32 MagicalAttack)
{
	return RollAttackValue(MagicalAttack);
}
