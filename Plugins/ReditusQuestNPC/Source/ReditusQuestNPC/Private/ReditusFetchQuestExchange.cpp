#include "ReditusFetchQuestExchange.h"
#include "ReditusQuestSubsystem.h"
#include "ReditusQuestItemRouting.h"
#include "GameFramework/PlayerController.h"
#include "UObject/UnrealType.h"

bool UReditusQuestSubsystem::TurnInFetchQuest(FDataTableRowHandle Quest, APlayerController* Player)
{
    if (bApplyingNPCAction || bAllowBuiltInFetchDelivery) return false;
    TGuardValue<bool> Allow(bAllowBuiltInFetchDelivery, true);
    return TurnInQuest(Quest, Player);
}

bool UReditusQuestSubsystem::ApplyBuiltInFetchDelivery(APlayerController* Player,
    const FReditusQuestDelivery& Delivery, FString& Error)
{
    const auto* Pending = Deliveries.Find(Key(Delivery.Quest));
    const FString QuestKey = Key(Delivery.Quest);
    const auto* Record = Runtime.FindRecord(std::string(TCHAR_TO_UTF8(*QuestKey)));
    if (!IsValid(Player) || !Player->IsLocalController() || bImporting || bApplyingNPCAction
        || !Pending || Pending->ReceiptID != Delivery.ReceiptID || Delivery.Kind != EReditusQuestDeliveryKind::TurnIn
        || !Record || Record->Status != ReditusQuestRuntime::State::Ready)
    { Error = TEXT("The fetch hand-in is no longer valid."); return false; }
    if (Delivery.Rewards.Experience != 0)
    { Error = TEXT("Experience rewards require the central OnQuestDeliveryRequested handler. Built-in fetch delivery supports gold and items."); return false; }

    struct Change
    {
        FMapProperty* Map = nullptr;
        FName Name;
        int32 Before = 0;
        bool Existed = false;
    };
    TArray<Change> Changes;
    std::vector<ReditusFetchQuestExchange::Item> Balances;
    auto AddItems = [&](const TArray<FReditusQuestItemAmount>& Items, bool Cost) -> bool
    {
        for (const auto& Item : Items)
        {
            EReditusQuestItemKind Kind;
            if (Item.Amount <= 0 || !ReditusQuestItemRouting::ResolveKind(Item.ItemRow, Kind)) return false;
            const FName PropertyName = Kind == EReditusQuestItemKind::Usable ? FName(TEXT("usableItems"))
                : Kind == EReditusQuestItemKind::Misc ? FName(TEXT("miscItems"))
                : Kind == EReditusQuestItemKind::KeyItem ? FName(TEXT("KeyItems")) : FName(TEXT("equipment"));
            auto* Map = FindFProperty<FMapProperty>(Player->GetClass(), PropertyName);
            auto* NameProperty = Map ? CastField<FNameProperty>(Map->KeyProp) : nullptr;
            auto* CountProperty = Map ? CastField<FIntProperty>(Map->ValueProp) : nullptr;
            if (!Map || !NameProperty || !CountProperty) return false;
            int32 Index = INDEX_NONE;
            for (int32 I = 0; I < Changes.Num(); ++I)
                if (Changes[I].Map == Map && Changes[I].Name == Item.ItemRow.RowName) { Index = I; break; }
            if (Index == INDEX_NONE)
            {
                Change Value; Value.Map = Map; Value.Name = Item.ItemRow.RowName;
                FScriptMapHelper Helper(Map, Map->ContainerPtrToValuePtr<void>(Player));
                for (int32 I = 0; I < Helper.GetMaxIndex(); ++I)
                    if (Helper.IsValidIndex(I) && NameProperty->GetPropertyValue(Helper.GetKeyPtr(I)) == Value.Name)
                    { Value.Existed = true; Value.Before = CountProperty->GetPropertyValue(Helper.GetValuePtr(I)); break; }
                Index = Changes.Add(Value);
                ReditusFetchQuestExchange::Item Balance; Balance.Owned = Value.Before; Balances.push_back(Balance);
            }
            auto& Balance = Balances[size_t(Index)];
            if (Cost) Balance.Cost += Item.Amount; else Balance.Reward += Item.Amount;
        }
        return true;
    };
    if (!AddItems(Delivery.ItemsToRemove, true) || !AddItems(Delivery.Rewards.Items, false))
    { Error = TEXT("A fetch item row or its inventory map is missing or has the wrong type."); return false; }
    FIntProperty* GoldProperty = nullptr;
    int32 GoldBefore = 0;
    if (Delivery.Rewards.Gold != 0)
    {
        GoldProperty = FindFProperty<FIntProperty>(Player->GetClass(), TEXT("gold"));
        if (!GoldProperty) { Error = TEXT("The player's gold integer is missing."); return false; }
        GoldBefore = GoldProperty->GetPropertyValue_InContainer(Player);
    }
    std::vector<std::int32_t> After; std::int32_t GoldAfter = 0;
    if (!ReditusFetchQuestExchange::Plan(Balances, GoldBefore, Delivery.Rewards.Gold, After, GoldAfter))
    { Error = TEXT("Items are missing, an inventory balance is invalid, or a reward would exceed the inventory limit."); return false; }

    TGuardValue<bool> Applying(bApplyingNPCAction, true);
    for (int32 I = 0; I < Changes.Num(); ++I)
    {
        const auto& ChangeValue = Changes[I];
        FScriptMapHelper Helper(ChangeValue.Map, ChangeValue.Map->ContainerPtrToValuePtr<void>(Player));
        const int32 Amount = After[size_t(I)];
        if (Amount == 0) Helper.RemovePair(&ChangeValue.Name); else Helper.AddPair(&ChangeValue.Name, &Amount);
    }
    if (GoldProperty) GoldProperty->SetPropertyValue_InContainer(Player, GoldAfter);
    if (ConfirmQuestDelivery(Delivery.Quest, Delivery.ReceiptID)) return true;

    // Restore exact map membership, including an originally present zero entry.
    for (const auto& ChangeValue : Changes)
    {
        FScriptMapHelper Helper(ChangeValue.Map, ChangeValue.Map->ContainerPtrToValuePtr<void>(Player));
        if (ChangeValue.Existed) Helper.AddPair(&ChangeValue.Name, &ChangeValue.Before); else Helper.RemovePair(&ChangeValue.Name);
    }
    if (GoldProperty) GoldProperty->SetPropertyValue_InContainer(Player, GoldBefore);
    Error = TEXT("Fetch receipt confirmation failed; inventory was restored."); return false;
}
