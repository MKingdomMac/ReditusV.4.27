#include "ReditusQuestCatalog.h"
#include "ReditusQuestRuntime.h"
#include "ReditusQuestItemRouting.h"

namespace ReditusQuestCatalog
{
    // Keep these helpers local to the catalogue when UBT combines source files.
    namespace
    {
        std::string Narrow(const FString& V) { return std::string(TCHAR_TO_UTF8(*V)); }
        std::string Identifier(FName V) { return Narrow(V.ToString().ToLower()); }
        FString Key(FDataTableRowHandle Row)
        {
            if (Row.DataTable && Row.DataTable->GetRowStruct() == FReditusQuestCatalogRow::StaticStruct())
                Row = ReditusQuestCatalog::Canonical(Row);
            return Row.DataTable && !Row.RowName.IsNone()
                ? (Row.DataTable->GetPathName() + TEXT(":") + Row.RowName.ToString()).ToLower() : FString();
        }
        bool ValidItemRow(FDataTableRowHandle Row)
        { EReditusQuestItemKind Kind; return ReditusQuestItemRouting::ResolveKind(Row, Kind); }
        bool ValidItems(const TArray<FReditusQuestItemAmount>& Items)
        {
            TSet<FString> IDs;
            for (const auto& Item : Items)
            {
                EReditusQuestItemKind Kind;
                if (!ReditusQuestItemRouting::ResolveKind(Item.ItemRow, Kind) || Item.Amount <= 0) return false;
                const FString InventoryID = (FString::FromInt(int32(Kind)) + TEXT(":") + Item.ItemRow.RowName.ToString()).ToLower();
                if (IDs.Contains(InventoryID)) return false;
                IDs.Add(InventoryID);
            }
            return true;
        }
    }
}

bool ReditusQuestCatalog::BuildDefinition(const FReditusQuestDefinition& Source, const FString& ID,
    ReditusQuestRuntime::Definition& D, FString& Error)
{
    Error.Reset();
    const auto* Row = &Source;
    if (Row->Rewards.Gold < 0 || Row->Rewards.Experience < 0
        || !ValidItems(Row->Rewards.Items) || !ValidItems(Row->TurnInItems))
    { Error = TEXT("Invalid reward/cost counts or duplicate item IDs: ") + ID; return false; }
    D = ReditusQuestRuntime::Definition();
    D.Key = Narrow(ID); D.Version = Row->DefinitionVersion;
    D.HasRewards = Row->Rewards.Gold > 0 || Row->Rewards.Experience > 0
        || Row->Rewards.Items.Num() > 0 || Row->TurnInItems.Num() > 0;
    for (const auto& Required : Row->RequiredCompletedQuests)
    {
        D.Prerequisites.push_back(Narrow(Key(Required)));
    }
    for (const auto& Stage : Row->Stages)
    {
        ReditusQuestRuntime::Stage S; S.Id = Identifier(Stage.StageID);
        S.Timer = static_cast<ReditusQuestRuntime::TimerMode>(Stage.Timer.Mode);
        S.Seconds = Stage.Timer.Mode == EReditusQuestTimerMode::None ? 0 : Stage.Timer.DurationSeconds;
        if (Stage.StageID.IsNone()) S.Id.clear();
        S.Next = Stage.NextStageID.IsNone() ? std::string() : Identifier(Stage.NextStageID);
        S.Finish = Stage.FinishAfterStage;
        for (const auto& Choice : Stage.Choices)
        {
            ReditusQuestRuntime::Choice C; C.Id = Choice.ChoiceID.IsNone() ? std::string() : Identifier(Choice.ChoiceID);
            for (const auto& Option : Choice.Options)
            {
                ReditusQuestRuntime::ChoiceOption O;
                O.Id = Option.OptionID.IsNone() ? std::string() : Identifier(Option.OptionID); O.Value = Option.Value;
                C.Options.push_back(O);
            }
            S.Choices.push_back(C);
        }
        for (const auto& Action : Stage.Actions)
        {
            if (!ValidItems(Action.ItemsToRemove) || !ValidItems(Action.Rewards.Items)
                || Action.Rewards.Gold < 0 || Action.Rewards.Experience < 0)
            { Error = TEXT("Invalid step action cost/reward: ") + ID; return false; }
            for (const auto& Cost : Action.ItemsToRemove) for (const auto& Task : Stage.Objectives)
                if (Task.Kind == EReditusQuestObjectiveKind::CollectItem
                    && Task.ItemRow.DataTable == Cost.ItemRow.DataTable && Task.ItemRow.RowName == Cost.ItemRow.RowName)
                { Error = TEXT("Use the StepAction objective for a handoff; do not also require possession of its consumed item in the same stage: ") + ID; return false; }
            if (!Action.GoldRewardChoiceID.IsNone())
            {
                bool Found = false;
                for (const auto& Earlier : Row->Stages)
                {
                    for (const auto& Choice : Earlier.Choices) if (Choice.ChoiceID == Action.GoldRewardChoiceID)
                    {
                        Found = true;
                        for (const auto& Option : Choice.Options) if (Option.Value < 0) Found = false;
                    }
                    if (&Earlier == &Stage) break;
                }
                if (!Found || Action.Rewards.Gold != 0)
                { Error = TEXT("GoldRewardChoiceID must refer to a current/earlier nonnegative choice, with fixed Gold = 0: ") + ID; return false; }
            }
            S.Actions.push_back(Action.ActionID.IsNone() ? std::string() : Identifier(Action.ActionID));
        }
        for (const auto& Route : Stage.Routes)
        {
            ReditusQuestRuntime::Route R;
            R.ChoiceId = Route.ChoiceID.IsNone() ? std::string() : Identifier(Route.ChoiceID);
            R.OptionId = Route.OptionID.IsNone() ? std::string() : Identifier(Route.OptionID);
            R.Next = Route.NextStageID.IsNone() ? std::string() : Identifier(Route.NextStageID); R.Finish = Route.Finish;
            S.Routes.push_back(R);
        }
        for (const auto& Objective : Stage.Objectives)
        {
            ReditusQuestRuntime::Task T;
            T.Id = Objective.ObjectiveID.IsNone() ? std::string() : Identifier(Objective.ObjectiveID);
            T.Target = Objective.TargetID.IsNone() ? std::string() : Identifier(Objective.TargetID);
            if (Objective.Kind == EReditusQuestObjectiveKind::CollectItem)
            {
                if (!ValidItemRow(Objective.ItemRow))
                { Error = TEXT("CollectItem objective needs a valid row from the game's item tables: ") + ID; return false; }
                T.Target = Narrow(Key(Objective.ItemRow));
            }
            if (Objective.Kind == EReditusQuestObjectiveKind::TimerElapsed) T.Target = "__timer";
            T.Type = static_cast<ReditusQuestRuntime::Kind>(Objective.Kind);
            T.Required = Objective.RequiredCount; T.Optional = Objective.Optional;
            S.Tasks.push_back(T);
        }
        D.Stages.push_back(S);
    }
    return true;
}
