#include "ReditusQuestCatalog.h"
#include "ReditusQuestSubsystem.h"
#include "ReditusQuestItemRouting.h"
#include "GameFramework/PlayerController.h"

namespace
{
    std::string StepKey(const FString& Value) { return std::string(TCHAR_TO_UTF8(*Value)); }
    std::string StepID(FName Value) { return Value.IsNone() ? std::string() : StepKey(Value.ToString().ToLower()); }
    FName SavedName(const std::string& Value) { return FName(UTF8_TO_TCHAR(Value.c_str())); }
}

bool UReditusQuestSubsystem::RecordQuestChoice(FDataTableRowHandle Quest, FName ChoiceID, FName OptionID)
{
    if (bImporting || ChoiceID.IsNone() || OptionID.IsNone() || Deliveries.Contains(Key(Quest)) || !Prepare(Quest)) return false;
    const bool Changed = Runtime.Choose(StepKey(Key(Quest)), StepID(ChoiceID), StepID(OptionID));
    FlushChanges(); return Changed;
}

bool UReditusQuestSubsystem::GetQuestChoice(FDataTableRowHandle Quest, FName ChoiceID, FName& OptionID, int32& Value) const
{
    OptionID = NAME_None; Value = 0;
    const auto* Record = Runtime.FindRecord(StepKey(Key(Quest)));
    if (!Record || ChoiceID.IsNone()) return false;
    const auto Found = Record->Choices.find(StepID(ChoiceID));
    if (Found == Record->Choices.end()) return false;
    OptionID = SavedName(Found->second.Option); Value = Found->second.Value; return true;
}

TArray<FReditusQuestSavedChoice> UReditusQuestSubsystem::GetQuestChoices(FDataTableRowHandle Quest) const
{
    TArray<FReditusQuestSavedChoice> Result;
    const auto* Record = Runtime.FindRecord(StepKey(Key(Quest))); if (!Record) return Result;
    for (const auto& Choice : Record->Choices)
    {
        FReditusQuestSavedChoice Saved; Saved.ChoiceID = SavedName(Choice.first);
        Saved.OptionID = SavedName(Choice.second.Option); Saved.Value = Choice.second.Value; Result.Add(Saved);
    }
    return Result;
}

bool UReditusQuestSubsystem::WasQuestStepActionApplied(FDataTableRowHandle Quest, FName ActionID) const
{
    const auto* Record = Runtime.FindRecord(StepKey(Key(Quest)));
    return Record && !ActionID.IsNone() && Record->Actions.count(StepID(ActionID)) != 0;
}

bool UReditusQuestSubsystem::BuildStepDelivery(FDataTableRowHandle Quest, FName ActionID, FReditusQuestDelivery& Delivery) const
{
    Delivery = FReditusQuestDelivery();
    const auto* Record = Runtime.FindRecord(StepKey(Key(Quest)));
    const auto* Row = ReditusQuestCatalog::Definition(Quest);
    if (!Record || !Row || ActionID.IsNone()
        || !Runtime.CanApplyAction(Record->Key, Record->StageId, StepID(ActionID))) return false;
    for (const auto& Stage : Row->Stages) if (StepID(Stage.StageID) == Record->StageId)
        for (const auto& Action : Stage.Actions) if (Action.ActionID == ActionID)
        {
            Delivery.Quest = Quest; Delivery.Kind = EReditusQuestDeliveryKind::StepAction;
            Delivery.StageID = Stage.StageID; Delivery.ActionID = Action.ActionID;
            Delivery.ItemsToRemove = Action.ItemsToRemove; Delivery.Rewards = Action.Rewards;
            if (!ReditusQuestItemRouting::NormalizeItems(Delivery.ItemsToRemove)
                || !ReditusQuestItemRouting::NormalizeItems(Delivery.Rewards.Items)) return false;
            if (!Action.GoldRewardChoiceID.IsNone())
            {
                FName Selected; int32 Amount = 0;
                if (!GetQuestChoice(Quest, Action.GoldRewardChoiceID, Selected, Amount) || Amount < 0) return false;
                Delivery.Rewards.Gold = Amount;
            }
            return true;
        }
    return false;
}

bool UReditusQuestSubsystem::RequestQuestStepAction(FDataTableRowHandle Quest, FName ActionID, APlayerController* Player)
{
    if (bImporting || Deliveries.Contains(Key(Quest)) || !Prepare(Quest)) return false;
    FReditusQuestDelivery Delivery;
    if (!BuildStepDelivery(Quest, ActionID, Delivery)) return false;
    for (const auto& Cost : Delivery.ItemsToRemove)
    {
        int32 Count = 0;
        if (!GetQuestItemCount(Player, Cost.ItemRow, Cost.Kind, Count) || Count < Cost.Amount) return false;
    }
    if (Delivery.ItemsToRemove.Num() == 0 && Delivery.Rewards.Items.Num() == 0
        && Delivery.Rewards.Gold == 0 && Delivery.Rewards.Experience == 0)
    {
        const bool Changed = Runtime.ApplyAction(StepKey(Key(Quest)), StepID(Delivery.StageID), StepID(ActionID));
        FlushChanges(); return Changed;
    }
    if (!IsValid(Player) || !OnQuestDeliveryRequested.IsBound())
    {
        UE_LOG(LogTemp, Warning, TEXT("Quest step needs the central OnQuestDeliveryRequested inventory/reward handler.")); return false;
    }
    Delivery.ReceiptID = FGuid::NewGuid(); Deliveries.Add(Key(Quest), Delivery);
    OnQuestDeliveryRequested.Broadcast(Delivery); return true;
}
