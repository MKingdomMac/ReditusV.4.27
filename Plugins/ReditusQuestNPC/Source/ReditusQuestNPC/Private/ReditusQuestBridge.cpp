#include "ReditusQuestBridge.h"

// Preserve old Blueprint symbol references without reading or mutating legacy
// activeQuests, completedQuests, AddQuest, UpdateQuest or DeliverQuest.
bool UReditusQuestBridge::ReadQuest(APlayerController* Player, TSubclassOf<AActor> Quest, FReditusQuestSnapshot& Snapshot)
{
    (void)Player; (void)Quest;
    Snapshot = FReditusQuestSnapshot(); return false;
}

bool UReditusQuestBridge::ApplyQuestAction(APlayerController* Player, TSubclassOf<AActor> Quest,
    EReditusQuestAction Action, int32 Progress, bool ShowRewards)
{
    (void)Player; (void)Quest; (void)Action; (void)Progress; (void)ShowRewards;
    return false;
}
