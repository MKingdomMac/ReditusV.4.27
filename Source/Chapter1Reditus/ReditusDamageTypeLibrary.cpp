#include "ReditusDamageTypeLibrary.h"

bool UReditusDamageTypeLibrary::TryConvertLegacyDamageType(
	uint8 LegacyDamageTypeValue,
	EReditusDamageKind& DamageKind)
{
	// Deliberate cases: do not assume the native enum shares ordinal values.
	switch (LegacyDamageTypeValue)
	{
	case 0: // D_DamageType::NewEnumerator0 (Physical)
		DamageKind = EReditusDamageKind::Physical;
		return true;
	case 1: // D_DamageType::NewEnumerator1 (Magical)
		DamageKind = EReditusDamageKind::Magical;
		return true;
	case 2: // D_DamageType::NewEnumerator2 (Pure)
		DamageKind = EReditusDamageKind::Pure;
		return true;
	case 3: // D_DamageType::NewEnumerator14 (Healing)
		DamageKind = EReditusDamageKind::Healing;
		return true;
	default:
		DamageKind = EReditusDamageKind::Physical;
		return false;
	}
}

FReditusDamageResult UReditusDamageTypeLibrary::CalculateDamageFromLegacyType(
	uint8 LegacyDamageTypeValue,
	const FReditusDamageRequest& Request,
	bool& bValidDamageType)
{
	EReditusDamageKind DamageKind = EReditusDamageKind::Physical;
	bValidDamageType = TryConvertLegacyDamageType(
		LegacyDamageTypeValue, DamageKind);

	if (!bValidDamageType)
	{
		FReditusDamageResult InvalidResult;
		InvalidResult.bHit = false;
		return InvalidResult;
	}

	FReditusDamageRequest ConvertedRequest = Request;
	ConvertedRequest.DamageKind = DamageKind;
	return UReditusDamageCalculator::CalculateDamage(ConvertedRequest);
}
