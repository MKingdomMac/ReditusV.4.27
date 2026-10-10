// ReditusEnemyDefaultsExporter.cpp
// UE4.27 editor-only migration helper.
// Drop this file into Source/Chapter1Reditus, compile, then run:
//     Reditus.ExportEnemyDefaults
// from Unreal Editor's Output Log console.
//
// Output:
//   <Project>/Saved/ReditusMigration/EnemyDefaults_Exact.csv
//   <Project>/Saved/ReditusMigration/EnemyDefaults_AllEditable.csv
//   <Project>/Saved/ReditusMigration/EnemyDefaults_ExporterLog.txt
//
// This reads the RESOLVED Class Default Object (CDO) for every concrete
// BP_EnemyUnitBase child, so inherited defaults + per-child overrides are
// exported exactly as Unreal currently sees them.

#include "CoreMinimal.h"

#if WITH_EDITOR

#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"

namespace ReditusEnemyDefaultsExporter
{
	static const TCHAR* EnemyBaseBlueprintObjectPath =
		TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Units/EnemyUnits/BP_EnemyUnitBase.BP_EnemyUnitBase");

	static const TCHAR* EnemyContentRelativePath =
		TEXT("TurnBasedJRPGTemplate/Blueprints/Units/EnemyUnits");

	struct FColumn
	{
		FString Header;
		FString PropertyName;

		FColumn(const TCHAR* InHeader, const TCHAR* InPropertyName)
			: Header(InHeader)
			, PropertyName(InPropertyName)
		{
		}
	};

	struct FStatMember
	{
		FString Header;
		FString InternalPrefix;

		FStatMember(const TCHAR* InHeader, const TCHAR* InInternalPrefix)
			: Header(InHeader)
			, InternalPrefix(InInternalPrefix)
		{
		}
	};

	static FString CsvEscape(const FString& InValue)
	{
		FString Escaped = InValue;
		Escaped.ReplaceInline(TEXT("\""), TEXT("\"\""));
		return FString::Printf(TEXT("\"%s\""), *Escaped);
	}

	static FString ExportPropertyValue(UObject* Object, const FString& PropertyName)
	{
		if (!Object)
		{
			return TEXT("<NULL_OBJECT>");
		}

		FProperty* Property = Object->GetClass()->FindPropertyByName(FName(*PropertyName));
		if (!Property)
		{
			return TEXT("<PROPERTY_NOT_FOUND>");
		}

		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);

		// Make FText fields human-readable in the migration CSV.
		if (const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
		{
			return TextProperty->GetPropertyValue(ValuePtr).ToString();
		}

		FString OutValue;
		Property->ExportTextItem(
			OutValue,
			ValuePtr,
			nullptr,
			Object,
			PPF_None
		);

		return OutValue;
	}

	static FStructProperty* FindStructProperty(UObject* Object, const FString& PropertyName)
	{
		if (!Object)
		{
			return nullptr;
		}

		FProperty* Property = Object->GetClass()->FindPropertyByName(FName(*PropertyName));
		return CastField<FStructProperty>(Property);
	}

	static FProperty* FindStructMemberByPrefix(UScriptStruct* Struct, const FString& InternalPrefix)
	{
		if (!Struct)
		{
			return nullptr;
		}

		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			FProperty* Member = *It;
			const FString MemberName = Member->GetName();

			// UE user-defined struct fields commonly become:
			// maxHP_3_<GUID>, criticalhit_34_<GUID>, etc.
			if (MemberName.Equals(InternalPrefix, ESearchCase::IgnoreCase) ||
				MemberName.StartsWith(InternalPrefix + TEXT("_"), ESearchCase::IgnoreCase))
			{
				return Member;
			}
		}

		return nullptr;
	}

	static FString ExportStructMemberValue(
		UObject* Object,
		const FString& StructPropertyName,
		const FString& MemberPrefix)
	{
		FStructProperty* StructProperty = FindStructProperty(Object, StructPropertyName);
		if (!StructProperty || !StructProperty->Struct)
		{
			return TEXT("<STRUCT_NOT_FOUND>");
		}

		void* StructValuePtr = StructProperty->ContainerPtrToValuePtr<void>(Object);
		FProperty* MemberProperty = FindStructMemberByPrefix(StructProperty->Struct, MemberPrefix);

		if (!MemberProperty)
		{
			return TEXT("<MEMBER_NOT_FOUND>");
		}

		const void* MemberValuePtr = MemberProperty->ContainerPtrToValuePtr<void>(StructValuePtr);

		FString OutValue;
		MemberProperty->ExportTextItem(
			OutValue,
			MemberValuePtr,
			nullptr,
			Object,
			PPF_None
		);

		return OutValue;
	}

	static bool ShouldSkipEnemyBlueprint(const FString& AssetName)
	{
		// Framework/template classes, not actual enemy content rows.
		static const TSet<FString> SkipNames =
		{
			TEXT("BP_EnemyUnitBase"),
			TEXT("BP_AverageEnemy"),
			TEXT("BP_WeakEnemy"),
			TEXT("BP_BossEnemy"),
			TEXT("BP_EnemyBossBase")
		};

		return SkipNames.Contains(AssetName);
	}

	static FString MakeSuggestedRowName(const FString& AssetName)
	{
		FString RowName = AssetName;

		if (RowName.StartsWith(TEXT("BP_")))
		{
			RowName.RightChopInline(3, false);
		}

		// Keep names stable/readable while avoiding aggressive guessing.
		if (RowName.EndsWith(TEXT("EnemyUnit")))
		{
			RowName.LeftChopInline(9, false);
		}

		return RowName;
	}

	static bool IsReditusBlueprintProperty(const FProperty* Property)
	{
		if (!Property)
		{
			return false;
		}

		const UStruct* OwnerStruct = Property->GetOwnerStruct();
		if (!OwnerStruct)
		{
			return false;
		}

		const FString OwnerPath = OwnerStruct->GetPathName();

		// Restrict the audit CSV to Reditus unit Blueprint properties rather than
		// dumping native AActor/UObject fields.
		return OwnerPath.Contains(TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Units/"));
	}

	static FString FriendlyPropertyName(const FProperty* Property)
	{
		if (!Property)
		{
			return FString();
		}

		const FString DisplayNameMeta = Property->GetMetaData(TEXT("DisplayName"));
		if (!DisplayNameMeta.IsEmpty())
		{
			return DisplayNameMeta;
		}

		return Property->GetName();
	}

	static FString ExportDirectProperty(const FProperty* Property, UObject* Object)
	{
		if (!Property || !Object)
		{
			return FString();
		}

		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);

		if (const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
		{
			return TextProperty->GetPropertyValue(ValuePtr).ToString();
		}

		FString OutValue;
		Property->ExportTextItem(
			OutValue,
			ValuePtr,
			nullptr,
			Object,
			PPF_None
		);

		return OutValue;
	}

	static void ExportEnemyDefaults()
	{
		UE_LOG(LogTemp, Display, TEXT("[Reditus Export] Starting exact enemy CDO export..."));

		UBlueprint* EnemyBaseBlueprint =
			LoadObject<UBlueprint>(nullptr, EnemyBaseBlueprintObjectPath);

		if (!EnemyBaseBlueprint || !EnemyBaseBlueprint->GeneratedClass)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[Reditus Export] Could not load BP_EnemyUnitBase at: %s"),
				EnemyBaseBlueprintObjectPath
			);
			return;
		}

		UClass* EnemyBaseClass = EnemyBaseBlueprint->GeneratedClass;

		const FString EnemyDiskDirectory =
			FPaths::Combine(FPaths::ProjectContentDir(), EnemyContentRelativePath);

		TArray<FString> EnemyAssetFiles;
		IFileManager::Get().FindFilesRecursive(
			EnemyAssetFiles,
			*EnemyDiskDirectory,
			TEXT("*.uasset"),
			true,
			false,
			false
		);

		if (EnemyAssetFiles.Num() == 0)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[Reditus Export] No .uasset files found under: %s"),
				*EnemyDiskDirectory
			);
			return;
		}

		// These are the legacy/static fields we know matter to the enemy migration.
		// Some legacy fields that are being removed from the new schema are still
		// exported at the end for auditing, so we don't lose information.
		const TArray<FColumn> SelectedColumns =
		{
			FColumn(TEXT("Title"),                         TEXT("title")),
			FColumn(TEXT("MaxLevel"),                      TEXT("maxLevel")),
			FColumn(TEXT("MaxStamina"),                    TEXT("MaxStamina")),
			FColumn(TEXT("MoveSpeed"),                     TEXT("moveSpeed")),
			FColumn(TEXT("IsRangedUnit"),                  TEXT("IsRangedUnit")),
			FColumn(TEXT("CanEnterOverdrive"),             TEXT("CanEnterOverdrive")),
			FColumn(TEXT("EnemyClass"),                    TEXT("E_EnemyClass")),
			FColumn(TEXT("ElementAffinity"),               TEXT("Element Affinity")),
			FColumn(TEXT("DefaultElementAffinity"),        TEXT("Default Element Affinity")),
			FColumn(TEXT("SecondaryElementAffinity"),      TEXT("Secondary Element Affinity")),
			FColumn(TEXT("AttackElement"),                 TEXT("AttackElement")),
			FColumn(TEXT("UnitChangesWeakness"),           TEXT("UnitChangesWeakness")),
			FColumn(TEXT("Immunities"),                    TEXT("Immunities")),
			FColumn(TEXT("Projectile"),                    TEXT("projectile")),
			FColumn(TEXT("ExpReward"),                     TEXT("expReward")),
			FColumn(TEXT("GoldReward"),                    TEXT("goldReward")),
			FColumn(TEXT("Skills"),                        TEXT("skills")),
			FColumn(TEXT("UsableItemDrops"),               TEXT("usableItemDrops")),
			FColumn(TEXT("MiscItemDrops"),                 TEXT("miscItemDrops")),
			FColumn(TEXT("EquipmentDrops"),                TEXT("equipmentDrops")),
			FColumn(TEXT("KeyItemDrops"),                  TEXT("keyitemdrops")),
			FColumn(TEXT("OnlyNormalDifficulty"),          TEXT("OnlyNormalDifficulty")),
			FColumn(TEXT("HardDifficulty"),                TEXT("Hard Difficulty")),
			FColumn(TEXT("RecruitedUnit"),                 TEXT("RecruitedUnit")),
			FColumn(TEXT("RobotTemperment"),               TEXT("RobotTemperment")),
			FColumn(TEXT("UsingPaperZD"),                  TEXT("using PaperZD")),

			// Legacy audit columns. These can be removed from the new DataTable
			// after we verify nothing important is lost.
			FColumn(TEXT("Legacy_CanDissemble"),           TEXT("CanDissemble")),
			FColumn(TEXT("Legacy_SkillToDisassemble"),     TEXT("Skill to Disassemble")),
			FColumn(TEXT("Legacy_CannotSteal"),            TEXT("Can't Steal?")),
			FColumn(TEXT("Legacy_HeadShotPossible"),       TEXT("HeadShotPossible"))
		};

		const TArray<FStatMember> StatMembers =
		{
			FStatMember(TEXT("MaxHP"),                 TEXT("maxHP")),
			FStatMember(TEXT("MaxMP"),                 TEXT("maxMP")),
			FStatMember(TEXT("MinAttack"),             TEXT("minAttack")),
			FStatMember(TEXT("MaxAttack"),             TEXT("maxAttack")),
			FStatMember(TEXT("Defense"),               TEXT("defense")),
			FStatMember(TEXT("Speed"),                 TEXT("speed")),
			FStatMember(TEXT("Hit"),                   TEXT("hit")),
			FStatMember(TEXT("MinMagicalAttack"),      TEXT("minMagicalAttack")),
			FStatMember(TEXT("MaxMagicalAttack"),      TEXT("maxMagicalAttack")),
			FStatMember(TEXT("MagicDefense"),          TEXT("magicDefense")),
			FStatMember(TEXT("ActionTime"),            TEXT("actionTime")),
			FStatMember(TEXT("CriticalHit"),           TEXT("criticalhit")),
			FStatMember(TEXT("MaxOverdrivePoints"),    TEXT("maxOverdrivepoints")),
			FStatMember(TEXT("MaxExhaustionPoints"),   TEXT("MaxExhaustionPoints"))
		};

		const TArray<FString> StatStructProperties =
		{
			TEXT("initialStats"),
			TEXT("firstLevelStats"),
			TEXT("lastLevelStats")
		};

		const TArray<FString> StatStructPrefixes =
		{
			TEXT("Initial"),
			TEXT("FirstLevel"),
			TEXT("LastLevel")
		};

		FString ExactCsv;
		ExactCsv +=
			TEXT("SuggestedRowName,BlueprintAsset,BlueprintObjectPath,GeneratedClassPath");

		for (const FColumn& Column : SelectedColumns)
		{
			ExactCsv += TEXT(",");
			ExactCsv += CsvEscape(Column.Header);
		}

		for (int32 StructIndex = 0; StructIndex < StatStructProperties.Num(); ++StructIndex)
		{
			for (const FStatMember& Stat : StatMembers)
			{
				ExactCsv += TEXT(",");
				ExactCsv += CsvEscape(
					StatStructPrefixes[StructIndex] + TEXT("_") + Stat.Header
				);
			}
		}

		ExactCsv += LINE_TERMINATOR;

		FString AuditCsv;
		AuditCsv +=
			TEXT("SuggestedRowName,BlueprintAsset,BlueprintObjectPath,GeneratedClassPath,")
			TEXT("PropertyOwner,PropertyName,FriendlyName,Category,Value");
		AuditCsv += LINE_TERMINATOR;

		TArray<FString> ExportLogLines;
		int32 ExportedEnemyCount = 0;

		for (const FString& AssetFilename : EnemyAssetFiles)
		{
			FString LongPackageName;
			if (!FPackageName::TryConvertFilenameToLongPackageName(
					AssetFilename,
					LongPackageName))
			{
				continue;
			}

			const FString AssetName = FPaths::GetBaseFilename(AssetFilename);
			const FString BlueprintObjectPath =
				LongPackageName + TEXT(".") + AssetName;

			UBlueprint* Blueprint =
				LoadObject<UBlueprint>(nullptr, *BlueprintObjectPath);

			if (!Blueprint || !Blueprint->GeneratedClass)
			{
				continue;
			}

			UClass* EnemyClass = Blueprint->GeneratedClass;

			if (EnemyClass == EnemyBaseClass ||
				!EnemyClass->IsChildOf(EnemyBaseClass) ||
				ShouldSkipEnemyBlueprint(AssetName))
			{
				continue;
			}

			UObject* CDO = EnemyClass->GetDefaultObject();
			if (!CDO)
			{
				continue;
			}

			const FString SuggestedRowName = MakeSuggestedRowName(AssetName);
			const FString GeneratedClassPath = EnemyClass->GetPathName();

			TArray<FString> ExactFields;
			ExactFields.Add(CsvEscape(SuggestedRowName));
			ExactFields.Add(CsvEscape(AssetName));
			ExactFields.Add(CsvEscape(BlueprintObjectPath));
			ExactFields.Add(CsvEscape(GeneratedClassPath));

			for (const FColumn& Column : SelectedColumns)
			{
				ExactFields.Add(
					CsvEscape(ExportPropertyValue(CDO, Column.PropertyName))
				);
			}

			for (int32 StructIndex = 0; StructIndex < StatStructProperties.Num(); ++StructIndex)
			{
				for (const FStatMember& Stat : StatMembers)
				{
					ExactFields.Add(
						CsvEscape(
							ExportStructMemberValue(
								CDO,
								StatStructProperties[StructIndex],
								Stat.InternalPrefix
							)
						)
					);
				}
			}

			ExactCsv += FString::Join(ExactFields, TEXT(","));
			ExactCsv += LINE_TERMINATOR;

			// Second file: a long-form audit of every editable property owned by
			// the Reditus unit Blueprint inheritance chain. This is intentionally
			// broader than the final DataTable so we can prove we didn't overlook
			// a meaningful old default.
			for (TFieldIterator<FProperty> It(EnemyClass, EFieldIteratorFlags::IncludeSuper);
				 It;
				 ++It)
			{
				FProperty* Property = *It;

				if (!Property->HasAnyPropertyFlags(CPF_Edit) ||
					!IsReditusBlueprintProperty(Property))
				{
					continue;
				}

				const UStruct* OwnerStruct = Property->GetOwnerStruct();
				const FString OwnerPath = OwnerStruct
					? OwnerStruct->GetPathName()
					: FString();

				const FString Category =
					Property->GetMetaData(TEXT("Category"));

				const FString Value =
					ExportDirectProperty(Property, CDO);

				TArray<FString> AuditFields;
				AuditFields.Add(CsvEscape(SuggestedRowName));
				AuditFields.Add(CsvEscape(AssetName));
				AuditFields.Add(CsvEscape(BlueprintObjectPath));
				AuditFields.Add(CsvEscape(GeneratedClassPath));
				AuditFields.Add(CsvEscape(OwnerPath));
				AuditFields.Add(CsvEscape(Property->GetName()));
				AuditFields.Add(CsvEscape(FriendlyPropertyName(Property)));
				AuditFields.Add(CsvEscape(Category));
				AuditFields.Add(CsvEscape(Value));

				AuditCsv += FString::Join(AuditFields, TEXT(","));
				AuditCsv += LINE_TERMINATOR;
			}

			ExportLogLines.Add(
				FString::Printf(
					TEXT("%s -> %s"),
					*AssetName,
					*GeneratedClassPath
				)
			);

			++ExportedEnemyCount;
		}

		const FString OutputDirectory =
			FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ReditusMigration"));

		IFileManager::Get().MakeDirectory(*OutputDirectory, true);

		const FString ExactCsvPath =
			FPaths::Combine(OutputDirectory, TEXT("EnemyDefaults_Exact.csv"));

		const FString AuditCsvPath =
			FPaths::Combine(OutputDirectory, TEXT("EnemyDefaults_AllEditable.csv"));

		const FString ExportLogPath =
			FPaths::Combine(OutputDirectory, TEXT("EnemyDefaults_ExporterLog.txt"));

		FString ExportLog;
		ExportLog += FString::Printf(
			TEXT("Exported enemy count: %d%s"),
			ExportedEnemyCount,
			LINE_TERMINATOR
		);
		ExportLog += FString::Printf(
			TEXT("Base class: %s%s"),
			*EnemyBaseClass->GetPathName(),
			LINE_TERMINATOR
		);
		ExportLog += FString::Printf(
			TEXT("Source folder: %s%s%s"),
			*EnemyDiskDirectory,
			LINE_TERMINATOR,
			LINE_TERMINATOR
		);
		ExportLog += FString::Join(ExportLogLines, LINE_TERMINATOR);

		const bool bSavedExact =
			FFileHelper::SaveStringToFile(
				ExactCsv,
				*ExactCsvPath,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM
			);

		const bool bSavedAudit =
			FFileHelper::SaveStringToFile(
				AuditCsv,
				*AuditCsvPath,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM
			);

		const bool bSavedLog =
			FFileHelper::SaveStringToFile(
				ExportLog,
				*ExportLogPath,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM
			);

		if (bSavedExact && bSavedAudit && bSavedLog)
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[Reditus Export] SUCCESS. Exported %d enemies."),
				ExportedEnemyCount
			);

			UE_LOG(
				LogTemp,
				Display,
				TEXT("[Reditus Export] Exact CSV: %s"),
				*ExactCsvPath
			);

			UE_LOG(
				LogTemp,
				Display,
				TEXT("[Reditus Export] Audit CSV: %s"),
				*AuditCsvPath
			);

			UE_LOG(
				LogTemp,
				Display,
				TEXT("[Reditus Export] Log: %s"),
				*ExportLogPath
			);
		}
		else
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[Reditus Export] One or more output files could not be saved to: %s"),
				*OutputDirectory
			);
		}
	}

	static FAutoConsoleCommand ExportEnemyDefaultsCommand(
		TEXT("Reditus.ExportEnemyDefaults"),
		TEXT("Export exact resolved Blueprint CDO defaults for all concrete Reditus enemies."),
		FConsoleCommandDelegate::CreateStatic(&ExportEnemyDefaults)
	);
}

#endif // WITH_EDITOR
