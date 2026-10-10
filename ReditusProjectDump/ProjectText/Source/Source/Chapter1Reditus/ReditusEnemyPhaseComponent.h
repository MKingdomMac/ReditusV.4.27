#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/ReditusEnemyDefinition.h"
#include "ReditusEnemyPhaseComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FReditusEnemyPhaseChangedSignature,
	int32, NewPhaseIndex,
	FReditusEnemyPhase, NewPhase
);

UCLASS(ClassGroup=(Reditus), meta=(BlueprintSpawnableComponent))
class CHAPTER1REDITUS_API UReditusEnemyPhaseComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UReditusEnemyPhaseComponent();

	UPROPERTY(BlueprintAssignable, Category = "Reditus|Enemy Phase")
	FReditusEnemyPhaseChangedSignature OnPhaseChanged;

	UPROPERTY(BlueprintReadOnly, Category = "Reditus|Enemy Phase")
	int32 CurrentPhaseIndex = -1;

	UPROPERTY(BlueprintReadOnly, Category = "Reditus|Enemy Phase")
	int32 TotalStaggerCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Reditus|Enemy Phase")
	int32 CurrentTurnNumber = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Reditus|Enemy Phase")
	TMap<EReditusElementAffinity, EReditusElementResponse> CurrentElementAffinity;

	UPROPERTY(BlueprintReadOnly, Category = "Reditus|Enemy Phase")
	TArray<FReditusEnemySkillEntry> CurrentSkills;

	UPROPERTY(BlueprintReadOnly, Category = "Reditus|Enemy Phase")
	FName CurrentAIProfileID = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Reditus|Enemy Phase")
	FSoftClassPath CurrentSpecialBehaviorClass;

	UFUNCTION(BlueprintCallable, Category = "Reditus|Enemy Phase")
	void InitializeFromEnemyDefinition(
		const FReditusEnemyDefinition& EnemyDefinition
	);

	UFUNCTION(BlueprintCallable, Category = "Reditus|Enemy Phase")
	bool NotifyEnemyStaggered();

	UFUNCTION(BlueprintCallable, Category = "Reditus|Enemy Phase")
	bool NotifyHPChanged(
		float CurrentHP,
		float MaxHP
	);

	UFUNCTION(BlueprintCallable, Category = "Reditus|Enemy Phase")
	bool NotifyTurnStarted(
		int32 TurnNumber
	);

	UFUNCTION(BlueprintCallable, Category = "Reditus|Enemy Phase")
	bool TriggerCustomPhase(
		FName PhaseID
	);

	UFUNCTION(BlueprintCallable, Category = "Reditus|Enemy Phase")
	bool ForceNextPhase();

	UFUNCTION(BlueprintPure, Category = "Reditus|Enemy Phase")
	bool HasNextPhase() const;

	UFUNCTION(BlueprintPure, Category = "Reditus|Enemy Phase")
	bool GetCurrentPhase(
		FReditusEnemyPhase& OutPhase
	) const;

private:

	UPROPERTY()
	TArray<FReditusEnemyPhase> PhaseDefinitions;

	bool DoesNextPhaseMatchStagger() const;
	bool DoesNextPhaseMatchHP(float CurrentHP, float MaxHP) const;
	bool DoesNextPhaseMatchTurn(int32 TurnNumber) const;
	bool EnterNextPhase();
};
