#pragma once

#include "ReditusQuestSystemTypes.h"

namespace ReditusQuestItemRouting
{
    // These are the actual catalogs backing the game's four inventory maps.
    // Never infer from a row name or from an editable/cached category field.
    inline bool ResolveKind(FDataTableRowHandle Row, EReditusQuestItemKind& Kind)
    {
        Kind = EReditusQuestItemKind::Misc;
        if (!Row.DataTable || Row.RowName.IsNone() || !Row.DataTable->GetRowMap().Contains(Row.RowName)) return false;
        const FName Path(*Row.DataTable->GetPathName());
        struct Catalog { const TCHAR* AssetPath; EReditusQuestItemKind InventoryKind; };
        const Catalog Catalogs[] = {
            {TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_UsableItems.DT_UsableItems"), EReditusQuestItemKind::Usable},
            {TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_MiscItems.DT_MiscItems"), EReditusQuestItemKind::Misc},
            {TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_KeyItems.DT_KeyItems"), EReditusQuestItemKind::KeyItem},
            {TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_Equipment.DT_Equipment"), EReditusQuestItemKind::Equipment}
        };
        for (const auto& CatalogEntry : Catalogs)
            if (Path == FName(CatalogEntry.AssetPath)) { Kind = CatalogEntry.InventoryKind; return true; }
        return false; // An unrelated table cannot borrow an inventory item's row name.
    }

    inline bool NormalizeItems(TArray<FReditusQuestItemAmount>& Items)
    {
        // Validate the whole payload before changing its cached delivery fields.
        for (const auto& Item : Items)
        {
            EReditusQuestItemKind Kind;
            if (!ResolveKind(Item.ItemRow, Kind) || Item.Amount <= 0) return false;
        }
        for (auto& Item : Items) ResolveKind(Item.ItemRow, Item.Kind);
        return true;
    }
}
