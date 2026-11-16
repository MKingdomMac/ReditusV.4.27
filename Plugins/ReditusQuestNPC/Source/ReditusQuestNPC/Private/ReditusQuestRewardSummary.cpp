#include "ReditusQuestSubsystem.h"
#include "ReditusQuestRewardText.h"
#include "UObject/UnrealType.h"

namespace
{
    FText RewardItemName(FDataTableRowHandle Row)
    {
        if (Row.DataTable && Row.DataTable->GetRowStruct())
            if (uint8* const* Data = Row.DataTable->GetRowMap().Find(Row.RowName))
                for (TFieldIterator<FProperty> It(Row.DataTable->GetRowStruct()); It; ++It)
                {
                    // Blueprint structs can append GUIDs to the authored property name.
                    FString Name = It->GetName(); Name.ReplaceInline(TEXT(" "), TEXT(""));
                    if (!Name.Equals(TEXT("DisplayName"), ESearchCase::IgnoreCase)
                        && !Name.StartsWith(TEXT("DisplayName_"), ESearchCase::IgnoreCase)) continue;
                    if (auto* Field = CastField<FTextProperty>(*It))
                    {
                        const FText Value = Field->GetPropertyValue_InContainer(*Data);
                        if (!Value.IsEmpty()) return Value;
                    }
                    else if (auto* StringField = CastField<FStrProperty>(*It))
                    {
                        const FString Value = StringField->GetPropertyValue_InContainer(*Data);
                        if (!Value.IsEmpty()) return FText::FromString(Value);
                    }
                }
        // Some existing key-item rows have no Display Name. Never label them None.
        return FText::FromName(Row.RowName);
    }
}

FText UReditusQuestSubsystem::GetQuestRewardSummary(FDataTableRowHandle Quest)
{
    FReditusQuestSnapshot Snapshot;
    if (!ReadQuest(Quest, Snapshot)) return FText::GetEmpty();
    const auto* Row = Quest.DataTable->FindRow<FReditusQuestDefinition>(Quest.RowName, TEXT("Quest rewards"), false);
    if (!Row) return FText::GetEmpty();
    std::vector<int> Amounts;
    for (const auto& Reward : Row->Rewards.Items)
        Amounts.push_back(Reward.Amount);
    const auto Status = Snapshot.State == EReditusQuestState::Completed ? ReditusQuestRewardText::State::Received
        : Snapshot.State == EReditusQuestState::Failed ? ReditusQuestRewardText::State::NotReceived : ReditusQuestRewardText::State::Offered;
    const auto Plan = ReditusQuestRewardText::Build(Row->Rewards.Gold, Row->Rewards.Experience, Amounts, Status);
    FText Result = Plan.Entries.empty() ? NSLOCTEXT("ReditusQuests", "QuestRewards", "Quest Rewards")
        : Plan.Status == ReditusQuestRewardText::State::Received ? NSLOCTEXT("ReditusQuests", "RewardsReceived", "Rewards Received")
        : Plan.Status == ReditusQuestRewardText::State::NotReceived ? NSLOCTEXT("ReditusQuests", "RewardsNotReceived", "Quest Rewards (not received)")
        : NSLOCTEXT("ReditusQuests", "QuestRewards", "Quest Rewards");
    const auto Append = [](const FText& A, const FText& B)
    { return FText::Format(FText::FromString(TEXT("{0}\n{1}")), A, B); };
    if (Plan.Entries.empty()) return Append(Result, NSLOCTEXT("ReditusQuests", "NoQuestRewards", "None"));
    for (const auto& Entry : Plan.Entries)
    {
        FText Line;
        if (Entry.Type == ReditusQuestRewardText::Kind::Gold)
            Line = FText::Format(NSLOCTEXT("ReditusQuests", "GoldReward", "Gold: {0}"), FText::AsNumber(Entry.Amount));
        else if (Entry.Type == ReditusQuestRewardText::Kind::Experience)
            Line = FText::Format(NSLOCTEXT("ReditusQuests", "ExperienceReward", "Experience: {0}"), FText::AsNumber(Entry.Amount));
        else
        {
            FFormatNamedArguments Args;
            Args.Add(TEXT("Item"), RewardItemName(Row->Rewards.Items[int32(Entry.ItemIndex)].ItemRow));
            Args.Add(TEXT("Count"), FText::AsNumber(Entry.Amount));
            Line = FText::Format(NSLOCTEXT("ReditusQuests", "ItemReward", "{Item} x{Count}"), Args);
        }
        Result = Append(Result, Line);
    }
    return Result;
}
