#include "ReditusElementDamageRules.h"
#include "ReditusCombatTuning.h"

EReditusElementResponse UReditusElementDamageRules::GetElementResponse(
	const TMap<EReditusElementAffinity, EReditusElementResponse>& ElementResponses,
	EReditusElementAffinity Element)
{
	if (const EReditusElementResponse* Found = ElementResponses.Find(Element))
	{
		return *Found;
	}

	// Authoring convenience: omitted element = Normal.
	return EReditusElementResponse::Normal;
}

float UReditusElementDamageRules::GetElementDamageMultiplier(
	EReditusElementResponse Response)
{
	switch (Response)
	{
	case EReditusElementResponse::Weak:
		return ReditusCombatTuning::ElementNormalDamageMultiplier
			+ ReditusCombatTuning::ElementWeakDamageBonus;

	case EReditusElementResponse::Resist:
		return ReditusCombatTuning::ElementResistDamageMultiplier;

	case EReditusElementResponse::Normal:
		return ReditusCombatTuning::ElementNormalDamageMultiplier;

	case EReditusElementResponse::Null:
	case EReditusElementResponse::Absorb:
	default:
		return 0.0f;
	}
}

FReditusElementDamageResult UReditusElementDamageRules::ResolveElementalDamage(
	int32 IncomingDamage,
	EReditusElementResponse Response)
{
	FReditusElementDamageResult Result;
	Result.Response = Response;

	const int32 SafeIncomingDamage = FMath::Max(0, IncomingDamage);

	switch (Response)
	{
	case EReditusElementResponse::Weak:
	case EReditusElementResponse::Normal:
	case EReditusElementResponse::Resist:
		Result.Damage = FMath::Max(
			0,
			FMath::RoundToInt(
				static_cast<float>(SafeIncomingDamage) *
				GetElementDamageMultiplier(Response)
			)
		);
		break;

	case EReditusElementResponse::Null:
		Result.Damage = 0;
		Result.Healing = 0;
		break;

	case EReditusElementResponse::Absorb:
		Result.Damage = 0;
		Result.Healing = FMath::Max(
			0,
			FMath::RoundToInt(
				static_cast<float>(SafeIncomingDamage) *
				ReditusCombatTuning::ElementAbsorbHealingMultiplier
			)
		);
		break;

	default:
		Result.Damage = SafeIncomingDamage;
		break;
	}

	return Result;
}

FReditusElementDamageResult UReditusElementDamageRules::ResolveElementalDamageFromMap(
	int32 IncomingDamage,
	const TMap<EReditusElementAffinity, EReditusElementResponse>& ElementResponses,
	EReditusElementAffinity Element)
{
	return ResolveElementalDamage(
		IncomingDamage,
		GetElementResponse(ElementResponses, Element)
	);
}
