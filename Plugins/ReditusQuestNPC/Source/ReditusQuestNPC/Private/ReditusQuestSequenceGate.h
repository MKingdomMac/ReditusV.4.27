#pragma once

#include "GameFramework/PlayerController.h"
#include "UObject/UnrealType.h"

namespace ReditusQuestSequenceGate
{
    // The exported Reditus controller uses this byte enum. Do not infer state
    // from localized display text, actor names, or a transient victory widget.
    inline bool IsExploring(APlayerController* Player)
    {
        if (!IsValid(Player)) return false;
        const auto* Field = FindFProperty<FByteProperty>(Player->GetClass(), TEXT("gameState"));
        if (!Field || !Field->Enum || Field->Enum->GetPathName() != TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Enums/E_GameState.E_GameState"))
            return true; // Other controllers keep their existing generic sequence behavior.
        return Field->GetPropertyValue_InContainer(Player) == 0; // Explore; other values include results' Battle state.
    }
}
