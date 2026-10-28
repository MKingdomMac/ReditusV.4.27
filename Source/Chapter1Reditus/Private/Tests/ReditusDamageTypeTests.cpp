#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "../../ReditusDamageTypeLibrary.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReditusDamageTypeConversionTest,
	"Reditus.Combat.DamageType.Conversion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReditusDamageTypeConversionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const EReditusDamageKind Expected[] = {
		EReditusDamageKind::Physical, EReditusDamageKind::Magical,
		EReditusDamageKind::Pure, EReditusDamageKind::Healing
	};
	for (int32 Value = 0; Value < 256; ++Value)
	{
		EReditusDamageKind Kind = EReditusDamageKind::Healing;
		const bool bValid = UReditusDamageTypeLibrary::TryConvertLegacyDamageType(
			static_cast<uint8>(Value), Kind);
		if (Value < 4)
		{
			TestTrue(TEXT("Known D_DamageType value accepted"), bValid);
			TestTrue(TEXT("Correct named native type"), Kind == Expected[Value]);
		}
		else
		{
			TestFalse(TEXT("Unknown D_DamageType value rejected"), bValid);
			TestTrue(TEXT("Invalid output initialized"), Kind == EReditusDamageKind::Physical);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReditusDamageTypeWrapperTest,
	"Reditus.Combat.DamageType.CalculatorAdapter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReditusDamageTypeWrapperTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FReditusDamageRequest Request;
	Request.DamageKind = EReditusDamageKind::Physical;
	Request.FlatAmount = 100;
	Request.PowerMultiplier = 2.0f;
	Request.bCanCritical = false;
	bool bValid = false;
	FReditusDamageResult Result = UReditusDamageTypeLibrary::CalculateDamageFromLegacyType(
		2, Request, bValid);
	TestTrue(TEXT("Pure accepted"), bValid);
	TestEqual(TEXT("Pure type overrides request type"), Result.FinalDamage, 200);
	TestEqual(TEXT("Pure does not heal"), Result.Healing, 0);
	TestTrue(TEXT("Input request preserved"), Request.DamageKind == EReditusDamageKind::Physical);

	Result = UReditusDamageTypeLibrary::CalculateDamageFromLegacyType(3, Request, bValid);
	TestTrue(TEXT("Healing accepted"), bValid);
	TestEqual(TEXT("Healing is separate from damage"), Result.FinalDamage, 0);
	TestEqual(TEXT("Healing type and power preserved"), Result.Healing, 200);

	Request.bAttackHit = false;
	Result = UReditusDamageTypeLibrary::CalculateDamageFromLegacyType(2, Request, bValid);
	TestTrue(TEXT("Valid type on a miss"), bValid);
	TestFalse(TEXT("Hit flag forwarded"), Result.bHit);
	TestEqual(TEXT("Miss remains zero damage"), Result.FinalDamage, 0);
	Request.bAttackHit = true;

	Request.TargetElementResponses.Add(Request.Element, EReditusElementResponse::Absorb);
	Result = UReditusDamageTypeLibrary::CalculateDamageFromLegacyType(2, Request, bValid);
	TestTrue(TEXT("Absorb forwarded"), Result.bAbsorbed);
	TestEqual(TEXT("Absorb returns no damage"), Result.FinalDamage, 0);
	TestEqual(TEXT("Absorb healing forwarded"), Result.Healing, 200);

	for (int32 Value = 4; Value < 256; ++Value)
	{
		bValid = true;
		Result = UReditusDamageTypeLibrary::CalculateDamageFromLegacyType(
			static_cast<uint8>(Value), Request, bValid);
		TestFalse(TEXT("Invalid type reported"), bValid);
		TestFalse(TEXT("Invalid type cannot hit"), Result.bHit);
		TestEqual(TEXT("Invalid type cannot damage"), Result.FinalDamage, 0);
		TestEqual(TEXT("Invalid type cannot heal"), Result.Healing, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReditusDamageTypeAssetContractTest,
	"Reditus.Combat.DamageType.BlueprintAssetContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReditusDamageTypeAssetContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UEnum* DamageType = LoadObject<UEnum>(nullptr,
		TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Skills/D_DamageType.D_DamageType"));
	if (!TestNotNull(TEXT("Existing D_DamageType asset"), DamageType))
	{
		return false;
	}
	const TCHAR* ExpectedNames[] = {
		TEXT("NewEnumerator0"), TEXT("NewEnumerator1"),
		TEXT("NewEnumerator2"), TEXT("NewEnumerator14")
	};
	for (int64 Value = 0; Value < 4; ++Value)
	{
		TestEqual(TEXT("Blueprint enum matches the adapter's verified value mapping"),
			DamageType->GetNameStringByValue(Value), FString(ExpectedNames[Value]));
	}
	return true;
}

#endif
