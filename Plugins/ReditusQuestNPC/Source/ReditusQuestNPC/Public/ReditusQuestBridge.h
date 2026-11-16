#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ReditusQuestNPCTypes.h"
#include "ReditusQuestBridge.generated.h"

class APlayerController;

UCLASS()
class REDITUSQUESTNPC_API UReditusQuestBridge : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // Retired compatibility symbols only. They never access the old quest system.
    UFUNCTION(BlueprintCallable, Category="Reditus|Retired Quests", meta=(DeprecatedFunction, DeprecationMessage="Use Get Quest System and a DT_Quests row handle."))
    static bool ReadQuest(APlayerController* Player, TSubclassOf<AActor> Quest, FReditusQuestSnapshot& Snapshot);

    // Use ReditusQuestSubsystem and quest row handles instead.
    UFUNCTION(BlueprintCallable, Category="Reditus|Retired Quests", meta=(DeprecatedFunction, DeprecationMessage="Use Get Quest System and a DT_Quests row handle."))
    static bool ApplyQuestAction(APlayerController* Player, TSubclassOf<AActor> Quest,
                                 EReditusQuestAction Action, int32 Progress = 1, bool ShowRewards = true);
};
