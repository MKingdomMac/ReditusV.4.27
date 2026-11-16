#include "ReditusQuestSystemTypes.h"
#include "Editor.h"
#include "Engine/DataTable.h"

namespace
{
    FDataTableRowHandle QuestHandle(UDataTable* Table, const TCHAR* Name)
    { FDataTableRowHandle Row; Row.DataTable = Table; Row.RowName = Name; return Row; }
    FReditusQuestTaskDefinition Objective(const TCHAR* ID, EReditusQuestObjectiveKind Kind, const TCHAR* Target, const TCHAR* Text)
    {
        FReditusQuestTaskDefinition Task; Task.ObjectiveID = ID; Task.Kind = Kind;
        Task.TargetID = Target; Task.Description = FText::FromString(Text); return Task;
    }
    FReditusQuestItemAmount Item(UDataTable* Table, const TCHAR* Name)
    { FReditusQuestItemAmount Amount; Amount.ItemRow = QuestHandle(Table, Name); return Amount; }
    FReditusQuestStageDefinition Payment(UDataTable* Quests, const TCHAR* StageID, const TCHAR* ActionID,
        const FReditusQuestItemAmount& Letter, const TCHAR* NextQuest)
    {
        FReditusQuestStageDefinition Stage; Stage.StageID = StageID; Stage.FinishAfterStage = true;
        Stage.Description = FText::FromString(TEXT("Hand over the letter and receive the agreed payment."));
        FReditusQuestStepAction Exchange; Exchange.ActionID = ActionID;
        Exchange.ItemsToRemove.Add(Letter); Exchange.GoldRewardChoiceID = TEXT("AgreedPayment");
        Stage.Actions.Add(Exchange);
        Stage.Objectives.Add(Objective(TEXT("CompleteExchange"), EReditusQuestObjectiveKind::StepAction, ActionID,
            TEXT("Exchange the letter for the agreed payment.")));
        Stage.StartQuestsOnCompletion.Add(QuestHandle(Quests, NextQuest));
        return Stage;
    }
}

void CreateReditusQuestStepExamples()
{
    if (GEditor && GEditor->PlayWorld)
    { UE_LOG(LogTemp, Warning, TEXT("Stop Play before creating quest examples.")); return; }
    auto* Quests = LoadObject<UDataTable>(nullptr, TEXT("/Game/Reditus/Story/DT_Quests.DT_Quests"));
    auto* KeyItems = LoadObject<UDataTable>(nullptr, TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_KeyItems.DT_KeyItems"));
    auto* Equipment = LoadObject<UDataTable>(nullptr, TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_Equipment.DT_Equipment"));
    if (!Quests || Quests->GetRowStruct() != FReditusQuestDefinition::StaticStruct())
    { UE_LOG(LogTemp, Error, TEXT("Run Reditus.CreateQuestSystem first to create DT_Quests.")); return; }
    if (!KeyItems || !KeyItems->GetRowMap().Contains(TEXT("Letter"))
        || !Equipment || !Equipment->GetRowMap().Contains(TEXT("MediocrisRing")))
    { UE_LOG(LogTemp, Error, TEXT("Step examples need DT_KeyItems/Letter and DT_Equipment/MediocrisRing. No rows changed.")); return; }
    const auto Letter = Item(KeyItems, TEXT("Letter"));
    if (!Quests->GetRowMap().Contains(TEXT("Example_ExtortionSteps")))
    {
        FReditusQuestDefinition Quest;
        Quest.Title = FText::FromString(TEXT("Extortion — Step Example"));
        Quest.Description = FText::FromString(TEXT("Receive the letter, agree on payment, then exchange it. The bargain is remembered."));
        FReditusQuestStageDefinition Receive; Receive.StageID = TEXT("ReceiveLetter");
        Receive.Description = FText::FromString(TEXT("Receive the letter."));
        FReditusQuestStepAction Gift; Gift.ActionID = TEXT("GiveLetter"); Gift.Rewards.Items.Add(Letter);
        Receive.Actions.Add(Gift);
        Receive.Objectives.Add(Objective(TEXT("ReceiveLetter"), EReditusQuestObjectiveKind::StepAction, TEXT("GiveLetter"), TEXT("Receive the letter.")));
        Quest.Stages.Add(Receive);
        FReditusQuestStageDefinition Bargain; Bargain.StageID = TEXT("Negotiate");
        Bargain.Description = FText::FromString(TEXT("Agree on the payment."));
        FReditusQuestChoiceDefinition Choice; Choice.ChoiceID = TEXT("AgreedPayment");
        Choice.Description = FText::FromString(TEXT("Agreed payment"));
        for (int32 Amount : {1, 1500, 1750, 2000, 2500})
        {
            FReditusQuestChoiceOption Option; Option.OptionID = FName(*FString::Printf(TEXT("Payment%d"), Amount));
            Option.Label = FText::FromString(FString::Printf(TEXT("$%d"), Amount)); Option.Value = Amount; Choice.Options.Add(Option);
        }
        Bargain.Choices.Add(Choice);
        Bargain.Objectives.Add(Objective(TEXT("AgreePayment"), EReditusQuestObjectiveKind::Choice, TEXT("AgreedPayment"), TEXT("Choose and agree on payment.")));
        FReditusQuestStageRoute Low; Low.ChoiceID = TEXT("AgreedPayment"); Low.OptionID = TEXT("Payment1"); Low.NextStageID = TEXT("LowPayment");
        Bargain.Routes.Add(Low); Bargain.NextStageID = TEXT("NormalPayment"); Quest.Stages.Add(Bargain);
        Quest.Stages.Add(Payment(Quests, TEXT("NormalPayment"), TEXT("CollectNormalPayment"), Letter, TEXT("Example_LetsGetWedge")));
        Quest.Stages.Add(Payment(Quests, TEXT("LowPayment"), TEXT("CollectLowPayment"), Letter, TEXT("Example_BargainedTooMuch")));
        Quests->AddRow(TEXT("Example_ExtortionSteps"), Quest); Quests->MarkPackageDirty();
    }
    if (!Quests->GetRowMap().Contains(TEXT("Example_LetsGetWedge")))
    {
        FReditusQuestDefinition Quest; Quest.Title = FText::FromString(TEXT("Let's Get Wedge — Step Example"));
        Quest.Description = FText::FromString(TEXT("Example follow-up: speak to Wedge, then receive the ring once."));
        FReditusQuestStageDefinition Talk; Talk.StageID = TEXT("SpeakToWedge");
        Talk.Objectives.Add(Objective(TEXT("SpeakToWedge"), EReditusQuestObjectiveKind::Talk, TEXT("Wedge"), TEXT("Speak to Wedge.")));
        Quest.Stages.Add(Talk);
        FReditusQuestStageDefinition Reward; Reward.StageID = TEXT("ReceiveRing");
        FReditusQuestStepAction Gift; Gift.ActionID = TEXT("GiveMediocrisRing");
        Gift.Rewards.Items.Add(Item(Equipment, TEXT("MediocrisRing")));
        Reward.Actions.Add(Gift); Reward.Objectives.Add(Objective(TEXT("ReceiveRing"), EReditusQuestObjectiveKind::StepAction,
            TEXT("GiveMediocrisRing"), TEXT("Receive the Mediocris Ring.")));
        Quest.Stages.Add(Reward); Quests->AddRow(TEXT("Example_LetsGetWedge"), Quest); Quests->MarkPackageDirty();
    }
    if (!Quests->GetRowMap().Contains(TEXT("Example_BargainedTooMuch")))
    {
        FReditusQuestDefinition Quest; Quest.Title = FText::FromString(TEXT("Bargained Too Far — Step Example"));
        Quest.Description = FText::FromString(TEXT("The $1 bargain leads to a different follow-up."));
        FReditusQuestStageDefinition Talk; Talk.StageID = TEXT("SpeakToWedge");
        Talk.Objectives.Add(Objective(TEXT("SpeakToWedge"), EReditusQuestObjectiveKind::Talk, TEXT("Wedge"), TEXT("Speak to Wedge about the outcome.")));
        Quest.Stages.Add(Talk); Quests->AddRow(TEXT("Example_BargainedTooMuch"), Quest); Quests->MarkPackageDirty();
    }
    UE_LOG(LogTemp, Display, TEXT("Added missing Example_ExtortionSteps / Example_LetsGetWedge / Example_BargainedTooMuch rows. Existing rows and dialogue assets preserved. Save All, then follow START_HERE_MULTI_STEP_QUESTS.txt."));
}
