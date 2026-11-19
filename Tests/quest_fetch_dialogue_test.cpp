// Actual simple-quest eligibility, phase and action methods, with typed host stand-ins.
#include "ReditusFetchQuestFlow.h"
#include <cassert>
#include <string>
#include <map>
#include <vector>
using int32=int;
#define TEXT(V) V
template<class T>struct TArray:std::vector<T>{using std::vector<T>::vector;int Num()const{return int(this->size());}};
struct FName{std::string V;FName()=default;FName(const char* S):V(S){}bool IsNone()const{return V.empty();}};
enum class EReditusQuestState{Unknown,Unavailable,Available,Active,ReadyToDeliver,Completed,Failed};
enum class EReditusQuestObjectiveKind{CollectItem,DefeatEnemy,Talk,Choice,StepAction};
enum class EReditusQuestTimerMode{None,Wait,Deadline};enum class EReditusQuestNPCDialogueMode{DialogueAssets,FetchQuestText};
struct APlayerController{bool Local=true;std::map<std::string,int> Items;bool IsLocalController()const{return Local;}};
bool IsValid(APlayerController* P){return P!=nullptr;}
struct Table;
struct FDataTableRowHandle{Table* DataTable=nullptr;FName RowName;bool operator==(const FDataTableRowHandle& R)const{return DataTable==R.DataTable&&RowName.V==R.RowName.V;}};
struct Objective{EReditusQuestObjectiveKind Kind=EReditusQuestObjectiveKind::CollectItem;bool Optional=false;FDataTableRowHandle ItemRow;int ItemKind=0,RequiredCount=1;};
struct Stage{struct{EReditusQuestTimerMode Mode=EReditusQuestTimerMode::None;}Timer;TArray<int> Actions,Choices,Routes;FName NextStageID;TArray<Objective> Objectives;};
struct Cost{FDataTableRowHandle ItemRow;int Kind=0,Amount=1;};
struct FReditusQuestDefinition{static int* StaticStruct(){static int I;return &I;}TArray<Stage> Stages;TArray<Cost> TurnInItems;};
struct Table{bool Valid=true;FReditusQuestDefinition Row;int* GetRowStruct(){return Valid?FReditusQuestDefinition::StaticStruct():nullptr;}template<class T>const T* FindRow(FName,const char*,bool){return &Row;}};
struct FReditusQuestSnapshot{EReditusQuestState State=EReditusQuestState::Available;};
struct FReditusQuestNPCDefinition{FDataTableRowHandle QuestRow;EReditusQuestNPCDialogueMode DialogueMode=EReditusQuestNPCDialogueMode::FetchQuestText;};
struct UReditusQuestSubsystem
{
    static UReditusQuestSubsystem* Current;static UReditusQuestSubsystem* GetQuestSystem(const void*){return Current;}
    EReditusQuestState Status=EReditusQuestState::Available;bool Pending=false,RejectStart=false,RejectTurnIn=false,Defer=false;
    int Starts=0,Payments=0,Defeats=0,ItemLookups=0;APlayerController* Player=nullptr;
    bool ReadQuest(FDataTableRowHandle,FReditusQuestSnapshot& S){S.State=Status;return true;}
    struct Delivery{FDataTableRowHandle Quest;};FDataTableRowHandle Quest;
    TArray<Delivery> GetPendingDeliveries(){return Pending?TArray<Delivery>{{Quest}}:TArray<Delivery>{};}
    bool GetQuestItemCount(APlayerController* P,FDataTableRowHandle R,int,int& Count)
    {++ItemLookups;auto I=P->Items.find(R.RowName.V);Count=I==P->Items.end()?0:I->second;return R.DataTable!=nullptr;}
    void RefreshQuestInventory(APlayerController*)
    {
        if(Pending||(Status!=EReditusQuestState::Active&&Status!=EReditusQuestState::ReadyToDeliver))return;
        bool Ready=true;
        for(const auto& T:Quest.DataTable->Row.Stages[0].Objectives)
            if(!T.Optional)
            {
                const int Count=T.Kind==EReditusQuestObjectiveKind::DefeatEnemy?Defeats:Player->Items[T.ItemRow.RowName.V];
                Ready=Ready&&Count>=T.RequiredCount;
            }
        Status=Ready?EReditusQuestState::ReadyToDeliver:EReditusQuestState::Active;
    }
    bool StartQuest(FDataTableRowHandle){if(RejectStart)return false;++Starts;Status=EReditusQuestState::Active;RefreshQuestInventory(Player);return true;}
    bool TurnInFetchQuest(FDataTableRowHandle,APlayerController*)
    {
        if(RejectTurnIn||Pending)return false;
        if(Defer){Pending=true;return true;}
        ++Payments;
        for(const auto& C:Quest.DataTable->Row.TurnInItems)Player->Items[C.ItemRow.RowName.V]-=C.Amount;
        Status=EReditusQuestState::Completed;return true;
    }
};
UReditusQuestSubsystem* UReditusQuestSubsystem::Current=nullptr;
struct UReditusQuestNPCComponent
{
    FReditusQuestNPCDefinition Definition;bool bEndingPlay=false,bTalking=true,ActiveFetchDialogue=true,bFetchActionFailed=false;
    struct{APlayerController* Value=nullptr;APlayerController* Get()const{return Value;}}ConversationPlayer;
    const FReditusQuestNPCDefinition* ResolveDefinition()const{return &Definition;}FDataTableRowHandle GetNPCQuestRow()const{return Definition.QuestRow;}
    ReditusFetchQuestFlow::Phase GetFetchQuestPhase(APlayerController*)const;
    bool ApplyFetchQuestAction(APlayerController*,ReditusFetchQuestFlow::Action);
};
namespace ReditusQuestCatalog {
const FReditusQuestDefinition* Definition(FDataTableRowHandle Q) {
    return Q.DataTable && Q.DataTable->GetRowStruct()==FReditusQuestDefinition::StaticStruct()
        ? Q.DataTable->FindRow<FReditusQuestDefinition>(Q.RowName,TEXT("Quest"),false) : nullptr;
}}
// ACTUAL_FETCH_ELIGIBILITY
// ACTUAL_FETCH_PHASE_ACTION
int main()
{
    using namespace ReditusFetchQuestFlow;
    using K=EReditusQuestObjectiveKind;using Q=EReditusQuestState;
    Table Quests,Items;Objective Need;Need.ItemRow={&Items,FName("Letter")};Stage S;S.Objectives.push_back(Need);
    FReditusQuestDefinition Fetch;Fetch.Stages.push_back(S);Fetch.TurnInItems.push_back({Need.ItemRow,0,1});Quests.Row=Fetch;
    APlayerController P;UReditusQuestSubsystem System;System.Player=&P;System.Quest={&Quests,FName("Quest")};UReditusQuestSubsystem::Current=&System;
    UReditusQuestNPCComponent NPC;NPC.Definition.QuestRow=System.Quest;NPC.ConversationPlayer.Value=&P;
    auto Reset=[&](const FReditusQuestDefinition& Row)
    {
        Quests.Row=Row;P.Items.clear();P.Local=true;System.Status=Q::Available;
        System.Pending=System.RejectStart=System.RejectTurnIn=System.Defer=false;
        System.Starts=System.Payments=System.Defeats=System.ItemLookups=0;
        NPC.bFetchActionFailed=false;
    };
    // Existing fetch behavior, including first-interaction hand-in, is preserved.
    assert(AllowHandInDecline(Fetch));
    assert(NPC.GetFetchQuestPhase(&P)==Phase::Offer&&System.Starts==0);
    P.Items["Letter"]=1;assert(NPC.GetFetchQuestPhase(&P)==Phase::Ready&&System.Starts==0);
    assert(NPC.ApplyFetchQuestAction(&P,Action::HandIn));assert(System.Starts==1&&System.Payments==1&&P.Items["Letter"]==0);
    assert(NPC.GetFetchQuestPhase(&P)==Phase::Completed);P.Items["Letter"]=5;
    assert(!NPC.ApplyFetchQuestAction(&P,Action::HandIn)&&System.Payments==1&&P.Items["Letter"]==5);
    Reset(Fetch);assert(NPC.ApplyFetchQuestAction(&P,Action::Accept)&&NPC.GetFetchQuestPhase(&P)==Phase::Reminder);
    assert(!NPC.ApplyFetchQuestAction(&P,Action::HandIn)&&System.Payments==0);
    P.Items["Letter"]=1;assert(NPC.GetFetchQuestPhase(&P)==Phase::Ready);
    System.RejectTurnIn=true;assert(!NPC.ApplyFetchQuestAction(&P,Action::HandIn));assert(NPC.GetFetchQuestPhase(&P)==Phase::Error&&P.Items["Letter"]==1);
    System.RejectTurnIn=false;System.Defer=true;assert(NPC.ApplyFetchQuestAction(&P,Action::HandIn));assert(NPC.GetFetchQuestPhase(&P)==Phase::Pending&&System.Payments==0);
    assert(!NPC.ApplyFetchQuestAction(&P,Action::HandIn)&&NPC.GetFetchQuestPhase(&P)==Phase::Pending);
    assert(!NPC.ApplyFetchQuestAction(&P,Action::Accept)&&System.Starts==1);
    System.Pending=false;System.Defer=false;System.Status=Q::Unavailable;NPC.bFetchActionFailed=false;
    assert(NPC.GetFetchQuestPhase(&P)==Phase::Unavailable);assert(!NPC.ApplyFetchQuestAction(&P,Action::HandIn)&&System.Payments==0);
    System.Status=Q::Available;System.RejectStart=true;assert(!NPC.ApplyFetchQuestAction(&P,Action::HandIn)&&NPC.GetFetchQuestPhase(&P)==Phase::Error);
    Reset(Fetch);P.Items["Letter"]=1;
    assert(!NPC.ApplyFetchQuestAction(&P,Action::None)&&System.Starts==0&&System.Payments==0);
    NPC.bTalking=false;assert(!NPC.ApplyFetchQuestAction(&P,Action::HandIn));NPC.bTalking=true;
    P.Local=false;assert(!NPC.ApplyFetchQuestAction(&P,Action::HandIn));P.Local=true;
    APlayerController Other;assert(!NPC.ApplyFetchQuestAction(&Other,Action::HandIn));
    assert(!NPC.ApplyFetchQuestAction(nullptr,Action::HandIn));
    NPC.Definition.DialogueMode=EReditusQuestNPCDialogueMode::DialogueAssets;
    assert(!NPC.ApplyFetchQuestAction(&P,Action::HandIn));NPC.Definition.DialogueMode=EReditusQuestNPCDialogueMode::FetchQuestText;
    // A pure hunt has no required items. That must never imply a completed kill.
    FReditusQuestDefinition Hunt;Stage H;Objective Enemy;Enemy.Kind=K::DefeatEnemy;Enemy.RequiredCount=2;
    H.Objectives.push_back(Enemy);Hunt.Stages.push_back(H);Reset(Hunt);
    assert(!AllowHandInDecline(Hunt));
    assert(NPC.GetFetchQuestPhase(&P)==Phase::Offer&&System.ItemLookups==0);
    assert(!NPC.ApplyFetchQuestAction(&P,Action::HandIn)&&System.Starts==0&&System.Payments==0);
    assert(NPC.ApplyFetchQuestAction(&P,Action::Accept)&&System.Starts==1);
    assert(NPC.GetFetchQuestPhase(&P)==Phase::Reminder&&System.ItemLookups==0);
    System.Defeats=1;System.RefreshQuestInventory(&P);assert(NPC.GetFetchQuestPhase(&P)==Phase::Reminder);
    assert(!NPC.ApplyFetchQuestAction(&P,Action::HandIn)&&System.Payments==0);
    System.Defeats=2;System.RefreshQuestInventory(&P);assert(NPC.GetFetchQuestPhase(&P)==Phase::Ready&&System.Payments==0);
    assert(NPC.ApplyFetchQuestAction(&P,Action::HandIn)&&System.Payments==1&&System.Starts==1&&P.Items.empty());
    assert(NPC.GetFetchQuestPhase(&P)==Phase::Completed);
    assert(!NPC.ApplyFetchQuestAction(&P,Action::HandIn)&&System.Payments==1);
    // A stale item-row selection on a Defeat Enemy objective is irrelevant.
    Hunt.Stages[0].Objectives[0].ItemRow={&Items,FName("UnrelatedEquipment")};Reset(Hunt);
    assert(!AllowHandInDecline(Hunt)); // An unused item row on a kill is not a fetch requirement.
    assert(NPC.ApplyFetchQuestAction(&P,Action::Accept));System.Defeats=2;System.RefreshQuestInventory(&P);
    assert(NPC.GetFetchQuestPhase(&P)==Phase::Ready&&System.ItemLookups==0);
    // Mixed requirements need both a recorded defeat and live item ownership.
    FReditusQuestDefinition Mixed=Hunt;Mixed.Stages[0].Objectives.push_back(Need);Reset(Mixed);P.Items["Letter"]=1;
    assert(AllowHandInDecline(Mixed));
    assert(NPC.GetFetchQuestPhase(&P)==Phase::Offer);assert(!NPC.ApplyFetchQuestAction(&P,Action::HandIn)&&System.Starts==0);
    assert(NPC.ApplyFetchQuestAction(&P,Action::Accept)&&NPC.GetFetchQuestPhase(&P)==Phase::Reminder);
    System.Defeats=2;System.RefreshQuestInventory(&P);assert(NPC.GetFetchQuestPhase(&P)==Phase::Ready);
    P.Items["Letter"]=0;assert(!NPC.ApplyFetchQuestAction(&P,Action::HandIn)&&System.Payments==0);
    assert(NPC.GetFetchQuestPhase(&P)==Phase::Reminder);
    P.Items["Letter"]=1;System.RefreshQuestInventory(&P);assert(NPC.ApplyFetchQuestAction(&P,Action::HandIn)&&System.Payments==1);
    // Costs remain required for a hunt, even after the monster objective is done.
    FReditusQuestDefinition CostHunt=Hunt;CostHunt.TurnInItems=Fetch.TurnInItems;Reset(CostHunt);
    assert(AllowHandInDecline(CostHunt));
    assert(NPC.ApplyFetchQuestAction(&P,Action::Accept));System.Defeats=2;System.RefreshQuestInventory(&P);
    assert(System.Status==Q::ReadyToDeliver&&NPC.GetFetchQuestPhase(&P)==Phase::Reminder);
    assert(!NPC.ApplyFetchQuestAction(&P,Action::HandIn)&&System.Payments==0);
    P.Items["Letter"]=1;assert(NPC.GetFetchQuestPhase(&P)==Phase::Ready);
    assert(NPC.ApplyFetchQuestAction(&P,Action::HandIn)&&P.Items["Letter"]==0&&System.Payments==1);
    // Optional tasks do not block required objectives in either direction.
    FReditusQuestDefinition Optional=Hunt;Objective Bonus=Need;Bonus.Optional=true;Optional.Stages[0].Objectives.push_back(Bonus);Reset(Optional);
    assert(!AllowHandInDecline(Optional));
    assert(NPC.ApplyFetchQuestAction(&P,Action::Accept));System.Defeats=2;System.RefreshQuestInventory(&P);
    assert(NPC.GetFetchQuestPhase(&P)==Phase::Ready&&System.ItemLookups==0);
    Optional=Fetch;Bonus=Enemy;Bonus.Optional=true;Optional.Stages[0].Objectives.push_back(Bonus);Reset(Optional);P.Items["Letter"]=1;
    assert(AllowHandInDecline(Optional));
    assert(NPC.GetFetchQuestPhase(&P)==Phase::Ready);assert(NPC.ApplyFetchQuestAction(&P,Action::HandIn)&&System.Payments==1);
    // A pending handler must not receive another request or claim completion.
    Reset(Hunt);assert(NPC.ApplyFetchQuestAction(&P,Action::Accept));System.Defeats=2;System.RefreshQuestInventory(&P);System.Defer=true;
    assert(NPC.ApplyFetchQuestAction(&P,Action::HandIn)&&NPC.GetFetchQuestPhase(&P)==Phase::Pending);
    assert(!NPC.ApplyFetchQuestAction(&P,Action::HandIn)&&System.Payments==0);
    System.Pending=false;System.Defer=false;System.Status=Q::Failed;
    assert(NPC.GetFetchQuestPhase(&P)==Phase::Failed&&!NPC.ApplyFetchQuestAction(&P,Action::HandIn));
    // Deadline quests use the same confirmed objective state and manual turn-in.
    FReditusQuestDefinition TimedHunt=Hunt;TimedHunt.Stages[0].Timer.Mode=EReditusQuestTimerMode::Deadline;Reset(TimedHunt);
    assert(!AllowHandInDecline(TimedHunt));
    assert(FetchDefinition(System.Quest)&&NPC.GetFetchQuestPhase(&P)==Phase::Offer);
    assert(!NPC.ApplyFetchQuestAction(&P,Action::HandIn)&&System.Starts==0);
    assert(NPC.ApplyFetchQuestAction(&P,Action::Accept)&&NPC.GetFetchQuestPhase(&P)==Phase::Reminder);
    System.Defeats=2;System.RefreshQuestInventory(&P);assert(NPC.GetFetchQuestPhase(&P)==Phase::Ready&&System.Payments==0);
    assert(NPC.ApplyFetchQuestAction(&P,Action::HandIn)&&System.Payments==1);
    Reset(TimedHunt);assert(NPC.ApplyFetchQuestAction(&P,Action::Accept));System.Status=Q::Failed;
    assert(NPC.GetFetchQuestPhase(&P)==Phase::Failed);
    assert(!NPC.ApplyFetchQuestAction(&P,Action::HandIn)&&!NPC.ApplyFetchQuestAction(&P,Action::Accept));
    assert(System.Payments==0&&System.Starts==1); // Talking after timeout never retries or rewards.
    FReditusQuestDefinition TimedFetch=Fetch;TimedFetch.Stages[0].Timer.Mode=EReditusQuestTimerMode::Deadline;Reset(TimedFetch);P.Items["Letter"]=1;
    assert(NPC.GetFetchQuestPhase(&P)==Phase::Ready&&System.Starts==0);
    assert(NPC.ApplyFetchQuestAction(&P,Action::HandIn)&&System.Starts==1&&System.Payments==1);
    // Complex definitions stay in Dialogue Assets instead of partially running.
    Reset(Hunt);Quests.Row.Stages[0].Timer.Mode=EReditusQuestTimerMode::Wait;assert(!FetchDefinition(System.Quest));
    Reset(Hunt);Quests.Row.Stages[0].Actions.push_back(1);assert(!FetchDefinition(System.Quest));
    Reset(Hunt);Quests.Row.Stages[0].Choices.push_back(1);assert(!FetchDefinition(System.Quest));
    Reset(Hunt);Quests.Row.Stages[0].Routes.push_back(1);assert(!FetchDefinition(System.Quest));
    Reset(Hunt);Quests.Row.Stages[0].NextStageID=FName("Next");assert(!FetchDefinition(System.Quest));
    Reset(Hunt);Quests.Row.Stages.push_back(H);assert(!FetchDefinition(System.Quest));
    for(auto Kind:{K::Talk,K::Choice,K::StepAction})
    {Reset(Hunt);Quests.Row.Stages[0].Objectives[0].Kind=Kind;assert(!FetchDefinition(System.Quest));}
    Reset(Hunt);Quests.Row.Stages[0].Objectives[0].Optional=true;assert(!FetchDefinition(System.Quest));
    Reset(Hunt);Quests.Row.Stages[0].Objectives.clear();assert(!FetchDefinition(System.Quest));
    Reset(Hunt);Quests.Valid=false;assert(NPC.GetFetchQuestPhase(&P)==Phase::Unavailable);
}
