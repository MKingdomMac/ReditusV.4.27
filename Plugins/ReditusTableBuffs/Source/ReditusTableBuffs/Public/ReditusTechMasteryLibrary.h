#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ReditusTechMasteryLibrary.generated.h"

class AActor;

USTRUCT()
struct FReditusCharacterMastery
{
    GENERATED_BODY()

    UPROPERTY(SaveGame)
    TMap<FName, int32> Progress;

    UPROPERTY(SaveGame)
    TArray<FName> Learned;
};

USTRUCT(BlueprintType)
struct REDITUSTABLEBUFFS_API FReditusTechMasterySaveData
{
    GENERATED_BODY()

    UPROPERTY(SaveGame)
    TMap<FName, FReditusCharacterMastery> Characters;
};

struct FReditusEncounterMastery
{
    int32 Reward = 1;
    bool bAwarded = false;
    bool bAnyProgress = false;
    TArray<FText> Learned;
};

UCLASS()
class REDITUSTABLEBUFFS_API UReditusTechMasterySubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

    friend class UReditusTechMasteryLibrary;
    TMap<TWeakObjectPtr<AActor>, FReditusEncounterMastery> Encounters;
};

UCLASS()
class REDITUSTABLEBUFFS_API UReditusTechMasteryLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Reditus|Tech Mastery")
    static void BeginMasteryEncounter(AActor* Battle);

    UFUNCTION(BlueprintCallable, Category="Reditus|Tech Mastery")
    static void IncludeEnemyMasteryReward(AActor* Battle, AActor* Enemy, bool bTableEnemy);

    UFUNCTION(BlueprintCallable, Category="Reditus|Tech Mastery")
    static void AwardVictoryMastery(AActor* BattleController);

    UFUNCTION(BlueprintCallable, Category="Reditus|Tech Mastery")
    static void AppendLearnedTechs(UObject* SkillOwner);

    UFUNCTION(BlueprintPure, Category="Reditus|Tech Mastery")
    static FText FormatVictoryMastery(UObject* VictoryWidget, const FText& RewardText);

    UFUNCTION(BlueprintCallable, Category="Reditus|Tech Mastery")
    static void ShowMasteryOnSkillButton(UObject* Menu, UObject* SkillButton, FName SkillID);
};
