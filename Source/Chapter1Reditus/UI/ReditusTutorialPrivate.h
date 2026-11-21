#pragma once

#include "ReditusMovementPrompt.h"

class FBoolProperty;

namespace ReditusMovementPrompt
{
    USaveGame* FindStorySave(const UWorld& World);
    const FBoolProperty* FindTutorialCompletionFlag(const USaveGame* SaveGame, EReditusTutorialLesson Lesson);
    UReditusBattleHUDWidget* FindLiveBattleHUD(const AActor& BattleController, const APlayerController& Player);
    FText BindingCaption(const FKey& Key);
    FKey ConfirmKey(const UReditusBattleHUDWidget& HUD, bool bGamepad);
}
