// ReditusPlayerDefaultsExporter.cpp
// UE4.27 editor-only migration helper.
//
// Copy this file into Source/Chapter1Reditus, close Unreal Editor, compile the
// Development Editor target, reopen the project, and run this in Output Log:
//
//     Reditus.ExportPlayerUnitDefaults
//
// Output is written to:
//     <Project>/Saved/ReditusMigration/PlayerUnitDefaults_Import.csv
//     <Project>/Saved/ReditusMigration/PlayerUnitDefaults_AllCandidates.csv
//     <Project>/Saved/ReditusMigration/PlayerUnitDefaults_Expanded.csv
//     <Project>/Saved/ReditusMigration/PlayerUnitDefaults_AllEditable.csv
//     <Project>/Saved/ReditusMigration/PlayerUnitDefaults_ExporterLog.txt
//
// PlayerUnitDefaults_Import.csv is the filtered Unreal DataTable import file.
// The other files preserve every candidate, expanded stat, and editable CDO
// property so nothing is silently lost during migration.

#include "CoreMinimal.h"
#include <initializer_list>

#if WITH_EDITOR

#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"

namespace ReditusPlayerDefaultsExporter
{
	static const TCHAR* PlayerBaseBlueprintObjectPath =
		TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Units/PlayerUnits/BP_PlayerUnitBase.BP_PlayerUnitBase");

	static const TCHAR* PlayerContentRelativePath =
		TEXT("TurnBasedJRPGTemplate/Blueprints/Units/PlayerUnits");

	struct FImportColumn
	{
		FString Header;
		TArray<FString> SourcePropertyCandidates;
		FString ForcedValue;
		bool bUseForcedValue = false;
		bool bDeriveAssetID = false;

		FImportColumn(const TCHAR* InHeader, std::initializer_list<const TCHAR*> InCandidates)
			: Header(InHeader)
		{
			for (const TCHAR* Candidate : InCandidates)
			{
				SourcePropertyCandidates.Add(Candidate);
			}
		}

		static FImportColumn Forced(const TCHAR* InHeader, const TCHAR* InValue)
		{
			FImportColumn Column(InHeader, {});
			Column.ForcedValue = InValue;
			Column.bUseForcedValue = true;
			return Column;
		}

		static FImportColumn AssetID(
			const TCHAR* InHeader,
			std::initializer_list<const TCHAR*> InCandidates)
		{
			FImportColumn Column(InHeader, InCandidates);
			Column.bDeriveAssetID = true;
			return Column;
		}
	};

	static FString CsvEscape(const FString& InValue)
	{
		FString Escaped = InValue;
		Escaped.ReplaceInline(TEXT("\""), TEXT("\"\""));
		return FString::Printf(TEXT("\"%s\""), *Escaped);
	}

	static FProperty* FindPropertyByCandidate(
		UObject* Object,
		const TArray<FString>& CandidateNames)
	{
		if (!Object)
		{
			return nullptr;
		}

		for (const FString& Candidate : CandidateNames)
		{
			if (FProperty* Exact = Object->GetClass()->FindPropertyByName(FName(*Candidate)))
			{
				return Exact;
			}

			for (TFieldIterator<FProperty> It(
				Object->GetClass(),
				EFieldIteratorFlags::IncludeSuper); It; ++It)
			{
				FProperty* Property = *It;
				const FString PropertyName = Property->GetName();
				const FString DisplayName = Property->GetDisplayNameText().ToString();

				if (PropertyName.Equals(Candidate, ESearchCase::IgnoreCase) ||
					PropertyName.StartsWith(Candidate + TEXT("_"), ESearchCase::IgnoreCase) ||
					DisplayName.Equals(Candidate, ESearchCase::IgnoreCase))
				{
					return Property;
				}
			}
		}

		return nullptr;
	}

	static FString ExportPropertyValue(
		UObject* Object,
		const TArray<FString>& CandidateNames)
	{
		FProperty* Property = FindPropertyByCandidate(Object, CandidateNames);
		if (!Property)
		{
			return TEXT("<PROPERTY_NOT_FOUND>");
		}

		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
		FString OutValue;
		Property->ExportTextItem(OutValue, ValuePtr, nullptr, Object, PPF_None);
		return OutValue;
	}

	static FString DeriveStableIDFromReference(const FString& ExportedReference)
	{
		if (ExportedReference.IsEmpty() ||
			ExportedReference.Equals(TEXT("None"), ESearchCase::IgnoreCase) ||
			ExportedReference.StartsWith(TEXT("<")))
		{
			return TEXT("None");
		}

		FString ObjectPath = ExportedReference;
		int32 FirstQuote = INDEX_NONE;
		int32 LastQuote = INDEX_NONE;

		if (ObjectPath.FindChar(TEXT('\''), FirstQuote) &&
			ObjectPath.FindLastChar(TEXT('\''), LastQuote) &&
			LastQuote > FirstQuote)
		{
			ObjectPath = ObjectPath.Mid(FirstQuote + 1, LastQuote - FirstQuote - 1);
		}

		FString AssetName = FPackageName::ObjectPathToObjectName(ObjectPath);
		AssetName.RemoveFromEnd(TEXT("_C"));
		AssetName.RemoveFromStart(TEXT("BP_"));

		return AssetName.IsEmpty() ? TEXT("None") : AssetName;
	}

	static FString MakeSuggestedRowName(const FString& AssetName)
	{
		FString RowName = AssetName;
		RowName.RemoveFromStart(TEXT("BP_"));
		RowName.RemoveFromEnd(TEXT("PlayerUnit"));
		RowName.RemoveFromEnd(TEXT("Recruit"));
		return RowName;
	}

	static bool IsTemplatePlayerClass(const FString& AssetName)
	{
		static const TSet<FString> TemplateNames =
		{
			TEXT("BP_PlayerUnitBase"),
			TEXT("BP_MagePlayerUnit"),
			TEXT("BP_PriestPlayerUnit"),
			TEXT("BP_ArcherPlayerUnit"),
			TEXT("BP_SwordsmanPlayerUnit")
		};

		return TemplateNames.Contains(AssetName);
	}

	static bool IsReditusUnitBlueprintProperty(const FProperty* Property)
	{
		if (!Property || !Property->GetOwnerStruct())
		{
			return false;
		}

		return Property->GetOwnerStruct()->GetPathName().Contains(
			TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Units/"));
	}

	static FString GetColumnValue(UObject* CDO, const FImportColumn& Column)
	{
		if (Column.bUseForcedValue)
		{
			return Column.ForcedValue;
		}

		const FString Exported = ExportPropertyValue(CDO, Column.SourcePropertyCandidates);
		return Column.bDeriveAssetID
			? DeriveStableIDFromReference(Exported)
			: Exported;
	}

	static void AppendImportRow(
		FString& Csv,
		const FString& RowName,
		UObject* CDO,
		const TArray<FImportColumn>& Columns)
	{
		TArray<FString> Fields;
		Fields.Add(CsvEscape(RowName));

		for (const FImportColumn& Column : Columns)
		{
			Fields.Add(CsvEscape(GetColumnValue(CDO, Column)));
		}

		Csv += FString::Join(Fields, TEXT(","));
		Csv += LINE_TERMINATOR;
	}

	static void AppendExpandedStructMembers(
		TArray<FString>& Headers,
		TArray<FString>& Values,
		UObject* CDO,
		const FString& StructPropertyCandidate,
		const FString& HeaderPrefix)
	{
		FProperty* Property = FindPropertyByCandidate(CDO, { StructPropertyCandidate });
		FStructProperty* StructProperty = CastField<FStructProperty>(Property);

		if (!StructProperty || !StructProperty->Struct)
		{
			Headers.Add(HeaderPrefix + TEXT("_Error"));
			Values.Add(TEXT("<STRUCT_NOT_FOUND>"));
			return;
		}

		void* StructPtr = StructProperty->ContainerPtrToValuePtr<void>(CDO);

		for (TFieldIterator<FProperty> It(StructProperty->Struct); It; ++It)
		{
			FProperty* Member = *It;
			const void* MemberPtr = Member->ContainerPtrToValuePtr<void>(StructPtr);
			FString MemberValue;
			Member->ExportTextItem(MemberValue, MemberPtr, nullptr, CDO, PPF_None);

			Headers.Add(
				HeaderPrefix + TEXT("_") + Member->GetDisplayNameText().ToString());
			Values.Add(MemberValue);
		}
	}

	static void ExportPlayerUnitDefaults()
	{
		UE_LOG(LogTemp, Display,
			TEXT("[Reditus Player Export] Starting resolved player-unit CDO export..."));

		UBlueprint* PlayerBaseBlueprint =
			LoadObject<UBlueprint>(nullptr, PlayerBaseBlueprintObjectPath);

		if (!PlayerBaseBlueprint || !PlayerBaseBlueprint->GeneratedClass)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[Reditus Player Export] Could not load BP_PlayerUnitBase at %s"),
				PlayerBaseBlueprintObjectPath);
			return;
		}

		UClass* PlayerBaseClass = PlayerBaseBlueprint->GeneratedClass;
		const FString PlayerDiskDirectory =
			FPaths::Combine(FPaths::ProjectContentDir(), PlayerContentRelativePath);

		TArray<FString> PlayerAssetFiles;
		IFileManager::Get().FindFilesRecursive(
			PlayerAssetFiles,
			*PlayerDiskDirectory,
			TEXT("*.uasset"),
			true,
			false,
			false);

		PlayerAssetFiles.Sort();

		const TArray<FImportColumn> ImportColumns =
		{
			FImportColumn(TEXT("DisplayName"), { TEXT("title"), TEXT("DisplayName") }),
			FImportColumn(TEXT("ExpExponent"), { TEXT("expExponent") }),
			FImportColumn(TEXT("MaxLevel"), { TEXT("maxLevel") }),
			FImportColumn(TEXT("Initial Stats"), { TEXT("initialStats") }),
			FImportColumn(TEXT("First Level Stats"), { TEXT("firstLevelStats") }),
			FImportColumn(TEXT("Last Level Stats"), { TEXT("lastLevelStats") }),
			FImportColumn(TEXT("Battle Image"), { TEXT("battleImage") }),
			FImportColumn(TEXT("Big Image"), { TEXT("bigImage") }),
			FImportColumn(TEXT("Exploration Skills"), { TEXT("exploreSkills") }),
			FImportColumn(TEXT("Battle skills"), { TEXT("battleSkills") }),
			FImportColumn(TEXT("Overdrive skills"), { TEXT("OverDriveSkills") }),
			FImportColumn(TEXT("Unit Class"), { TEXT("unitClass") }),
			FImportColumn(TEXT("Movement Speed"), { TEXT("moveSpeed"), TEXT("Movement Speed") }),
			FImportColumn(TEXT("IsRangedUnit"), { TEXT("IsRangedUnit"), TEXT("Is Ranged Unit") }),
			FImportColumn(TEXT("DefaultProjectileClass"), { TEXT("projectile"), TEXT("Projectile") }),

			// The old project stores a single Blueprint affinity enum. The new field
			// is a map of C++ element -> response enums, so automatic conversion
			// would be unsafe. The old values are preserved in Expanded and Audit.
			FImportColumn::Forced(TEXT("Element Affinity"), TEXT("")),

			FImportColumn(TEXT("ExploreCharacter"), { TEXT("exploreCharacter") }),
			FImportColumn::Forced(TEXT("StartingWeaponID"), TEXT("None")),
			FImportColumn::Forced(TEXT("StartingArmorID"), TEXT("None")),
			FImportColumn::Forced(TEXT("StartingBootsID"), TEXT("None")),
			FImportColumn::Forced(TEXT("StartingHelmID"), TEXT("None")),
			FImportColumn::AssetID(TEXT("CoreDropID"), { TEXT("Robot Core") })
		};

		FString ImportCsv = TEXT("---");
		FString AllCandidatesCsv = TEXT("---");
		for (const FImportColumn& Column : ImportColumns)
		{
			ImportCsv += TEXT(",") + CsvEscape(Column.Header);
			AllCandidatesCsv += TEXT(",") + CsvEscape(Column.Header);
		}
		ImportCsv += LINE_TERMINATOR;
		AllCandidatesCsv += LINE_TERMINATOR;

		FString ExpandedCsv;
		FString AuditCsv =
			TEXT("SuggestedRowName,BlueprintAsset,BlueprintObjectPath,GeneratedClassPath,")
			TEXT("PropertyOwner,PropertyName,FriendlyName,Category,Value") LINE_TERMINATOR;

		TArray<FString> ExportLogLines;
		int32 CandidateCount = 0;
		int32 ImportCount = 0;
		bool bExpandedHeaderWritten = false;

		for (const FString& AssetFilename : PlayerAssetFiles)
		{
			FString LongPackageName;
			if (!FPackageName::TryConvertFilenameToLongPackageName(
				AssetFilename, LongPackageName))
			{
				continue;
			}

			const FString AssetName = FPaths::GetBaseFilename(AssetFilename);
			const FString BlueprintObjectPath = LongPackageName + TEXT(".") + AssetName;

			UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintObjectPath);
			if (!Blueprint || !Blueprint->GeneratedClass)
			{
				continue;
			}

			UClass* PlayerClass = Blueprint->GeneratedClass;
			if (PlayerClass == PlayerBaseClass ||
				!PlayerClass->IsChildOf(PlayerBaseClass) ||
				PlayerClass->HasAnyClassFlags(CLASS_Abstract))
			{
				continue;
			}

			UObject* CDO = PlayerClass->GetDefaultObject();
			if (!CDO)
			{
				continue;
			}

			const FString RowName = MakeSuggestedRowName(AssetName);
			AppendImportRow(AllCandidatesCsv, RowName, CDO, ImportColumns);
			++CandidateCount;

			const bool bTemplate = IsTemplatePlayerClass(AssetName);
			if (!bTemplate)
			{
				AppendImportRow(ImportCsv, RowName, CDO, ImportColumns);
				++ImportCount;
			}

			TArray<FString> ExpandedHeaders =
			{
				TEXT("SuggestedRowName"),
				TEXT("BlueprintAsset"),
				TEXT("IncludedInImport"),
				TEXT("LegacyElementAffinity"),
				TEXT("LegacyDefaultElementAffinity"),
				TEXT("LegacySecondaryElementAffinity"),
				TEXT("LegacyRobotCoreClass")
			};

			TArray<FString> ExpandedValues =
			{
				RowName,
				AssetName,
				bTemplate ? TEXT("False") : TEXT("True"),
				ExportPropertyValue(CDO, { TEXT("Element Affinity") }),
				ExportPropertyValue(CDO, { TEXT("Default Element Affinity") }),
				ExportPropertyValue(CDO, { TEXT("Secondary Element Affinity") }),
				ExportPropertyValue(CDO, { TEXT("Robot Core") })
			};

			AppendExpandedStructMembers(
				ExpandedHeaders, ExpandedValues, CDO, TEXT("initialStats"), TEXT("Initial"));
			AppendExpandedStructMembers(
				ExpandedHeaders, ExpandedValues, CDO, TEXT("firstLevelStats"), TEXT("FirstLevel"));
			AppendExpandedStructMembers(
				ExpandedHeaders, ExpandedValues, CDO, TEXT("lastLevelStats"), TEXT("LastLevel"));

			if (!bExpandedHeaderWritten)
			{
				TArray<FString> EscapedHeaders;
				for (const FString& Header : ExpandedHeaders)
				{
					EscapedHeaders.Add(CsvEscape(Header));
				}
				ExpandedCsv += FString::Join(EscapedHeaders, TEXT(","));
				ExpandedCsv += LINE_TERMINATOR;
				bExpandedHeaderWritten = true;
			}

			TArray<FString> EscapedValues;
			for (const FString& Value : ExpandedValues)
			{
				EscapedValues.Add(CsvEscape(Value));
			}
			ExpandedCsv += FString::Join(EscapedValues, TEXT(","));
			ExpandedCsv += LINE_TERMINATOR;

			for (TFieldIterator<FProperty> It(
				PlayerClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
			{
				FProperty* Property = *It;
				if (!Property->HasAnyPropertyFlags(CPF_Edit) ||
					!IsReditusUnitBlueprintProperty(Property))
				{
					continue;
				}

				const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(CDO);
				FString Value;
				Property->ExportTextItem(Value, ValuePtr, nullptr, CDO, PPF_None);

				const UStruct* Owner = Property->GetOwnerStruct();
				TArray<FString> Fields =
				{
					CsvEscape(RowName),
					CsvEscape(AssetName),
					CsvEscape(BlueprintObjectPath),
					CsvEscape(PlayerClass->GetPathName()),
					CsvEscape(Owner ? Owner->GetPathName() : FString()),
					CsvEscape(Property->GetName()),
					CsvEscape(Property->GetDisplayNameText().ToString()),
					CsvEscape(Property->GetMetaData(TEXT("Category"))),
					CsvEscape(Value)
				};

				AuditCsv += FString::Join(Fields, TEXT(","));
				AuditCsv += LINE_TERMINATOR;
			}

			ExportLogLines.Add(FString::Printf(
				TEXT("%s -> %s (%s)"),
				*AssetName,
				*RowName,
				bTemplate ? TEXT("audit only") : TEXT("included")));
		}

		const FString OutputDirectory =
			FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ReditusMigration"));
		IFileManager::Get().MakeDirectory(*OutputDirectory, true);

		const FString ImportPath = FPaths::Combine(
			OutputDirectory, TEXT("PlayerUnitDefaults_Import.csv"));
		const FString CandidatesPath = FPaths::Combine(
			OutputDirectory, TEXT("PlayerUnitDefaults_AllCandidates.csv"));
		const FString ExpandedPath = FPaths::Combine(
			OutputDirectory, TEXT("PlayerUnitDefaults_Expanded.csv"));
		const FString AuditPath = FPaths::Combine(
			OutputDirectory, TEXT("PlayerUnitDefaults_AllEditable.csv"));
		const FString ExporterLogPath = FPaths::Combine(
			OutputDirectory, TEXT("PlayerUnitDefaults_ExporterLog.txt"));

		FString LogText;
		LogText += FString::Printf(TEXT("Imported row candidates: %d%s"),
			ImportCount, LINE_TERMINATOR);
		LogText += FString::Printf(TEXT("All concrete candidates: %d%s"),
			CandidateCount, LINE_TERMINATOR);
		LogText += TEXT("Element Affinity is intentionally blank in the import CSV because the old single Blueprint enum cannot be safely converted into the new element-response map.") LINE_TERMINATOR;
		LogText += TEXT("The three old affinity values are preserved in PlayerUnitDefaults_Expanded.csv.") LINE_TERMINATOR LINE_TERMINATOR;
		LogText += FString::Join(ExportLogLines, LINE_TERMINATOR);

		const bool bSaved =
			FFileHelper::SaveStringToFile(ImportCsv, *ImportPath,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) &&
			FFileHelper::SaveStringToFile(AllCandidatesCsv, *CandidatesPath,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) &&
			FFileHelper::SaveStringToFile(ExpandedCsv, *ExpandedPath,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) &&
			FFileHelper::SaveStringToFile(AuditCsv, *AuditPath,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) &&
			FFileHelper::SaveStringToFile(LogText, *ExporterLogPath,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

		if (bSaved)
		{
			UE_LOG(LogTemp, Display,
				TEXT("[Reditus Player Export] SUCCESS. Exported %d import rows and %d total candidates."),
				ImportCount, CandidateCount);
			UE_LOG(LogTemp, Display,
				TEXT("[Reditus Player Export] Import CSV: %s"), *ImportPath);
			UE_LOG(LogTemp, Display,
				TEXT("[Reditus Player Export] Expanded CSV: %s"), *ExpandedPath);
		}
		else
		{
			UE_LOG(LogTemp, Error,
				TEXT("[Reditus Player Export] Failed to save one or more files under %s"),
				*OutputDirectory);
		}
	}

	static FAutoConsoleCommand ExportPlayerUnitDefaultsCommand(
		TEXT("Reditus.ExportPlayerUnitDefaults"),
		TEXT("Export resolved Blueprint defaults for all concrete Reditus player units."),
		FConsoleCommandDelegate::CreateStatic(&ExportPlayerUnitDefaults));
}

#endif // WITH_EDITOR
