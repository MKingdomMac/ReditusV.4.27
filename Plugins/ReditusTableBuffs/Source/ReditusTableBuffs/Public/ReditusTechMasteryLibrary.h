#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ReditusTechMasteryLibrary.generated.h"

class AActor;
struct FReditusResultsPresentation;

USTRUCT()
struct FReditusCharacterMastery
{
    GENERATED_BODY()

    UPROPERTY(SaveGame)
    TMap<FName, int32> Progress;

    UPROPERTY(SaveGame)
    TArray<FName> Learned;

    UPROPERTY(SaveGame)
    TMap<FName, int32> OverdriveProgress;

    UPROPERTY(SaveGame)
    TArray<FName> LearnedOverdrives;
};

USTRUCT(BlueprintType)
struct REDITUSTABLEBUFFS_API FReditusTechMasterySaveData
{
    GENERATED_BODY()

    UPROPERTY(SaveGame)
    TMap<FName, FReditusCharacterMastery> Characters;
};

struct FReditusVictoryTechProgress
{
    FText Character;
    FText Tech;
    int32 Current;
    int32 Required;
};

struct FReditusEncounterMastery
{
    int32 Reward = 1;
    bool bAwarded = false;
    bool bAnyProgress = false;
    TArray<FText> Learned;
    TArray<FReditusVictoryTechProgress> Progress;
    TMap<FName, int32> PreAwardExperience;
};

UCLASS()
class REDITUSTABLEBUFFS_API UReditusTechMasterySubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

    friend class UReditusTechMasteryLibrary;
    TMap<TWeakObjectPtr<AActor>, FReditusEncounterMastery> Encounters;
    TWeakObjectPtr<AActor> LastVictory;
    TMap<TWeakObjectPtr<AActor>, TArray<TWeakObjectPtr<AActor>>> HiddenIntroUnits;
    TSharedPtr<FReditusResultsPresentation> ResultsPresentation;
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

    UFUNCTION(BlueprintCallable, Category="Reditus|Tech Mastery")
    static void AppendEquipmentAndLearnedOverdrives(UObject* SkillOwner);

    UFUNCTION(BlueprintCallable, Category="Reditus|Tech Mastery")
    static void ShowEquipmentSkillPages(UObject* Menu);

    UFUNCTION(BlueprintPure, Category="Reditus|Tech Mastery")
    static FText FormatVictoryMastery(UObject* VictoryWidget, const FText& RewardText);

    UFUNCTION(BlueprintCallable, Category="Reditus|Tech Mastery")
    static void ShowMasteryOnSkillButton(UObject* Menu, UObject* SkillButton, FName SkillID);

    UFUNCTION(BlueprintPure, Category="Reditus|Tech Mastery")
    static FText FormatSkillMasteryDescription(UObject* Menu, FName SkillID, const FText& Description);

    UFUNCTION(BlueprintCallable, Category="Reditus|Tech Mastery")
    static void UpdateSelectedTechMastery(UObject* Menu, FName SkillID);

    UFUNCTION(BlueprintCallable, Category="Reditus|Tech Mastery")
    static void ShowEquipmentMastery(UObject* Menu, UObject* Details, FName EquipmentID);

    UFUNCTION(BlueprintPure, Category="Reditus|Tech Mastery")
    static bool IsTechMastered(UObject* Unit, FName SkillID);

    UFUNCTION(BlueprintPure, Category="Reditus|Tech Mastery")
    static FText DescribeEquippedTech(UObject* Unit, FName SkillID);

    UFUNCTION(BlueprintPure, Category="Reditus|Tech Mastery")
    static bool IsOverdriveMastered(UObject* Unit, FName SkillID);

    UFUNCTION(BlueprintPure, Category="Reditus|Tech Mastery")
    static FText DescribeEquippedOverdrive(UObject* Unit, FName SkillID);

    UFUNCTION(BlueprintPure, Category="Reditus|Overdrive")
    static FText GetOverdrivePartnerMessage(UObject* Unit, FName SkillID);

    UFUNCTION(BlueprintPure, Category="Reditus|Overdrive")
    static bool CanConfirmOverdrive(UObject* Menu, FName SkillID);

    UFUNCTION(BlueprintCallable, Category="Reditus|Overdrive")
    static void UpdateOverdriveDescription(UObject* Menu, FName SkillID);

    UFUNCTION(BlueprintCallable, Category="Reditus|Battle Presentation")
    static void PresentVictory(UObject* VictoryWidget);

    UFUNCTION(BlueprintCallable, Category="Reditus|Battle Presentation")
    static void PresentLevelUp(UObject* LevelUpWidget);

    UFUNCTION(BlueprintCallable, Category="Reditus|Battle Presentation")
    static void HoldBattleUnitsForHUD(AActor* Battle);

    UFUNCTION(BlueprintCallable, Category="Reditus|Battle Presentation")
    static void ReleaseBattleIntro(AActor* BattleController);

private:
    static void ResolveVictoryMastery(AActor* BattleController, bool bBeforeExperienceAward);
};
