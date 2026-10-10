#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Data/ReditusEnemyDefinition.h"
#include "ReditusDataLibrary.generated.h"

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
		FReditusEnemyDefinition& OutEnemyDefinition
	);
};