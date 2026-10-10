#pragma once

#include "CoreMinimal.h"
#include "ReditusPlayerRosterTypes.generated.h"

/**
 * Save/migration-friendly snapshot of the PLAYER MEMBERSHIP state.
 *
 * This does NOT yet replace S_PlayerUnitData.
 * EXP / HP / MP / equipment / status state remain in the existing Blueprint
 * data structure until we migrate that separately.
 */
USTRUCT(BlueprintType)
struct CHAPTER1REDITUS_API FReditusPlayerRosterSnapshot
{
	GENERATED_BODY()

public:

	/** Every character currently recruited/owned by the player. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roster")
	TArray<FName> RecruitedUnitIDs;

	/**
	 * Stable active-party order.
	 *
	 * This is intentionally separate from FormationUnitIDs.
	 * Reordering the formation must NOT reorder PartyMemberIDs.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roster")
	TArray<FName> PartyMemberIDs;

	/**
	 * Independent tactical/formation ordering of the active party.
	 *
	 * It must contain the same unit IDs as PartyMemberIDs, but may be in a
	 * different order.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roster")
	TArray<FName> FormationUnitIDs;
};
