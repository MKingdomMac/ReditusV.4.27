#include "ReditusPlayerRosterSubsystem.h"

void UReditusPlayerRosterSubsystem::ConfigurePlayerRoster(
	UDataTable* InPlayerUnitDataTable,
	int32 InMaxPartySize)
{
	PlayerUnitDataTable = InPlayerUnitDataTable;
	MaxPartySize = FMath::Max(1, InMaxPartySize);
}

bool UReditusPlayerRosterSubsystem::GetPlayerUnitDefinition(
	FName PlayerUnitID,
	FReditusPlayerUnitDefinition& OutDefinition) const
{
	if (!PlayerUnitDataTable || PlayerUnitID.IsNone())
	{
		return false;
	}

	static const FString ContextString =
		TEXT("UReditusPlayerRosterSubsystem::GetPlayerUnitDefinition");

	if (const FReditusPlayerUnitDefinition* Row =
		PlayerUnitDataTable->FindRow<FReditusPlayerUnitDefinition>(
			PlayerUnitID,
			ContextString,
			false))
	{
		OutDefinition = *Row;
		return true;
	}

	return false;
}

bool UReditusPlayerRosterSubsystem::DoesPlayerUnitDefinitionExist(
	FName PlayerUnitID) const
{
	return ValidatePlayerUnitID(PlayerUnitID);
}

bool UReditusPlayerRosterSubsystem::ResolvePlayerUnitIDFromLegacyBattleClass(
	TSubclassOf<AActor> LegacyBattleClass,
	FName& OutPlayerUnitID) const
{
	OutPlayerUnitID = NAME_None;

	if (!PlayerUnitDataTable || !LegacyBattleClass)
	{
		return false;
	}

	static const FString ContextString =
		TEXT("UReditusPlayerRosterSubsystem::ResolvePlayerUnitIDFromLegacyBattleClass");

	const TArray<FName> RowNames = PlayerUnitDataTable->GetRowNames();

	for (const FName& RowName : RowNames)
	{
		const FReditusPlayerUnitDefinition* Row =
			PlayerUnitDataTable->FindRow<FReditusPlayerUnitDefinition>(
				RowName,
				ContextString,
				false);

		if (!Row || Row->LegacyBattleUnitClass.IsNull())
		{
			continue;
		}

		UClass* DefinitionClass = Row->LegacyBattleUnitClass.LoadSynchronous();

		if (DefinitionClass == LegacyBattleClass.Get())
		{
			OutPlayerUnitID = RowName;
			return true;
		}
	}

	return false;
}

bool UReditusPlayerRosterSubsystem::GetLegacyBattleUnitClass(
	FName PlayerUnitID,
	TSubclassOf<AActor>& OutLegacyBattleClass) const
{
	OutLegacyBattleClass = nullptr;

	FReditusPlayerUnitDefinition Definition;

	if (!GetPlayerUnitDefinition(PlayerUnitID, Definition) ||
		Definition.LegacyBattleUnitClass.IsNull())
	{
		return false;
	}

	UClass* LoadedClass = Definition.LegacyBattleUnitClass.LoadSynchronous();

	if (!LoadedClass)
	{
		return false;
	}

	OutLegacyBattleClass = LoadedClass;
	return true;
}

bool UReditusPlayerRosterSubsystem::GetLegacyExplorationCharacterClass(
	FName PlayerUnitID,
	TSubclassOf<AActor>& OutLegacyCharacterClass) const
{
	OutLegacyCharacterClass = nullptr;

	FReditusPlayerUnitDefinition Definition;

	if (!GetPlayerUnitDefinition(PlayerUnitID, Definition) ||
		Definition.LegacyExplorationCharacterClass.IsNull())
	{
		return false;
	}

	UClass* LoadedClass =
		Definition.LegacyExplorationCharacterClass.LoadSynchronous();

	if (!LoadedClass)
	{
		return false;
	}

	OutLegacyCharacterClass = LoadedClass;
	return true;
}

bool UReditusPlayerRosterSubsystem::RecruitPlayerUnit(
	FName PlayerUnitID,
	bool bAddToPartyIfSpace)
{
	if (!ValidatePlayerUnitID(PlayerUnitID))
	{
		return false;
	}

	const bool bAlreadyRecruited =
		RecruitedUnitIDs.Contains(PlayerUnitID);

	if (!bAlreadyRecruited)
	{
		RecruitedUnitIDs.Add(PlayerUnitID);
		OnRosterChanged.Broadcast();
	}

	if (bAddToPartyIfSpace &&
		!PartyMemberIDs.Contains(PlayerUnitID) &&
		PartyMemberIDs.Num() < MaxPartySize)
	{
		AddPlayerUnitToParty(PlayerUnitID);
	}

	return !bAlreadyRecruited;
}

bool UReditusPlayerRosterSubsystem::RemovePlayerUnitFromRoster(
	FName PlayerUnitID)
{
	const bool bWasRecruited =
		RecruitedUnitIDs.Remove(PlayerUnitID) > 0;

	if (!bWasRecruited)
	{
		return false;
	}

	const bool bWasInParty =
		PartyMemberIDs.Remove(PlayerUnitID) > 0;

	const bool bWasInFormation =
		FormationUnitIDs.Remove(PlayerUnitID) > 0;

	OnRosterChanged.Broadcast();

	if (bWasInParty)
	{
		OnPartyChanged.Broadcast();
	}

	if (bWasInFormation)
	{
		OnFormationChanged.Broadcast();
	}

	return true;
}

bool UReditusPlayerRosterSubsystem::IsPlayerUnitRecruited(
	FName PlayerUnitID) const
{
	return RecruitedUnitIDs.Contains(PlayerUnitID);
}

bool UReditusPlayerRosterSubsystem::AddPlayerUnitToParty(
	FName PlayerUnitID)
{
	if (!RecruitedUnitIDs.Contains(PlayerUnitID))
	{
		return false;
	}

	if (PartyMemberIDs.Contains(PlayerUnitID))
	{
		return false;
	}

	if (PartyMemberIDs.Num() >= MaxPartySize)
	{
		return false;
	}

	PartyMemberIDs.Add(PlayerUnitID);

	if (!FormationUnitIDs.Contains(PlayerUnitID))
	{
		FormationUnitIDs.Add(PlayerUnitID);
	}

	OnPartyChanged.Broadcast();
	OnFormationChanged.Broadcast();

	return true;
}

bool UReditusPlayerRosterSubsystem::RemovePlayerUnitFromParty(
	FName PlayerUnitID)
{
	const bool bRemovedFromParty =
		PartyMemberIDs.Remove(PlayerUnitID) > 0;

	if (!bRemovedFromParty)
	{
		return false;
	}

	FormationUnitIDs.Remove(PlayerUnitID);

	OnPartyChanged.Broadcast();
	OnFormationChanged.Broadcast();

	return true;
}

bool UReditusPlayerRosterSubsystem::IsPlayerUnitInParty(
	FName PlayerUnitID) const
{
	return PartyMemberIDs.Contains(PlayerUnitID);
}

bool UReditusPlayerRosterSubsystem::SwapFormationMembers(
	FName PlayerUnitA,
	FName PlayerUnitB)
{
	if (PlayerUnitA == PlayerUnitB)
	{
		return false;
	}

	const int32 IndexA = FormationUnitIDs.IndexOfByKey(PlayerUnitA);
	const int32 IndexB = FormationUnitIDs.IndexOfByKey(PlayerUnitB);

	if (IndexA == INDEX_NONE || IndexB == INDEX_NONE)
	{
		return false;
	}

	FormationUnitIDs.Swap(IndexA, IndexB);
	OnFormationChanged.Broadcast();

	return true;
}

bool UReditusPlayerRosterSubsystem::MoveFormationMember(
	FName PlayerUnitID,
	int32 NewIndex)
{
	const int32 OldIndex =
		FormationUnitIDs.IndexOfByKey(PlayerUnitID);

	if (OldIndex == INDEX_NONE ||
		FormationUnitIDs.Num() <= 1)
	{
		return false;
	}

	const int32 ClampedIndex =
		FMath::Clamp(NewIndex, 0, FormationUnitIDs.Num() - 1);

	if (OldIndex == ClampedIndex)
	{
		return false;
	}

	FormationUnitIDs.RemoveAt(OldIndex);

	// If removing an earlier element shifted the destination left,
	// insert at the corrected final position.
	const int32 CorrectedIndex =
		(OldIndex < ClampedIndex)
		? ClampedIndex - 1
		: ClampedIndex;

	FormationUnitIDs.Insert(
		PlayerUnitID,
		FMath::Clamp(CorrectedIndex, 0, FormationUnitIDs.Num())
	);

	OnFormationChanged.Broadcast();
	return true;
}

bool UReditusPlayerRosterSubsystem::SetFormationOrder(
	const TArray<FName>& NewFormationOrder)
{
	if (!FormationMatchesParty(
		PartyMemberIDs,
		NewFormationOrder))
	{
		return false;
	}

	if (FormationUnitIDs == NewFormationOrder)
	{
		return false;
	}

	FormationUnitIDs = NewFormationOrder;
	OnFormationChanged.Broadcast();

	return true;
}

void UReditusPlayerRosterSubsystem::ResetFormationToPartyOrder()
{
	if (FormationUnitIDs == PartyMemberIDs)
	{
		return;
	}

	FormationUnitIDs = PartyMemberIDs;
	OnFormationChanged.Broadcast();
}

FReditusPlayerRosterSnapshot
UReditusPlayerRosterSubsystem::GetRosterSnapshot() const
{
	FReditusPlayerRosterSnapshot Snapshot;
	Snapshot.RecruitedUnitIDs = RecruitedUnitIDs;
	Snapshot.PartyMemberIDs = PartyMemberIDs;
	Snapshot.FormationUnitIDs = FormationUnitIDs;
	return Snapshot;
}

bool UReditusPlayerRosterSubsystem::ApplyRosterSnapshot(
	const FReditusPlayerRosterSnapshot& Snapshot)
{
	if (!HasNoDuplicateIDs(Snapshot.RecruitedUnitIDs) ||
		!HasNoDuplicateIDs(Snapshot.PartyMemberIDs) ||
		!HasNoDuplicateIDs(Snapshot.FormationUnitIDs))
	{
		return false;
	}

	for (const FName& UnitID : Snapshot.RecruitedUnitIDs)
	{
		if (!ValidatePlayerUnitID(UnitID))
		{
			return false;
		}
	}

	if (Snapshot.PartyMemberIDs.Num() > MaxPartySize)
	{
		return false;
	}

	for (const FName& UnitID : Snapshot.PartyMemberIDs)
	{
		if (!Snapshot.RecruitedUnitIDs.Contains(UnitID))
		{
			return false;
		}
	}

	if (!FormationMatchesParty(
		Snapshot.PartyMemberIDs,
		Snapshot.FormationUnitIDs))
	{
		return false;
	}

	RecruitedUnitIDs = Snapshot.RecruitedUnitIDs;
	PartyMemberIDs = Snapshot.PartyMemberIDs;
	FormationUnitIDs = Snapshot.FormationUnitIDs;

	OnRosterChanged.Broadcast();
	OnPartyChanged.Broadcast();
	OnFormationChanged.Broadcast();

	return true;
}

bool UReditusPlayerRosterSubsystem::ValidatePlayerUnitID(
	FName PlayerUnitID) const
{
	if (!PlayerUnitDataTable || PlayerUnitID.IsNone())
	{
		return false;
	}

	static const FString ContextString =
		TEXT("UReditusPlayerRosterSubsystem::ValidatePlayerUnitID");

	return PlayerUnitDataTable->FindRow<FReditusPlayerUnitDefinition>(
		PlayerUnitID,
		ContextString,
		false) != nullptr;
}

bool UReditusPlayerRosterSubsystem::HasNoDuplicateIDs(
	const TArray<FName>& IDs) const
{
	TSet<FName> UniqueIDs;

	for (const FName& ID : IDs)
	{
		if (ID.IsNone() || UniqueIDs.Contains(ID))
		{
			return false;
		}

		UniqueIDs.Add(ID);
	}

	return true;
}

bool UReditusPlayerRosterSubsystem::FormationMatchesParty(
	const TArray<FName>& InParty,
	const TArray<FName>& InFormation) const
{
	if (InParty.Num() != InFormation.Num())
	{
		return false;
	}

	if (!HasNoDuplicateIDs(InParty) ||
		!HasNoDuplicateIDs(InFormation))
	{
		return false;
	}

	for (const FName& PartyUnitID : InParty)
	{
		if (!InFormation.Contains(PartyUnitID))
		{
			return false;
		}
	}

	return true;
}
