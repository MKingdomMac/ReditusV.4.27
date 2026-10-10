#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/DataTable.h"
#include "GameFramework/Actor.h"
#include "Data/ReditusPlayerUnitDefinition.h"
#include "ReditusPlayerRosterTypes.h"
#include "ReditusPlayerRosterSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FReditusRosterChangedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FReditusPartyChangedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FReditusFormationChangedSignature);

/**
 * Cross-level player roster backend.
 *
 * Why GameInstanceSubsystem?
 * The recruited roster / active party / formation are game-state concepts and
 * should not belong to one map or one spawned PlayerController instance.
 *
 * During migration BP_JRPGPlayerController remains in place as the
 * Blueprint-facing compatibility facade.
 */
UCLASS(BlueprintType)
class CHAPTER1REDITUS_API UReditusPlayerRosterSubsystem
	: public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:

	// ---------------------------------------------------------------------
	// CONFIGURATION
	// ---------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Reditus|Player Roster")
	void ConfigurePlayerRoster(
		UDataTable* InPlayerUnitDataTable,
		int32 InMaxPartySize = 4
	);

	UFUNCTION(BlueprintPure, Category = "Reditus|Player Roster")
	UDataTable* GetPlayerUnitDataTable() const
	{
		return PlayerUnitDataTable;
	}

	UFUNCTION(BlueprintPure, Category = "Reditus|Player Roster")
	int32 GetMaxPartySize() const
	{
		return MaxPartySize;
	}

	// ---------------------------------------------------------------------
	// DEFINITIONS / LEGACY CLASS RESOLUTION
	// ---------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Reditus|Player Roster|Data")
	bool GetPlayerUnitDefinition(
		FName PlayerUnitID,
		FReditusPlayerUnitDefinition& OutDefinition
	) const;

	UFUNCTION(BlueprintPure, Category = "Reditus|Player Roster|Data")
	bool DoesPlayerUnitDefinitionExist(FName PlayerUnitID) const;

	/**
	 * Transitional helper:
	 * existing Blueprint logic still passes BP_PlayerUnitBase classes.
	 * This resolves that class to the new stable DataTable RowName/PlayerUnitID.
	 */
	UFUNCTION(BlueprintCallable, Category = "Reditus|Player Roster|Legacy")
	bool ResolvePlayerUnitIDFromLegacyBattleClass(
		TSubclassOf<AActor> LegacyBattleClass,
		FName& OutPlayerUnitID
	) const;

	UFUNCTION(BlueprintCallable, Category = "Reditus|Player Roster|Legacy")
	bool GetLegacyBattleUnitClass(
		FName PlayerUnitID,
		TSubclassOf<AActor>& OutLegacyBattleClass
	) const;

	UFUNCTION(BlueprintCallable, Category = "Reditus|Player Roster|Legacy")
	bool GetLegacyExplorationCharacterClass(
		FName PlayerUnitID,
		TSubclassOf<AActor>& OutLegacyCharacterClass
	) const;

	// ---------------------------------------------------------------------
	// RECRUITED ROSTER
	// ---------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Reditus|Player Roster")
	bool RecruitPlayerUnit(
		FName PlayerUnitID,
		bool bAddToPartyIfSpace = false
	);

	UFUNCTION(BlueprintCallable, Category = "Reditus|Player Roster")
	bool RemovePlayerUnitFromRoster(FName PlayerUnitID);

	UFUNCTION(BlueprintPure, Category = "Reditus|Player Roster")
	bool IsPlayerUnitRecruited(FName PlayerUnitID) const;

	UFUNCTION(BlueprintPure, Category = "Reditus|Player Roster")
	TArray<FName> GetRecruitedUnitIDs() const
	{
		return RecruitedUnitIDs;
	}

	// ---------------------------------------------------------------------
	// ACTIVE PARTY
	// ---------------------------------------------------------------------

	/**
	 * Adds a recruited unit to the stable active-party list.
	 * The formation receives the unit at the end if it was not already there.
	 */
	UFUNCTION(BlueprintCallable, Category = "Reditus|Player Roster|Party")
	bool AddPlayerUnitToParty(FName PlayerUnitID);

	/**
	 * Removes the unit from BOTH active party and formation.
	 * It does NOT unrecruit the character.
	 */
	UFUNCTION(BlueprintCallable, Category = "Reditus|Player Roster|Party")
	bool RemovePlayerUnitFromParty(FName PlayerUnitID);

	UFUNCTION(BlueprintPure, Category = "Reditus|Player Roster|Party")
	bool IsPlayerUnitInParty(FName PlayerUnitID) const;

	UFUNCTION(BlueprintPure, Category = "Reditus|Player Roster|Party")
	TArray<FName> GetPartyMemberIDs() const
	{
		return PartyMemberIDs;
	}

	// ---------------------------------------------------------------------
	// FORMATION
	// ---------------------------------------------------------------------

	/**
	 * Formation order is intentionally independent from PartyMemberIDs.
	 */
	UFUNCTION(BlueprintCallable, Category = "Reditus|Player Roster|Formation")
	bool SwapFormationMembers(FName PlayerUnitA, FName PlayerUnitB);

	UFUNCTION(BlueprintCallable, Category = "Reditus|Player Roster|Formation")
	bool MoveFormationMember(FName PlayerUnitID, int32 NewIndex);

	/**
	 * Replaces formation order only if it contains exactly the same IDs
	 * as the active party.
	 */
	UFUNCTION(BlueprintCallable, Category = "Reditus|Player Roster|Formation")
	bool SetFormationOrder(const TArray<FName>& NewFormationOrder);

	UFUNCTION(BlueprintCallable, Category = "Reditus|Player Roster|Formation")
	void ResetFormationToPartyOrder();

	UFUNCTION(BlueprintPure, Category = "Reditus|Player Roster|Formation")
	TArray<FName> GetFormationUnitIDs() const
	{
		return FormationUnitIDs;
	}

	// ---------------------------------------------------------------------
	// SNAPSHOT / MIGRATION / SAVE HANDOFF
	// ---------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Reditus|Player Roster|Snapshot")
	FReditusPlayerRosterSnapshot GetRosterSnapshot() const;

	/**
	 * Validates and applies a snapshot.
	 *
	 * This gives the existing BP_JRPGPlayerController and future SaveGame code
	 * one clean handoff point.
	 */
	UFUNCTION(BlueprintCallable, Category = "Reditus|Player Roster|Snapshot")
	bool ApplyRosterSnapshot(
		const FReditusPlayerRosterSnapshot& Snapshot
	);

	// ---------------------------------------------------------------------
	// EVENTS
	// ---------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "Reditus|Player Roster|Events")
	FReditusRosterChangedSignature OnRosterChanged;

	UPROPERTY(BlueprintAssignable, Category = "Reditus|Player Roster|Events")
	FReditusPartyChangedSignature OnPartyChanged;

	UPROPERTY(BlueprintAssignable, Category = "Reditus|Player Roster|Events")
	FReditusFormationChangedSignature OnFormationChanged;

private:

	UPROPERTY(Transient)
	UDataTable* PlayerUnitDataTable = nullptr;

	UPROPERTY(Transient)
	int32 MaxPartySize = 4;

	UPROPERTY(Transient)
	TArray<FName> RecruitedUnitIDs;

	UPROPERTY(Transient)
	TArray<FName> PartyMemberIDs;

	UPROPERTY(Transient)
	TArray<FName> FormationUnitIDs;

	bool ValidatePlayerUnitID(FName PlayerUnitID) const;
	bool HasNoDuplicateIDs(const TArray<FName>& IDs) const;
	bool FormationMatchesParty(
		const TArray<FName>& InParty,
		const TArray<FName>& InFormation
	) const;
};
