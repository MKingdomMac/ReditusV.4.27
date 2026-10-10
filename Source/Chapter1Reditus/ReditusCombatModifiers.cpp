#include "ReditusCombatModifiers.h"

FReditusCombatModifierTotals
UReditusCombatModifierLibrary::AccumulateCombatModifiers(
	const TArray<FReditusCombatModifier>& Modifiers)
{
	FReditusCombatModifierTotals Totals;

	for (const FReditusCombatModifier& Modifier : Modifiers)
	{
		switch (Modifier.Type)
		{
		case EReditusCombatModifierType::OutgoingDamageBonus:
			Totals.OutgoingDamageBonus += Modifier.Value;
			break;

		case EReditusCombatModifierType::PhysicalDamageBonus:
			Totals.PhysicalDamageBonus += Modifier.Value;
			break;

		case EReditusCombatModifierType::MagicalDamageBonus:
			Totals.MagicalDamageBonus += Modifier.Value;
			break;

		case EReditusCombatModifierType::PureDamageBonus:
			Totals.PureDamageBonus += Modifier.Value;
			break;

		case EReditusCombatModifierType::HealingBonus:
			Totals.HealingBonus += Modifier.Value;
			break;

		case EReditusCombatModifierType::CriticalChanceBonus:
			Totals.CriticalChanceBonus += Modifier.Value;
			break;

		case EReditusCombatModifierType::CriticalDamageBonus:
			Totals.CriticalDamageBonus += Modifier.Value;
			break;

		case EReditusCombatModifierType::WeaknessDamageBonus:
			Totals.WeaknessDamageBonus += Modifier.Value;
			break;


		case EReditusCombatModifierType::IncomingDamageBonus:
			Totals.IncomingDamageBonus += Modifier.Value;
			break;

		case EReditusCombatModifierType::IncomingDamageReduction:
			Totals.IncomingDamageReduction += Modifier.Value;
			break;

		case EReditusCombatModifierType::IncomingCriticalChanceBonus:
			Totals.IncomingCriticalChanceBonus += Modifier.Value;
			break;

		default:
			break;
		}
	}

	return Totals;
}

TArray<FReditusCombatModifier>
UReditusCombatModifierLibrary::CombineCombatModifierArrays(
	const TArray<FReditusCombatModifier>& A,
	const TArray<FReditusCombatModifier>& B)
{
	TArray<FReditusCombatModifier> Result;
	Result.Reserve(A.Num() + B.Num());
	Result.Append(A);
	Result.Append(B);
	return Result;
}
