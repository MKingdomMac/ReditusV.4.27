#include "ReditusQuestAsset.h"

#if WITH_EDITOR
void UReditusQuestAsset::InvalidateCompilation()
{
    Compiled = false;
    MarkPackageDirty();
}

void UReditusQuestAsset::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
    Super::PostEditChangeProperty(Event);
    InvalidateCompilation();
}

void UReditusQuestAsset::PostEditUndo()
{
    Super::PostEditUndo();
    InvalidateCompilation();
}

void UReditusQuestAsset::PostDuplicate(bool bDuplicateForPIE)
{
    Super::PostDuplicate(bDuplicateForPIE);
    if (!bDuplicateForPIE)
    {
        QuestID = GetFName();
        RegisteredQuest = FDataTableRowHandle();
        InvalidateCompilation();
    }
}
#endif
