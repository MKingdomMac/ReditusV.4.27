#include "ReditusDataLibrary.h"
#include "Engine/DataTable.h"
#include "GameFramework/Actor.h"

bool UReditusDataLibrary::GetEnemyDefinition(
	UDataTable* EnemyDataTable,
	FName RowName,
	FReditusEnemyDefinition& OutEnemyDefinition)
{
	OutEnemyDefinition = FReditusEnemyDefinition();
	if (!EnemyDataTable || RowName.IsNone())
	{
		return false;
	}

	if (EnemyDataTable->GetRowStruct() != FReditusEnemyDefinition::StaticStruct())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[Reditus Enemies] %s does not use FReditusEnemyDefinition."),
			*GetNameSafe(EnemyDataTable));
		return false;
	}

	static const FString ContextString(TEXT("GetEnemyDefinition"));
	const FReditusEnemyDefinition* EnemyRow =
		EnemyDataTable->FindRow<FReditusEnemyDefinition>(RowName, ContextString, true);
	if (!EnemyRow)
	{
		return false;
	}

	OutEnemyDefinition = *EnemyRow;
	return true;
}

bool UReditusDataLibrary::ResolveEnemySpawnDefinition(
	UDataTable* EnemyDataTable,
	FName EnemyID,
	TSubclassOf<AActor> ExpectedEnemyBaseClass,
	FReditusEnemyDefinition& OutEnemyDefinition,
	TSubclassOf<AActor>& OutEnemyActorClass)
{
	OutEnemyActorClass = nullptr;
	if (!GetEnemyDefinition(EnemyDataTable, EnemyID, OutEnemyDefinition))
	{
		return false;
	}

	UClass* EnemyClass = OutEnemyDefinition.LegacyEnemyActorClass.TryLoadClass<AActor>();
	if (!EnemyClass ||
		(ExpectedEnemyBaseClass.Get() && !EnemyClass->IsChildOf(ExpectedEnemyBaseClass.Get())))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[Reditus Enemies] Row %s has an invalid or incompatible enemy Actor class: %s"),
			*EnemyID.ToString(), *OutEnemyDefinition.LegacyEnemyActorClass.ToString());
		OutEnemyDefinition = FReditusEnemyDefinition();
		return false;
	}

	OutEnemyActorClass = EnemyClass;
	return true;
}

bool UReditusDataLibrary::FindEnemyIDForLegacyClass(
	UDataTable* EnemyDataTable,
	TSubclassOf<AActor> LegacyEnemyClass,
	FName& OutEnemyID)
{
	OutEnemyID = NAME_None;
	if (!EnemyDataTable || !LegacyEnemyClass.Get() ||
		EnemyDataTable->GetRowStruct() != FReditusEnemyDefinition::StaticStruct())
	{
		return false;
	}

	const FString ClassPath = LegacyEnemyClass.Get()->GetPathName();
	static const FString ContextString(TEXT("FindEnemyIDForLegacyClass"));
	for (const FName RowName : EnemyDataTable->GetRowNames())
	{
		const FReditusEnemyDefinition* Row =
			EnemyDataTable->FindRow<FReditusEnemyDefinition>(RowName, ContextString, false);
		if (!Row || !Row->LegacyEnemyActorClass.ToString().Equals(ClassPath, ESearchCase::IgnoreCase))
		{
			continue;
		}

		if (!OutEnemyID.IsNone())
		{
			UE_LOG(LogTemp, Error,
				TEXT("[Reditus Enemies] Actor class %s matches multiple enemy rows; select an EnemyID explicitly."),
				*ClassPath);
			OutEnemyID = NAME_None;
			return false;
		}
		OutEnemyID = RowName;
	}
	return !OutEnemyID.IsNone();
}

bool UReditusDataLibrary::ResolveLegacyEnemySpawnDefinition(
	UDataTable* EnemyDataTable,
	TSubclassOf<AActor> LegacyEnemyClass,
	TSubclassOf<AActor> ExpectedEnemyBaseClass,
	FName& OutEnemyID,
	FReditusEnemyDefinition& OutEnemyDefinition,
	TSubclassOf<AActor>& OutEnemyActorClass)
{
	OutEnemyDefinition = FReditusEnemyDefinition();
	OutEnemyActorClass = nullptr;
	if (!FindEnemyIDForLegacyClass(EnemyDataTable, LegacyEnemyClass, OutEnemyID))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[Reditus Enemies] No unique enemy row matches configured Actor class %s."),
			*GetNameSafe(LegacyEnemyClass.Get()));
		return false;
	}
	return ResolveEnemySpawnDefinition(EnemyDataTable, OutEnemyID,
		ExpectedEnemyBaseClass, OutEnemyDefinition, OutEnemyActorClass);
}
