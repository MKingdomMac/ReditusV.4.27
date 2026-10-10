#include "ReditusJRPGControllerBridgeComponent.h"

#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"

UReditusJRPGControllerBridgeComponent::
UReditusJRPGControllerBridgeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UReditusJRPGControllerBridgeComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!Cast<APlayerController>(GetOwner()))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("ReditusJRPGControllerBridgeComponent is intended for a PlayerController owner.")
		);
	}

	RefreshRosterConfiguration();
}

UReditusPlayerRosterSubsystem*
UReditusJRPGControllerBridgeComponent::GetPlayerRosterSubsystem() const
{
	if (!GetWorld())
	{
		return nullptr;
	}

	UGameInstance* GameInstance = GetWorld()->GetGameInstance();

	if (!GameInstance)
	{
		return nullptr;
	}

	return GameInstance->GetSubsystem<UReditusPlayerRosterSubsystem>();
}

void UReditusJRPGControllerBridgeComponent::RefreshRosterConfiguration()
{
	if (UReditusPlayerRosterSubsystem* Roster =
		GetPlayerRosterSubsystem())
	{
		Roster->ConfigurePlayerRoster(
			PlayerUnitDataTable,
			MaxPartySize
		);
	}
}

bool UReditusJRPGControllerBridgeComponent::RecruitPlayerUnitByID(
	FName PlayerUnitID,
	bool bAddToPartyIfSpace)
{
	if (UReditusPlayerRosterSubsystem* Roster =
		GetPlayerRosterSubsystem())
	{
		return Roster->RecruitPlayerUnit(
			PlayerUnitID,
			bAddToPartyIfSpace
		);
	}

	return false;
}

bool UReditusJRPGControllerBridgeComponent::AddPlayerUnitToPartyByID(
	FName PlayerUnitID)
{
	if (UReditusPlayerRosterSubsystem* Roster =
		GetPlayerRosterSubsystem())
	{
		return Roster->AddPlayerUnitToParty(PlayerUnitID);
	}

	return false;
}

bool UReditusJRPGControllerBridgeComponent::RemovePlayerUnitFromPartyByID(
	FName PlayerUnitID)
{
	if (UReditusPlayerRosterSubsystem* Roster =
		GetPlayerRosterSubsystem())
	{
		return Roster->RemovePlayerUnitFromParty(PlayerUnitID);
	}

	return false;
}

bool UReditusJRPGControllerBridgeComponent::SwapFormationMembersByID(
	FName PlayerUnitA,
	FName PlayerUnitB)
{
	if (UReditusPlayerRosterSubsystem* Roster =
		GetPlayerRosterSubsystem())
	{
		return Roster->SwapFormationMembers(
			PlayerUnitA,
			PlayerUnitB
		);
	}

	return false;
}

bool UReditusJRPGControllerBridgeComponent::RecruitLegacyPlayerUnitClass(
	TSubclassOf<AActor> LegacyPlayerUnitClass,
	bool bAddToPartyIfSpace,
	FName& OutPlayerUnitID)
{
	OutPlayerUnitID = NAME_None;

	UReditusPlayerRosterSubsystem* Roster =
		GetPlayerRosterSubsystem();

	if (!Roster ||
		!Roster->ResolvePlayerUnitIDFromLegacyBattleClass(
			LegacyPlayerUnitClass,
			OutPlayerUnitID))
	{
		return false;
	}

	return Roster->RecruitPlayerUnit(
		OutPlayerUnitID,
		bAddToPartyIfSpace
	);
}

bool UReditusJRPGControllerBridgeComponent::AddLegacyPlayerUnitToParty(
	TSubclassOf<AActor> LegacyPlayerUnitClass,
	FName& OutPlayerUnitID)
{
	OutPlayerUnitID = NAME_None;

	UReditusPlayerRosterSubsystem* Roster =
		GetPlayerRosterSubsystem();

	if (!Roster ||
		!Roster->ResolvePlayerUnitIDFromLegacyBattleClass(
			LegacyPlayerUnitClass,
			OutPlayerUnitID))
	{
		return false;
	}

	return Roster->AddPlayerUnitToParty(OutPlayerUnitID);
}

bool UReditusJRPGControllerBridgeComponent::RemoveLegacyPlayerUnitFromParty(
	TSubclassOf<AActor> LegacyPlayerUnitClass,
	FName& OutPlayerUnitID)
{
	OutPlayerUnitID = NAME_None;

	UReditusPlayerRosterSubsystem* Roster =
		GetPlayerRosterSubsystem();

	if (!Roster ||
		!Roster->ResolvePlayerUnitIDFromLegacyBattleClass(
			LegacyPlayerUnitClass,
			OutPlayerUnitID))
	{
		return false;
	}

	return Roster->RemovePlayerUnitFromParty(OutPlayerUnitID);
}

bool UReditusJRPGControllerBridgeComponent::SwapLegacyFormationMembers(
	TSubclassOf<AActor> LegacyPlayerUnitA,
	TSubclassOf<AActor> LegacyPlayerUnitB)
{
	UReditusPlayerRosterSubsystem* Roster =
		GetPlayerRosterSubsystem();

	if (!Roster)
	{
		return false;
	}

	FName UnitA = NAME_None;
	FName UnitB = NAME_None;

	if (!Roster->ResolvePlayerUnitIDFromLegacyBattleClass(
			LegacyPlayerUnitA,
			UnitA) ||
		!Roster->ResolvePlayerUnitIDFromLegacyBattleClass(
			LegacyPlayerUnitB,
			UnitB))
	{
		return false;
	}

	return Roster->SwapFormationMembers(UnitA, UnitB);
}

bool UReditusJRPGControllerBridgeComponent::
ResolvePlayerUnitIDFromLegacyClass(
	TSubclassOf<AActor> LegacyPlayerUnitClass,
	FName& OutPlayerUnitID) const
{
	if (UReditusPlayerRosterSubsystem* Roster =
		GetPlayerRosterSubsystem())
	{
		return Roster->ResolvePlayerUnitIDFromLegacyBattleClass(
			LegacyPlayerUnitClass,
			OutPlayerUnitID
		);
	}

	OutPlayerUnitID = NAME_None;
	return false;
}

bool UReditusJRPGControllerBridgeComponent::GetLegacyPlayerUnitClass(
	FName PlayerUnitID,
	TSubclassOf<AActor>& OutLegacyPlayerUnitClass) const
{
	if (UReditusPlayerRosterSubsystem* Roster =
		GetPlayerRosterSubsystem())
	{
		return Roster->GetLegacyBattleUnitClass(
			PlayerUnitID,
			OutLegacyPlayerUnitClass
		);
	}

	OutLegacyPlayerUnitClass = nullptr;
	return false;
}

bool UReditusJRPGControllerBridgeComponent::
ImportLegacyControllerMembership(
	const TArray<TSubclassOf<AActor>>& LegacyRecruitedClasses,
	const TArray<TSubclassOf<AActor>>& LegacyPartyMemberClasses,
	const TArray<TSubclassOf<AActor>>& LegacyFormationClasses)
{
	UReditusPlayerRosterSubsystem* Roster =
		GetPlayerRosterSubsystem();

	if (!Roster)
	{
		return false;
	}

	FReditusPlayerRosterSnapshot Snapshot;

	if (!ResolveLegacyClassArray(
			LegacyRecruitedClasses,
			Snapshot.RecruitedUnitIDs) ||
		!ResolveLegacyClassArray(
			LegacyPartyMemberClasses,
			Snapshot.PartyMemberIDs) ||
		!ResolveLegacyClassArray(
			LegacyFormationClasses,
			Snapshot.FormationUnitIDs))
	{
		return false;
	}

	return Roster->ApplyRosterSnapshot(Snapshot);
}

bool UReditusJRPGControllerBridgeComponent::ResolveLegacyClassArray(
	const TArray<TSubclassOf<AActor>>& LegacyClasses,
	TArray<FName>& OutPlayerUnitIDs) const
{
	OutPlayerUnitIDs.Reset();

	UReditusPlayerRosterSubsystem* Roster =
		GetPlayerRosterSubsystem();

	if (!Roster)
	{
		return false;
	}

	for (const TSubclassOf<AActor>& LegacyClass : LegacyClasses)
	{
		FName PlayerUnitID = NAME_None;

		if (!Roster->ResolvePlayerUnitIDFromLegacyBattleClass(
			LegacyClass,
			PlayerUnitID))
		{
			OutPlayerUnitIDs.Reset();
			return false;
		}

		if (OutPlayerUnitIDs.Contains(PlayerUnitID))
		{
			OutPlayerUnitIDs.Reset();
			return false;
		}

		OutPlayerUnitIDs.Add(PlayerUnitID);
	}

	return true;
}
