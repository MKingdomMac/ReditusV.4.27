#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Data/ReditusUnitStats.h"
#include "ReditusDifficultyRules.generated.h"

UCLASS()
class CHAPTER1REDITUS_API UReditusDifficultyRules : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintPure, Category = "Reditus|Combat|Difficulty")
	static FReditusUnitStats ApplyEnemyDifficultyToStats(
		const FReditusUnitStats& BaseStats,
		bool bEasyDifficulty,
		bool bIgnoreDifficultyScaling
	);

	UFUNCTION(BlueprintPure, Category = "Reditus|Combat|Difficulty")
	static bool IsEnemyAMPEnabledForDifficulty(bool bEasyDifficulty);

	UFUNCTION(BlueprintPure, Category = "Reditus|Combat|Difficulty")
	static bool CanEnemyUseAMP(bool bCanUseAMP, bool bEasyDifficulty);
};
