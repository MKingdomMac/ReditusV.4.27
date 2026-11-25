#pragma once

#include "CoreMinimal.h"
#include "UObject/UnrealType.h"

class AActor;
class UActorComponent;

namespace ReditusTableBuffQueries
{
    UObject* ReadObject(const UObject* Owner, const TCHAR* Name);
    bool ReadBool(const UObject* Owner, const TCHAR* Name);
    int32 ReadInt(const UObject* Owner, const TCHAR* Name);
    bool AliveInBattle(const AActor* Unit, const AActor* Battle);
    UActorComponent* FindClock(AActor* Battle);
    bool UnitParameter(const FObjectPropertyBase* Prop, const AActor* Unit);
    bool MatchesCatalogueSide(const AActor* Unit, FName Side);
    int32 FindAggroFocus(AActor* Battle, AActor*& OutTarget);

    template<typename T> const T* InputProperty(const UStruct* Owner, const TCHAR* Name)
    {
        const auto* Prop = FindFProperty<T>(Owner, FName(Name));
        return Prop && Prop->HasAnyPropertyFlags(CPF_Parm) && !Prop->HasAnyPropertyFlags(CPF_ReturnParm)
            ? Prop : nullptr;
    }
}
