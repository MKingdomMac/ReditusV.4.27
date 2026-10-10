#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataTable.h"
#include "GameFramework/Actor.h"
#include "ReditusPlayerRosterSubsystem.h"
#include "ReditusJRPGControllerBridgeComponent.generated.h"

/**
 * Compatibility bridge attached to the EXISTING BP_JRPGPlayerController.
 *
 * We deliberately do NOT reparent BP_JRPGPlayerController in Phase 1.
 *
 * Existing Blueprint events/functions stay alive while their implementations
 * are migrated one at a time to the new C++ roster backend.
 */
UCLASS(
	ClassGroup = (Reditus),
	meta = (BlueprintSpawnableComponent)
)
class CHAPTER1REDITUS_API UReditusJRPGControllerBridgeComponent
	: public UActorComponent
{
	GENERATED_BODY()

public:

	UReditusJRPGControllerBridgeComponent();

protected:

	virtual void BeginPlay() override;

public:

	// ---------------------------------------------------------------------
	// EDITOR CONFIGURATION
	// ---------------------------------------------------------------------

	/**
	 * Assign DT_PlayerUnits here on BP_JRPGPlayerController.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reditus|Player Roster")
	UDataTable* PlayerUnitDataTable = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reditus|Player Roster",
		meta = (ClampMin = "1"))
	int32 MaxPartySize = 4;

	// ---------------------------------------------------------------------
	// BACKEND ACCESS
	// ---------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Reditus|Player Roster")
	UReditusPlayerRosterSubsystem* GetPlayerRosterSubsystem() const;

	UFUNCTION(BlueprintCallable, Category = "Reditus|Player Roster")
	void RefreshRosterConfiguration();

	// ---------------------------------------------------------------------
	// NEW ID-BASED API
	// ---------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Reditus|Player Roster")
	bool RecruitPlayerUnitByID(
		FName PlayerUnitID,
		bool bAddToPartyIfSpace = false
	);

	UFUNCTION(BlueprintCallable, Category = "Reditus|Player Roster")
	bool AddPlayerUnitToPartyByID(FName PlayerUnitID);

	UFUNCTION(BlueprintCallable, Category = "Reditus|Player Roster")
	bool RemovePlayerUnitFromPartyByID(FName PlayerUnitID);

	UFUNCTION(BlueprintCallable, Category = "Reditus|Player Roster")
	bool SwapFormationMembersByID(
		FName PlayerUnitA,
		FName PlayerUnitB
	);

	// ---------------------------------------------------------------------
	// LEGACY CLASS-BASED MIGRATION API
	// ---------------------------------------------------------------------

	/**
	 * Use this inside the EXISTING AddPlayerUnit Blueprint event first.
	 *
	 * Old callers can keep passing BP_PlayerUnitBase classes.
	 * The bridge resolves the class to the DataTable row ID.
	 */
	UFUNCTION(BlueprintCallable, Category = "Reditus|Player Roster|Legacy")
	bool RecruitLegacyPlayerUnitClass(
		TSubclassOf<AActor> LegacyPlayerUnitClass,
		bool bAddToPartyIfSpace,
		FName& OutPlayerUnitID
	);

	UFUNCTION(BlueprintCallable, Category = "Reditus|Player Roster|Legacy")
	bool AddLegacyPlayerUnitToParty(
		TSubclassOf<AActor> LegacyPlayerUnitClass,
		FName& OutPlayerUnitID
	);

	UFUNCTION(BlueprintCallable, Category = "Reditus|Player Roster|Legacy")
	bool RemoveLegacyPlayerUnitFromParty(
		TSubclassOf<AActor> LegacyPlayerUnitClass,
		FName& OutPlayerUnitID
	);

	UFUNCTION(BlueprintCallable, Category = "Reditus|Player Roster|Legacy")
	bool SwapLegacyFormationMembers(
		TSubclassOf<AActor> LegacyPlayerUnitA,
		TSubclassOf<AActor> LegacyPlayerUnitB
	);

	UFUNCTION(BlueprintCallable, Category = "Reditus|Player Roster|Legacy")
	bool ResolvePlayerUnitIDFromLegacyClass(
		TSubclassOf<AActor> LegacyPlayerUnitClass,
		FName& OutPlayerUnitID
	) const;

	UFUNCTION(BlueprintCallable, Category = "Reditus|Player Roster|Legacy")
	bool GetLegacyPlayerUnitClass(
		FName PlayerUnitID,
		TSubclassOf<AActor>& OutLegacyPlayerUnitClass
	) const;

	/**
	 * One-shot migration helper.
	 *
	 * BP_JRPGPlayerController can pass:
	 *   - Map Keys(playerUnits)
	 *   - partyMembers
	 *   - Party Members Formation
	 *
	 * The C++ backend will resolve them to stable PlayerUnitIDs and mirror the
	 * current controller state WITHOUT changing the three old variables.
	 */
	UFUNCTION(BlueprintCallable, Category = "Reditus|Player Roster|Legacy")
	bool ImportLegacyControllerMembership(
		const TArray<TSubclassOf<AActor>>& LegacyRecruitedClasses,
		const TArray<TSubclassOf<AActor>>& LegacyPartyMemberClasses,
		const TArray<TSubclassOf<AActor>>& LegacyFormationClasses
	);

private:

	bool ResolveLegacyClassArray(
		const TArray<TSubclassOf<AActor>>& LegacyClasses,
		TArray<FName>& OutPlayerUnitIDs
	) const;
};
