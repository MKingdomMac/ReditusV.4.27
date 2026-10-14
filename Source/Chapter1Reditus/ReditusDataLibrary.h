#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Data/ReditusEnemyDefinition.h"
#include "ReditusDataLibrary.generated.h"

class AActor;
class UDataTable;

UCLASS()
class CHAPTER1REDITUS_API UReditusDataLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Reditus|Data|Enemies")
	static bool GetEnemyDefinition(
		UDataTable* EnemyDataTable,
		FName RowName,
		FReditusEnemyDefinition& OutEnemyDefinition);

	/** Read one enemy row and resolve its existing Actor class before spawning. */
	UFUNCTION(BlueprintCallable, Category = "Reditus|Data|Enemies")
	static bool ResolveEnemySpawnDefinition(
		UDataTable* EnemyDataTable,
		FName EnemyID,
		TSubclassOf<AActor> ExpectedEnemyBaseClass,
		FReditusEnemyDefinition& OutEnemyDefinition,
		TSubclassOf<AActor>& OutEnemyActorClass);

	/**
	 * Migration helper for configured encounters that currently contain classes.
	 * Returns false when a class has no row or has more than one matching row.
	 * Spawn chance and level ranges remain in the encounter, outside DT_Enemies.
	 */
	UFUNCTION(BlueprintCallable, Category = "Reditus|Data|Enemies|Migration")
	static bool FindEnemyIDForLegacyClass(
		UDataTable* EnemyDataTable,
		TSubclassOf<AActor> LegacyEnemyClass,
		FName& OutEnemyID);

	/** Central adapter: existing map selections can remain unchanged. */
	UFUNCTION(BlueprintCallable, Category = "Reditus|Data|Enemies|Migration")
	static bool ResolveLegacyEnemySpawnDefinition(
		UDataTable* EnemyDataTable,
		TSubclassOf<AActor> LegacyEnemyClass,
		TSubclassOf<AActor> ExpectedEnemyBaseClass,
		FName& OutEnemyID,
		FReditusEnemyDefinition& OutEnemyDefinition,
		TSubclassOf<AActor>& OutEnemyActorClass);
};
