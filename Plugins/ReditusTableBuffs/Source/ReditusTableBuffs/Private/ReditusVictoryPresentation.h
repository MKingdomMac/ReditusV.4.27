#pragma once

#include "CoreMinimal.h"

class AActor;
class APlayerController;
class UPanelWidget;
class UTextBlock;
class UUserWidget;
class UWidget;
struct FReditusEncounterMastery;
struct FReditusResultsPresentation;

struct FReditusLevelUpChanges
{
    TMap<FName, int64> Stats;
    float ActionTime = 0.f;
};

namespace ReditusVictoryPresentation
{
    UWidget* Build(UUserWidget* Widget, UPanelWidget* Drops, int32 EXP, int32 Gold, const UTextBlock* FontSource,
        const FReditusEncounterMastery* Mastery);
    UWidget* BuildLevelUp(UUserWidget* Widget, const FReditusLevelUpChanges& Changes);
    bool UpdateTechMastery(UUserWidget* Widget, const UTextBlock* FontSource, const FText& Status);
    void Reveal(UUserWidget* Widget, UWidget* Panel, APlayerController* Player, AActor* StaticCamera,
        const TArray<AActor*>& Party, TSharedPtr<FReditusResultsPresentation>& Active);
    void Finish(TSharedPtr<FReditusResultsPresentation>& Active);
}
