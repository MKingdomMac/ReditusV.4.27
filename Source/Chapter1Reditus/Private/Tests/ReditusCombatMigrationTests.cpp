#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "../../ReditusDamageCalculator.h"
#include "../../ReditusBattleBridgeComponent.h"
#include "../../ReditusDifficultyRules.h"
#include "UObject/UObjectGlobals.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReditusCombatMigrationCalculatorTest,
	"Reditus.Combat.Migration.Calculator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReditusCombatMigrationCalculatorTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	auto MakeRequest = []()
	{
		FReditusDamageRequest Request;
		Request.DamageKind = EReditusDamageKind::Pure;
		Request.FlatAmount = 100;
		Request.CriticalChancePercent = 0.0f;
		return Request;
	};

	FReditusDamageRequest Request = MakeRequest();
	TestEqual(TEXT("Normal damage"), UReditusDamageCalculator::CalculateDamage(Request).FinalDamage, 100);

	Request.TargetElementResponses.Add(Request.Element, EReditusElementResponse::Weak);
	TestEqual(TEXT("Weakness gives 1.5x"), UReditusDamageCalculator::CalculateDamage(Request).FinalDamage, 150);

	Request = MakeRequest();
	Request.bForceCritical = true;
	TestEqual(TEXT("Critical gives 1.5x"), UReditusDamageCalculator::CalculateDamage(Request).FinalDamage, 150);
	Request.TargetElementResponses.Add(Request.Element, EReditusElementResponse::Weak);
	TestEqual(TEXT("Weakness plus critical gives 2.0x"), UReditusDamageCalculator::CalculateDamage(Request).FinalDamage, 200);

	Request = MakeRequest();
	Request.bTargetStaggered = true;
	FReditusDamageResult Result = UReditusDamageCalculator::CalculateDamage(Request);
	TestTrue(TEXT("Stagger guarantees critical"), Result.bCritical);
	TestEqual(TEXT("Stagger does not add another multiplier"), Result.FinalDamage, 150);
	Request.TargetElementResponses.Add(Request.Element, EReditusElementResponse::Weak);
	TestEqual(TEXT("Weakness plus stagger gives 2.0x"), UReditusDamageCalculator::CalculateDamage(Request).FinalDamage, 200);

	Request = MakeRequest();
	Request.TargetElementResponses.Add(Request.Element, EReditusElementResponse::Resist);
	TestEqual(TEXT("Resistance gives 0.5x"), UReditusDamageCalculator::CalculateDamage(Request).FinalDamage, 50);
	Request.TargetElementResponses.Add(Request.Element, EReditusElementResponse::Null);
	Result = UReditusDamageCalculator::CalculateDamage(Request);
	TestTrue(TEXT("Null response flag"), Result.bNullified);
	TestEqual(TEXT("Null gives zero damage"), Result.FinalDamage, 0);
	TestEqual(TEXT("Null gives zero healing"), Result.Healing, 0);
	Request.TargetElementResponses.Add(Request.Element, EReditusElementResponse::Absorb);
	Result = UReditusDamageCalculator::CalculateDamage(Request);
	TestTrue(TEXT("Absorb response flag"), Result.bAbsorbed);
	TestEqual(TEXT("Absorb gives zero damage"), Result.FinalDamage, 0);
	TestEqual(TEXT("Absorb restores HP"), Result.Healing, 100);

	Request = MakeRequest();
	Request.bAttackHit = false;
	Result = UReditusDamageCalculator::CalculateDamage(Request);
	TestFalse(TEXT("Miss flag"), Result.bHit);
	TestEqual(TEXT("Miss gives zero damage"), Result.FinalDamage, 0);
	Request = MakeRequest();
	Request.bTargetDefending = true;
	TestEqual(TEXT("Defending gives 0.5x"), UReditusDamageCalculator::CalculateDamage(Request).FinalDamage, 50);

	Request = MakeRequest();
	Request.DamageKind = EReditusDamageKind::Physical;
	Request.Attack = 100;
	Request.TargetDefense = 20;
	Result = UReditusDamageCalculator::CalculateDamage(Request);
	TestTrue(TEXT("Physical offense respects the configured 90-100% range"),
		Result.RolledOffense >= 90 && Result.RolledOffense <= 100);
	TestEqual(TEXT("Physical defense is subtracted once"), Result.FinalDamage, Result.RolledOffense - 20);

	Request = MakeRequest();
	Request.DamageKind = EReditusDamageKind::Healing;
	Result = UReditusDamageCalculator::CalculateDamage(Request);
	TestEqual(TEXT("Healing produces no damage"), Result.FinalDamage, 0);
	TestEqual(TEXT("Healing amount"), Result.Healing, 100);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReditusCombatMigrationBridgeTest,
	"Reditus.Combat.Migration.BridgeFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReditusCombatMigrationBridgeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UReditusBattleBridgeComponent* Bridge = NewObject<UReditusBattleBridgeComponent>();
	if (!TestNotNull(TEXT("Transient damage bridge"), Bridge))
	{
		return false;
	}
	TestTrue(TEXT("Test bridge has no battle subsystem"), Bridge->GetBattleSubsystem() == nullptr);
	FReditusDamageRequest Request;
	Request.DamageKind = EReditusDamageKind::Pure;
	Request.FlatAmount = 100;
	TestEqual(TEXT("Stateless calculator still works without a battle subsystem"),
		Bridge->CalculateDamage(Request).FinalDamage, 100);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReditusCombatMigrationDifficultyTest,
	"Reditus.Combat.Migration.Difficulty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReditusCombatMigrationDifficultyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FReditusUnitStats Stats;
	Stats.MaxHP = 101;
	Stats.Attack = 101;
	Stats.MaxMP = 25;
	Stats.Defense = 30;
	FReditusUnitStats Result = UReditusDifficultyRules::ApplyEnemyDifficultyToStats(Stats, true, false);
	TestEqual(TEXT("Easy HP scales to 80%, rounded up"), Result.MaxHP, 81);
	TestEqual(TEXT("Easy attack scales to 80%, rounded up"), Result.Attack, 81);
	TestEqual(TEXT("Easy MP stays unchanged"), Result.MaxMP, 25);
	TestEqual(TEXT("Easy defense stays unchanged"), Result.Defense, 30);
	Result = UReditusDifficultyRules::ApplyEnemyDifficultyToStats(Stats, true, true);
	TestEqual(TEXT("Opt-out enemy preserves HP"), Result.MaxHP, 101);
	TestEqual(TEXT("Opt-out enemy preserves attack"), Result.Attack, 101);
	TestFalse(TEXT("Enemy AMP disabled on easy"), UReditusDifficultyRules::CanEnemyUseAMP(true, true));
	TestTrue(TEXT("Enemy AMP enabled on hard when allowed"), UReditusDifficultyRules::CanEnemyUseAMP(true, false));
	return true;
}

#endif
