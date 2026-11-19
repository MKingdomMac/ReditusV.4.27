#include "ReditusQuestCatalog.h"

namespace ReditusQuestCatalog
{
    FDataTableRowHandle Canonical(FDataTableRowHandle Reference)
    {
        if (!Reference.DataTable || Reference.RowName.IsNone()) return FDataTableRowHandle();
        if (Reference.DataTable->GetRowStruct() == FReditusQuestDefinition::StaticStruct())
            return Reference.DataTable->FindRow<FReditusQuestDefinition>(Reference.RowName, TEXT("Quest definition"), false)
                ? Reference : FDataTableRowHandle();
        if (Reference.DataTable->GetRowStruct() != FReditusQuestCatalogRow::StaticStruct()) return FDataTableRowHandle();
        const auto* Row = Reference.DataTable->FindRow<FReditusQuestCatalogRow>(Reference.RowName, TEXT("Quest catalogue"), false);
        if (!Row) return FDataTableRowHandle();
        if (Row->Kind == EReditusQuestCatalogKind::SimpleRow)
        {
            const auto Simple = Row->SimpleQuest;
            // No alias chains: one catalogue entry points directly to an existing simple row.
            if (Row->GraphQuest || !Simple.DataTable || Simple.RowName.IsNone()
                || Simple.DataTable->GetRowStruct() != FReditusQuestDefinition::StaticStruct()) return FDataTableRowHandle();
            return Simple.DataTable->FindRow<FReditusQuestDefinition>(Simple.RowName, TEXT("Simple quest"), false)
                ? Simple : FDataTableRowHandle();
        }
        const auto* Asset = Row->GraphQuest;
        if (Row->Kind != EReditusQuestCatalogKind::Graph || !Asset || !Asset->Compiled
            || Row->SimpleQuest.DataTable || !Row->SimpleQuest.RowName.IsNone()
            || Asset->QuestID != Reference.RowName || !(Asset->RegisteredQuest == Reference)) return FDataTableRowHandle();
        return Reference;
    }

    const FReditusQuestDefinition* Definition(FDataTableRowHandle Reference)
    {
        const auto Quest = Canonical(Reference);
        if (!Quest.DataTable) return nullptr;
        if (Quest.DataTable->GetRowStruct() == FReditusQuestDefinition::StaticStruct())
            return Quest.DataTable->FindRow<FReditusQuestDefinition>(Quest.RowName, TEXT("Quest definition"), false);
        const auto* Row = Quest.DataTable->FindRow<FReditusQuestCatalogRow>(Quest.RowName, TEXT("Quest catalogue"), false);
        return Row && Row->GraphQuest ? &Row->GraphQuest->CompiledDefinition : nullptr;
    }

    UDialogue* Conversation(FDataTableRowHandle Reference, EReditusQuestState State, FName StageID)
    {
        const auto Quest = Canonical(Reference);
        if (!Quest.DataTable || Quest.DataTable->GetRowStruct() != FReditusQuestCatalogRow::StaticStruct()) return nullptr;
        const auto* Row = Quest.DataTable->FindRow<FReditusQuestCatalogRow>(Quest.RowName, TEXT("Quest conversation"), false);
        const auto* Asset = Row ? Row->GraphQuest : nullptr;
        if (!Asset) return nullptr;
        switch (State)
        {
        case EReditusQuestState::Available: return Asset->AvailableDialogue;
        case EReditusQuestState::ReadyToDeliver: return Asset->ReadyToDeliverDialogue;
        case EReditusQuestState::Completed: return Asset->CompletedDialogue;
        case EReditusQuestState::Failed: return Asset->FailedDialogue;
        case EReditusQuestState::Unavailable: return Asset->UnavailableDialogue;
        case EReditusQuestState::Active:
            for (const auto& Entry : Asset->StageConversations) if (Entry.StageID == StageID) return Entry.Dialogue;
            return nullptr;
        default: return nullptr;
        }
    }
}

bool UReditusQuestCatalogLibrary::FindQuestInCatalog(UDataTable* Catalog, FName QuestID, FDataTableRowHandle& Quest)
{
    Quest = FDataTableRowHandle();
    if (!Catalog || Catalog->GetRowStruct() != FReditusQuestCatalogRow::StaticStruct()) return false;
    FDataTableRowHandle Reference; Reference.DataTable = Catalog; Reference.RowName = QuestID;
    Quest = ReditusQuestCatalog::Canonical(Reference);
    return Quest.DataTable != nullptr;
}
