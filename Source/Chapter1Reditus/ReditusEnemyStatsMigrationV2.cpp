// ReditusEnemyStatsMigrationV2.cpp
//
// REPLACEMENT for the previous V2 migration file.
// Copy this over your EXISTING ReditusEnemyStatsMigrationV2.cpp.
//
// Changes:
// - Old MaxAttack -> new Attack.
// - Old MaxMagicalAttack -> new MagicalAttack.
// - Old MinAttack and MinMagicalAttack are intentionally discarded.
// - Legacy OnlyNormalDifficulty -> bIgnoreDifficultyScaling.
// - MaxStamina is copied exactly and NEVER difficulty-scaled.
//
// Run in Unreal Output Log:
//     Reditus.ApplyEnemyStatsV2

#include "CoreMinimal.h"

#if WITH_EDITOR

#include "Data/ReditusEnemyDefinition.h"
#include "Engine/DataTable.h"
#include "HAL/IConsoleManager.h"

namespace ReditusEnemyStatsMigrationV2
{
	static FReditusUnitStats MakeStats(
		int32 MaxHP,
		int32 MaxMP,
		int32 Attack,
		int32 MagicalAttack,
		int32 Defense,
		int32 Speed,
		int32 Hit,
		int32 MagicDefense,
		float ActionTime,
		float CriticalHit,
		float MaxOverdrivePoints,
		int32 MaxExhaustionPoints)
	{
		FReditusUnitStats Stats;
		Stats.MaxHP = MaxHP;
		Stats.MaxMP = MaxMP;
		Stats.Attack = Attack;
		Stats.MagicalAttack = MagicalAttack;
		Stats.Defense = Defense;
		Stats.Speed = Speed;
		Stats.Hit = Hit;
		Stats.MagicDefense = MagicDefense;
		Stats.ActionTime = ActionTime;
		Stats.CriticalHit = CriticalHit;
		Stats.MaxOverdrivePoints = MaxOverdrivePoints;
		Stats.MaxExhaustionPoints = MaxExhaustionPoints;
		return Stats;
	}

	struct FMigrationRow
	{
		FName PrimaryRowName;
		FName AlternateRowName;
		int32 MaxLevel;
		int32 MaxStamina;
		float MoveSpeed;
		bool bIgnoreDifficultyScaling;
		FReditusUnitStats InitialStats;
		FReditusUnitStats FirstLevelStats;
		FReditusUnitStats LastLevelStats;

		FMigrationRow(
			const TCHAR* InPrimaryRowName,
			const TCHAR* InAlternateRowName,
			int32 InMaxLevel,
			int32 InMaxStamina,
			float InMoveSpeed,
			bool bInIgnoreDifficultyScaling,
			const FReditusUnitStats& InInitialStats,
			const FReditusUnitStats& InFirstLevelStats,
			const FReditusUnitStats& InLastLevelStats)
			: PrimaryRowName(InPrimaryRowName)
			, AlternateRowName(InAlternateRowName)
			, MaxLevel(InMaxLevel)
			, MaxStamina(InMaxStamina)
			, MoveSpeed(InMoveSpeed)
			, bIgnoreDifficultyScaling(bInIgnoreDifficultyScaling)
			, InitialStats(InInitialStats)
			, FirstLevelStats(InFirstLevelStats)
			, LastLevelStats(InLastLevelStats)
		{
		}
	};

	static UDataTable* LoadEnemyTable()
	{
		UDataTable* Table = LoadObject<UDataTable>(
			nullptr,
			TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_Enemies.DT_Enemies")
		);

		if (!Table)
		{
			Table = LoadObject<UDataTable>(
				nullptr,
				TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_Enemies_New.DT_Enemies_New")
			);
		}

		return Table;
	}

	static FReditusEnemyDefinition* FindMigrationRow(
		UDataTable* Table,
		const FMigrationRow& Migration,
		const FString& Context,
		FName& OutResolvedRowName)
	{
		if (!Table)
		{
			return nullptr;
		}

		if (FReditusEnemyDefinition* Row =
			Table->FindRow<FReditusEnemyDefinition>(
				Migration.PrimaryRowName,
				Context,
				false))
		{
			OutResolvedRowName = Migration.PrimaryRowName;
			return Row;
		}

		if (!Migration.AlternateRowName.IsNone())
		{
			if (FReditusEnemyDefinition* Row =
				Table->FindRow<FReditusEnemyDefinition>(
					Migration.AlternateRowName,
					Context,
					false))
			{
				OutResolvedRowName = Migration.AlternateRowName;
				return Row;
			}
		}

		return nullptr;
	}

	static void ApplyEnemyStatsV2()
	{
		UDataTable* EnemyTable = LoadEnemyTable();

		if (!EnemyTable)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[Reditus Stats V2] Could not find DT_Enemies or DT_Enemies_New."));
			return;
		}

		if (EnemyTable->GetRowStruct() != FReditusEnemyDefinition::StaticStruct())
		{
			UE_LOG(LogTemp, Error,
				TEXT("[Reditus Stats V2] DataTable row struct is not FReditusEnemyDefinition."));
			return;
		}

		const TArray<FMigrationRow> MigrationRows =
		{
		FMigrationRow(
			TEXT("Aranea"),
			TEXT(""),
			99,
			0,
			800.0f,
			false,
			MakeStats(0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0),
			MakeStats(170, 200, 65, 65, 32, 22, 31, 28, 2.2f, 0.0f, 200.0f, 0),
			MakeStats(999, 999, 168, 97, 48, 85, 170, 42, 0.0f, 1.0f, 0.0f, 0)
		),
		FMigrationRow(
			TEXT("BabyAranea"),
			TEXT(""),
			99,
			0,
			800.0f,
			false,
			MakeStats(0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0),
			MakeStats(170, 200, 65, 65, 32, 22, 31, 28, 2.2f, 0.0f, 200.0f, 0),
			MakeStats(999, 999, 168, 97, 48, 85, 170, 42, 0.0f, 1.0f, 0.0f, 0)
		),
		FMigrationRow(
			TEXT("BabySandworm"),
			TEXT(""),
			99,
			0,
			800.0f,
			false,
			MakeStats(0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0),
			MakeStats(170, 200, 65, 65, 32, 22, 31, 28, 2.2f, 0.0f, 200.0f, 0),
			MakeStats(999, 999, 168, 97, 48, 85, 170, 42, 0.0f, 1.0f, 0.0f, 0)
		),
		FMigrationRow(
			TEXT("BattleDummy"),
			TEXT(""),
			99,
			1,
			800.0f,
			true,
			MakeStats(0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0),
			MakeStats(99999, 0, 30, 65, 10, 8, 494, 28, 2.2f, 1.0f, 0.0f, 0),
			MakeStats(99999, 0, 10, 97, 48, 85, 4638, 42, 1.6f, 0.0f, 0.0f, 0)
		),
		FMigrationRow(
			TEXT("BucketOfBolts"),
			TEXT(""),
			99,
			1,
			800.0f,
			true,
			MakeStats(0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0),
			MakeStats(35, 0, 33, 65, 100, 200, 100, 28, 2.2f, 1.0f, 0.0f, 0),
			MakeStats(999, 0, 10, 97, 48, 85, 4638, 42, 1.6f, 0.0f, 0.0f, 0)
		),
		FMigrationRow(
			TEXT("CaveBoar"),
			TEXT(""),
			99,
			3,
			800.0f,
			false,
			MakeStats(0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0),
			MakeStats(210, 50, 70, 65, 30, 20, 35, 28, 0.0f, 1.0f, 0.0f, 0),
			MakeStats(726, 0, 168, 97, 48, 85, 170, 42, 1.6f, 0.0f, 0.0f, 0)
		),
		FMigrationRow(
			TEXT("Cerebus"),
			TEXT(""),
			99,
			5,
			800.0f,
			false,
			MakeStats(0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0),
			MakeStats(1500, 200, 95, 65, 30, 30, 50, 28, 2.2f, 0.0f, 200.0f, 0),
			MakeStats(999, 999, 168, 97, 48, 85, 170, 42, 0.0f, 1.0f, 0.0f, 0)
		),
		FMigrationRow(
			TEXT("DamagedHeremusHarrier"),
			TEXT(""),
			99,
			1,
			800.0f,
			true,
			MakeStats(0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0),
			MakeStats(225, 0, 27, 65, 10, 8, 100, 28, 2.2f, 1.0f, 0.0f, 0),
			MakeStats(726, 0, 168, 97, 48, 85, 170, 42, 1.6f, 0.0f, 0.0f, 0)
		),
		FMigrationRow(
			TEXT("DeathBoar"),
			TEXT(""),
			99,
			3,
			800.0f,
			false,
			MakeStats(0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0),
			MakeStats(170, 50, 50, 65, 22, 14, 35, 28, 0.0f, 1.0f, 0.0f, 0),
			MakeStats(726, 0, 168, 97, 48, 85, 170, 42, 1.6f, 0.0f, 0.0f, 0)
		),
		FMigrationRow(
			TEXT("DeathBoarTutorial"),
			TEXT(""),
			99,
			2,
			800.0f,
			false,
			MakeStats(0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0),
			MakeStats(250, 50, 45, 65, 22, 1, 25, 28, 0.0f, 1.0f, 0.0f, 0),
			MakeStats(726, 0, 168, 97, 48, 85, 170, 42, 1.6f, 0.0f, 0.0f, 0)
		),
		FMigrationRow(
			TEXT("Desmodus"),
			TEXT(""),
			99,
			4,
			800.0f,
			false,
			MakeStats(0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0),
			MakeStats(200, 55, 65, 65, 28, 20, 40, 28, 2.2f, 0.0f, 0.0f, 0),
			MakeStats(999, 999, 168, 97, 48, 85, 170, 42, 0.0f, 1.0f, 0.0f, 0)
		),
		FMigrationRow(
			TEXT("GamaciaKnight"),
			TEXT(""),
			99,
			2,
			800.0f,
			true,
			MakeStats(0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0),
			MakeStats(200, 0, 55, 65, 30, 17, 40, 40, 2.2f, 1.0f, 0.0f, 0),
			MakeStats(0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0)
		),
		FMigrationRow(
			TEXT("GiantSandworm"),
			TEXT(""),
			99,
			0,
			800.0f,
			false,
			MakeStats(0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0),
			MakeStats(170, 200, 65, 65, 32, 22, 31, 28, 2.2f, 0.0f, 200.0f, 0),
			MakeStats(999, 999, 168, 97, 48, 85, 170, 42, 0.0f, 1.0f, 0.0f, 0)
		),
		FMigrationRow(
			TEXT("HackedHarrier"),
			TEXT(""),
			99,
			2,
			800.0f,
			false,
			MakeStats(0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0),
			MakeStats(230, 55, 70, 65, 30, 23, 40, 28, 2.2f, 0.0f, 0.0f, 0),
			MakeStats(999, 999, 168, 97, 48, 85, 170, 42, 0.0f, 1.0f, 0.0f, 0)
		),
		FMigrationRow(
			TEXT("HeremusHarrier"),
			TEXT(""),
			99,
			2,
			800.0f,
			false,
			MakeStats(0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0),
			MakeStats(190, 60, 60, 65, 22, 24, 35, 28, 0.0f, 1.0f, 0.0f, 0),
			MakeStats(999, 0, 168, 97, 48, 85, 170, 42, 1.6f, 0.0f, 0.0f, 0)
		),
		FMigrationRow(
			TEXT("HeremusRover"),
			TEXT(""),
			99,
			2,
			800.0f,
			false,
			MakeStats(0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0),
			MakeStats(170, 50, 60, 65, 23, 16, 35, 28, 0.0f, 1.0f, 0.0f, 0),
			MakeStats(999, 0, 168, 97, 48, 85, 170, 42, 1.6f, 0.0f, 0.0f, 0)
		),
		FMigrationRow(
			TEXT("LizardMan"),
			TEXT(""),
			99,
			2,
			800.0f,
			false,
			MakeStats(0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0),
			MakeStats(140, 50, 50, 65, 22, 14, 40, 28, 0.0f, 1.0f, 0.0f, 0),
			MakeStats(726, 0, 168, 97, 48, 85, 170, 42, 1.6f, 0.0f, 0.0f, 0)
		),
		FMigrationRow(
			TEXT("LizardManGroundDigger"),
			TEXT(""),
			99,
			2,
			800.0f,
			false,
			MakeStats(0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0),
			MakeStats(140, 50, 45, 65, 22, 19, 40, 28, 0.0f, 1.0f, 0.0f, 0),
			MakeStats(726, 0, 168, 97, 48, 85, 170, 42, 1.6f, 0.0f, 0.0f, 0)
		),
		FMigrationRow(
			TEXT("Macy"),
			TEXT("MacyEnemyUnit"),
			99,
			5,
			800.0f,
			false,
			MakeStats(0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0),
			MakeStats(1500, 200, 95, 65, 35, 30, 50, 28, 2.2f, 0.0f, 100.0f, 0),
			MakeStats(2000, 999, 168, 97, 48, 85, 170, 42, 0.0f, 1.0f, 100.0f, 0)
		),
		FMigrationRow(
			TEXT("MetalShark"),
			TEXT(""),
			99,
			0,
			800.0f,
			false,
			MakeStats(0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0),
			MakeStats(170, 200, 65, 65, 32, 22, 31, 28, 2.2f, 0.0f, 200.0f, 0),
			MakeStats(999, 999, 168, 97, 48, 85, 170, 42, 0.0f, 1.0f, 0.0f, 0)
		),
		FMigrationRow(
			TEXT("Perch"),
			TEXT(""),
			99,
			4,
			600.0f,
			false,
			MakeStats(0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0),
			MakeStats(1500, 200, 75, 65, 30, 23, 44, 28, 2.2f, 0.0f, 200.0f, 0),
			MakeStats(999, 999, 168, 97, 48, 85, 170, 42, 0.0f, 1.0f, 0.0f, 0)
		),
		FMigrationRow(
			TEXT("Reaver_2"),
			TEXT("Reaver"),
			99,
			3,
			800.0f,
			false,
			MakeStats(0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0),
			MakeStats(250, 200, 75, 65, 33, 23, 44, 28, 2.2f, 0.0f, 200.0f, 0),
			MakeStats(999, 999, 168, 97, 48, 85, 170, 42, 0.0f, 1.0f, 0.0f, 0)
		),
		FMigrationRow(
			TEXT("ReaverThief"),
			TEXT(""),
			99,
			3,
			800.0f,
			false,
			MakeStats(0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0),
			MakeStats(250, 200, 75, 65, 33, 23, 38, 28, 2.2f, 0.0f, 200.0f, 0),
			MakeStats(999, 999, 168, 97, 48, 85, 170, 42, 0.0f, 1.0f, 0.0f, 0)
		),
		FMigrationRow(
			TEXT("ReditusSoldier"),
			TEXT(""),
			99,
			8,
			800.0f,
			false,
			MakeStats(0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0),
			MakeStats(9999, 999, 60, 65, 32, 30, 40, 28, 2.2f, 0.0f, 0.0f, 0),
			MakeStats(9999, 999, 168, 97, 48, 85, 170, 42, 0.0f, 1.0f, 0.0f, 0)
		),
		FMigrationRow(
			TEXT("Rolly"),
			TEXT(""),
			99,
			3,
			800.0f,
			false,
			MakeStats(0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0),
			MakeStats(160, 50, 50, 65, 20, 12, 35, 28, 0.0f, 0.0f, 0.0f, 0),
			MakeStats(999, 300, 90, 97, 48, 12, 1758, 42, 1.6f, 0.0f, 0.0f, 0)
		),
		FMigrationRow(
			TEXT("Scourge"),
			TEXT(""),
			99,
			99,
			800.0f,
			false,
			MakeStats(0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0),
			MakeStats(150, 50, 65, 65, 999, 16, 193, 28, 0.0f, 1.0f, 0.0f, 0),
			MakeStats(9999, 0, 168, 97, 48, 85, 170, 42, 1.6f, 0.0f, 0.0f, 0)
		),
		FMigrationRow(
			TEXT("SlaveCrownUnit"),
			TEXT(""),
			99,
			0,
			800.0f,
			false,
			MakeStats(0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0),
			MakeStats(170, 200, 65, 65, 32, 22, 31, 28, 2.2f, 0.0f, 200.0f, 0),
			MakeStats(999, 999, 168, 97, 48, 85, 170, 42, 0.0f, 1.0f, 0.0f, 0)
		),
		FMigrationRow(
			TEXT("SlaveCrownUnitBazooka"),
			TEXT(""),
			99,
			0,
			800.0f,
			false,
			MakeStats(0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0),
			MakeStats(170, 200, 65, 65, 32, 22, 31, 28, 2.2f, 0.0f, 200.0f, 0),
			MakeStats(999, 999, 168, 97, 48, 85, 170, 42, 0.0f, 1.0f, 0.0f, 0)
		)
		};

		EnemyTable->Modify();

		int32 UpdatedCount = 0;
		int32 MissingCount = 0;
		const FString Context(TEXT("Reditus.ApplyEnemyStatsV2"));

		for (const FMigrationRow& Migration : MigrationRows)
		{
			FName ResolvedRowName = NAME_None;

			FReditusEnemyDefinition* Row =
				FindMigrationRow(
					EnemyTable,
					Migration,
					Context,
					ResolvedRowName
				);

			if (!Row)
			{
				++MissingCount;

				UE_LOG(
					LogTemp,
					Warning,
					TEXT("[Reditus Stats V2] Missing row. Tried '%s' and '%s'."),
					*Migration.PrimaryRowName.ToString(),
					*Migration.AlternateRowName.ToString()
				);

				continue;
			}

			Row->MaxLevel = Migration.MaxLevel;
			Row->InitialStats = Migration.InitialStats;
			Row->FirstLevelStats = Migration.FirstLevelStats;
			Row->LastLevelStats = Migration.LastLevelStats;

			// Exact static values. Difficulty does NOT touch MaxStamina.
			Row->MaxStamina = Migration.MaxStamina;
			Row->MoveSpeed = Migration.MoveSpeed;

			// Tutorial/special enemies marked by the old flag keep base stats on Easy.
			Row->bIgnoreDifficultyScaling =
				Migration.bIgnoreDifficultyScaling;

			++UpdatedCount;
		}

		EnemyTable->MarkPackageDirty();
		EnemyTable->PostEditChange();

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[Reditus Stats V2] Finished. Updated: %d | Missing: %d"),
			UpdatedCount,
			MissingCount
		);

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[Reditus Stats V2] Open DT_Enemies, verify it, then SAVE it.")
		);
	}

	static FAutoConsoleCommand ApplyEnemyStatsV2Command(
		TEXT("Reditus.ApplyEnemyStatsV2"),
		TEXT("Migrate exact enemy stats into the single Attack + single MagicalAttack model."),
		FConsoleCommandDelegate::CreateStatic(&ApplyEnemyStatsV2)
	);
}

#endif // WITH_EDITOR
