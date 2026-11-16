#pragma once
#include "CoreMinimal.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

// The actual Blueprint facade in StoryIntegration.json owns character stats,
// equipment and exploration refresh. Do not bypass it with a second roster.
namespace ReditusQuestPartyFacade
{
    inline bool Names(UObject* Player, FName PropertyName, TArray<FName>& Values)
    {
        Values.Reset();
        auto* Array = IsValid(Player) ? FindFProperty<FArrayProperty>(Player->GetClass(), PropertyName) : nullptr;
        auto* Name = Array ? CastField<FNameProperty>(Array->Inner) : nullptr;
        if (!Name) return false;
        FScriptArrayHelper Helper(Array, Array->ContainerPtrToValuePtr<void>(Player));
        for (int32 I = 0; I < Helper.Num(); ++I) Values.Add(Name->GetPropertyValue(Helper.GetRawPtr(I)));
        return true;
    }
    inline bool ExactInputs(UFunction* Function, bool HasLevel)
    {
        if (!Function) return false;
        int32 Count = 0;
        for (TFieldIterator<FProperty> It(Function); It; ++It)
            if (It->HasAnyPropertyFlags(CPF_Parm))
            {
                ++Count;
                if (It->ArrayDim != 1 || It->HasAnyPropertyFlags(CPF_OutParm | CPF_ReturnParm)) return false;
                if (It->GetFName() == TEXT("CharacterID")) { if (!CastField<FNameProperty>(*It)) return false; }
                else if (HasLevel && It->GetFName() == TEXT("StartingLevel")) { if (!CastField<FIntProperty>(*It)) return false; }
                else return false;
            }
        return Count == (HasLevel ? 2 : 1);
    }
    inline bool Owned(UObject* Player, FName CharacterID)
    {
        TArray<FName> Roster;
        return !CharacterID.IsNone() && Names(Player, TEXT("UnlockedPartyCharacterIDs"), Roster) && Roster.Contains(CharacterID);
    }
    inline bool Recruit(UObject* Player, FName CharacterID, int32 Level, bool AddToParty, int32 PartyLimit)
    {
        TArray<FName> Roster, Party;
        if (!IsValid(Player) || CharacterID.IsNone() || Level < 1 || PartyLimit < 1
            || !Names(Player, TEXT("UnlockedPartyCharacterIDs"), Roster)
            || !Names(Player, TEXT("ActivePartyCharacterIDs"), Party) || Roster.Contains(CharacterID)) return false;
        auto* Unlock = Player->FindFunction(TEXT("UnlockCharacterAtLevel"));
        auto* Add = Player->FindFunction(TEXT("AddCharacterIDToActiveParty"));
        if (!ExactInputs(Unlock, true) || (AddToParty && !ExactInputs(Add, false))) return false;
        FStructOnScope Parameters(Unlock);
        FindFProperty<FNameProperty>(Unlock, TEXT("CharacterID"))->SetPropertyValue_InContainer(Parameters.GetStructMemory(), CharacterID);
        FindFProperty<FIntProperty>(Unlock, TEXT("StartingLevel"))->SetPropertyValue_InContainer(Parameters.GetStructMemory(), Level);
        Player->ProcessEvent(Unlock, Parameters.GetStructMemory());
        if (!Owned(Player, CharacterID)) return false;
        // Refresh after the facade: existing listeners may already have joined it.
        if (AddToParty && Names(Player, TEXT("ActivePartyCharacterIDs"), Party)
            && !Party.Contains(CharacterID) && Party.Num() < PartyLimit)
        {
            FStructOnScope AddParameters(Add);
            FindFProperty<FNameProperty>(Add, TEXT("CharacterID"))->SetPropertyValue_InContainer(AddParameters.GetStructMemory(), CharacterID);
            Player->ProcessEvent(Add, AddParameters.GetStructMemory());
        }
        return true; // Full active party: the new unit remains available in the roster.
    }
}
