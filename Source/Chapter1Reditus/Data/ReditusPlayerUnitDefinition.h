#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameFramework/Actor.h"
#include "ReditusUnitStats.h"
#include "ReditusPlayerUnitDefinition.generated.h"

/**
 * Static definition for a playable Reditus character.
 *
 * IMPORTANT:
 * - The DataTable row name is the stable PlayerUnitID.
 *   Example: Augustus, Caesar, Macy.
 * - Being present in DT_PlayerUnits does NOT mean the character is recruited.
 * - Runtime/save state does NOT belong in this row.
 * - Legacy Blueprint classes remain here during migration so existing content
 *   can keep spawning/using the same Blueprint assets.
 */
USTRUCT(BlueprintType)
struct CHAPTER1REDITUS_API FReditusPlayerUnitDefinition : public FTableRowBase
{
	GENERATED_BODY()

public:

	// ---------------------------------------------------------------------
	// IDENTITY
	// ---------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity",
		meta = (MultiLine = "true"))
	FText Description;

	// ---------------------------------------------------------------------
	// LEGACY / PRESENTATION CLASSES
	// ---------------------------------------------------------------------

	/**
	 * Existing BP_PlayerUnitBase-derived battle class.
	 *
	 * This is intentionally a soft AActor class reference so the C++ data
	 * layer does not depend on a Blueprint-generated C++ type.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Legacy")
	TSoftClassPtr<AActor> LegacyBattleUnitClass;

	/**
	 * Existing exploration/follower character class, if the unit has one.
	 * This remains optional during the first migration phase.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Legacy")
	TSoftClassPtr<AActor> LegacyExplorationCharacterClass;

	// ---------------------------------------------------------------------
	// PROGRESSION
	// ---------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Progression")
	int32 MaxLevel = 99;

	/**
	 * These mirror the three stat blocks already used by the project.
	 * We are NOT changing the player's level formula in this phase.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Progression")
	FReditusUnitStats InitialStats;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Progression")
	FReditusUnitStats FirstLevelStats;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Progression")
	FReditusUnitStats LastLevelStats;

	// ---------------------------------------------------------------------
	// EXPLORATION
	// ---------------------------------------------------------------------

	/**
	 * Stable identifier for a character-specific exploration ability.
	 * The actual ability implementation can stay in Blueprint during migration.
	 *
	 * Example IDs:
	 * Engineer
	 * Hack
	 * BreakObstacle
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Exploration")
	FName ExplorationAbilityID = NAME_None;
};
