#include "ReditusQuestRuntime.h"
#include <cassert>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>
using int32=int;
using TCHAR=char;
using FString=std::string;
template<class T>struct TGuardValue{T& Ref;T Before;TGuardValue(T& R,T V):Ref(R),Before(R){R=V;}~TGuardValue(){Ref=Before;}};
template<class T>struct TArray:std::vector<T>{using std::vector<T>::vector;using std::vector<T>::operator=;int Num()const{return int(this->size());}};
struct FName {
    std::string Text; FName()=default; FName(const char* V):Text(V){};
    bool IsNone() const { return Text.empty(); }
    bool operator==(const FName& B) const { return Text==B.Text; }
    bool operator!=(const FName& B) const { return !(*this==B); }
};
static FName NAME_None;
static std::string StepKey(const FString& V){return V;}
static std::string StepID(FName V){return V.Text;}
static std::string Narrow(const FString& V){return V;}
static std::string Identifier(FName V){return V.Text;}
static FName SavedName(const std::string& V){return FName(V.c_str());}
#define TEXT(V) V
#define UE_LOG(...) do {} while(false)
struct Table;
struct FDataTableRowHandle { Table* DataTable=nullptr; FName RowName; };
enum class EReditusQuestItemKind{Usable,Misc,KeyItem,Equipment};
struct Item { FName ID; int Amount=1;FDataTableRowHandle ItemRow;EReditusQuestItemKind Kind=EReditusQuestItemKind::Misc; };
using FReditusQuestItemAmount=Item;
struct Rewards { int Gold=0, Experience=0; TArray<Item> Items; };
struct Action { FName ActionID, GoldRewardChoiceID; TArray<Item> ItemsToRemove; struct Rewards Rewards; };
enum class EReditusQuestObjectiveKind{CollectItem,Custom,ReachLocation};
struct Objective {FName ObjectiveID;EReditusQuestObjectiveKind Kind=EReditusQuestObjectiveKind::CollectItem;FDataTableRowHandle ItemRow;EReditusQuestItemKind ItemKind=EReditusQuestItemKind::Misc;bool Optional=false;int RequiredCount=1;FName TargetID;};
struct Stage { FName StageID; std::vector<Action> Actions;std::vector<Objective> Objectives; };
struct FReditusQuestDefinition {
    static int* StaticStruct(){static int Value;return &Value;}
    std::vector<Stage> Stages;
    TArray<Item> TurnInItems;struct Rewards Rewards;
};
struct Table {
    struct Rows{std::set<std::string> Names;bool Contains(FName N)const{return Names.count(N.Text)!=0;}}Values;
    struct ObjectPath{std::string V;const char* operator*()const{return V.c_str();}};
    std::string Path;
    bool Correct=true; FReditusQuestDefinition Row;
    const Rows& GetRowMap()const{return Values;}ObjectPath GetPathName()const{return {Path};}
    int* GetRowStruct(){return Correct ? FReditusQuestDefinition::StaticStruct() : nullptr;}
    template<class T>const T* FindRow(FName Name,const char*,bool){return Name.Text=="Quest"?&Row:nullptr;}
};
enum class EReditusQuestDeliveryKind { TurnIn,StepAction };
struct FGuid { int ID=0;static FGuid NewGuid(){static int Next=100;return {++Next};}bool operator!=(const FGuid& Other) const{return ID!=Other.ID;} };
struct FReditusQuestDelivery {
    FDataTableRowHandle Quest; EReditusQuestDeliveryKind Kind=EReditusQuestDeliveryKind::TurnIn;
    FName StageID,ActionID; TArray<Item> ItemsToRemove; struct Rewards Rewards; FGuid ReceiptID;
};
struct Pending {
    std::map<std::string,FReditusQuestDelivery> Values;
    const FReditusQuestDelivery* Find(const std::string& K)const{auto I=Values.find(K);return I==Values.end()?nullptr:&I->second;}
    void Remove(const std::string& K){Values.erase(K);}
    bool Contains(const std::string& K)const{return Values.count(K)!=0;}void Add(const std::string& K,FReditusQuestDelivery V){Values[K]=V;}
};
struct APlayerController{std::map<std::string,int> Inventory;int Gold=0;};
bool IsValid(APlayerController* P){return P!=nullptr;}
namespace UGameplayStatics{APlayerController* GetPlayerController(APlayerController* P,int){return P;}}
const FReditusQuestDefinition* Definition(FDataTableRowHandle Q){return Q.DataTable&&Q.DataTable->Correct&&Q.RowName.Text=="Quest"?&Q.DataTable->Row:nullptr;}
FString Wide(const std::string& S){return S;}
struct Handles{std::map<std::string,FDataTableRowHandle> Values;const FDataTableRowHandle* Find(const std::string& S)const{auto I=Values.find(S);return I==Values.end()?nullptr:&I->second;}};
struct FrameTime { double Value; double AsDecimal() const { return Value; } };
struct QualifiedTime { FrameTime Time; };
struct ULevelSequencePlayer
{
    bool Playing = false, Paused = false, Reversed = false;
    double Position = 0, Start = 0, End = 100;
    bool IsPlaying() const { return Playing; }
    bool IsPaused() const { return Paused; }
    bool IsReversed() const { return Reversed; }
    QualifiedTime GetCurrentTime() const { return {{Position}}; }
    QualifiedTime GetStartTime() const { return {{Start}}; }
    QualifiedTime GetEndTime() const { return {{End}}; }
};
bool IsValid(const ULevelSequencePlayer* Player) { return Player != nullptr; }
struct ALevelSequenceActor
{
    ULevelSequencePlayer* Player = nullptr;
    struct Settings { bool bPauseAtEnd = false; } PlaybackSettings;
    ULevelSequencePlayer* GetSequencePlayer() const { return Player; }
};
struct UWorld
{
    std::vector<ALevelSequenceActor*> Sequences;
    bool Paused = false;
    bool IsPaused() const { return Paused; }
};
template<class T> struct TActorIterator
{
    const UWorld* World;
    std::size_t Index = 0;
    explicit TActorIterator(const UWorld* W) : World(W) {}
    explicit operator bool() const { return Index < World->Sequences.size(); }
    T* operator->() const { return World->Sequences[Index]; }
    void operator++() { ++Index; }
};
struct FReditusQuestSequence { bool PauseQuestTimers = true; };
enum class EReditusQuestState { Available, Active, ReadyToDeliver, Completed, Failed };
struct FReditusQuestSnapshot { EReditusQuestState State; FName StageID; };
struct FReditusQuestObjectiveStatus { ::Objective Objective; bool CurrentStage, Complete; };
struct UReditusQuestSubsystem {
    ReditusQuestRuntime::Engine Runtime; Pending Deliveries; bool bImporting=false; int Updates=0;
    Handles QuestRows;APlayerController* Player=nullptr;bool bAllowBuiltInFetchDelivery=false,bApplyingNPCAction=false;
    bool BuiltInRewardsApply = false, PauseQuestTimers = false;
    UWorld* World = nullptr;
    ULevelSequencePlayer* SequencePlayer = nullptr;
    FReditusQuestSequence ActiveSequence;
    TArray<FReditusQuestSequence> SequenceQueue;
    struct WorldRef { UWorld* Value = nullptr; UWorld* Get() const { return Value; } } QueueWorld;
    UWorld* GetTickableGameObjectWorld() const { return World; }
    bool AreQuestTimersPaused() const;
    struct Event{bool Bound=true;int Calls=0;FReditusQuestDelivery Last;bool IsBound()const{return Bound;}void Broadcast(FReditusQuestDelivery D){++Calls;Last=D;}}OnQuestDeliveryRequested;
    static std::string Key(FDataTableRowHandle Q){return Q.DataTable?Q.RowName.Text:std::string();}
    bool GetQuestChoice(FDataTableRowHandle Quest,FName ChoiceID,FName& OptionID,int32& Value)const;
    bool BuildStepDelivery(FDataTableRowHandle Quest,FName ActionID,FReditusQuestDelivery& Delivery)const;
    bool ConfirmQuestDelivery(FDataTableRowHandle Quest,FGuid ReceiptID);
    bool TurnInQuest(FDataTableRowHandle Quest,APlayerController* Player);
    bool TurnInDialogueQuest(FDataTableRowHandle Quest,APlayerController* Player);
    bool ApplyBuiltInFetchDelivery(APlayerController* P,const FReditusQuestDelivery& Delivery,FString&)
    {
        if (!P || !BuiltInRewardsApply || !ConfirmQuestDelivery(Delivery.Quest, Delivery.ReceiptID)) return false;
        for (const auto& Item : Delivery.Rewards.Items) P->Inventory[Item.ItemRow.RowName.Text] += Item.Amount;
        return true;
    }
    bool ReadQuest(FDataTableRowHandle Quest, FReditusQuestSnapshot& Snapshot)
    {
        using State = ReditusQuestRuntime::State;
        const auto* Record = Runtime.FindRecord(Key(Quest));
        if (!Record) { Snapshot.State = EReditusQuestState::Available; return true; }
        Snapshot.StageID = SavedName(Record->StageId);
        switch (Record->Status)
        {
        case State::Active: Snapshot.State = EReditusQuestState::Active; return true;
        case State::Ready: Snapshot.State = EReditusQuestState::ReadyToDeliver; return true;
        case State::Completed: Snapshot.State = EReditusQuestState::Completed; return true;
        case State::Failed: Snapshot.State = EReditusQuestState::Failed; return true;
        default: return false;
        }
    }
    bool GetQuestDetails(FDataTableRowHandle Quest, FReditusQuestDefinition& Out,
        FReditusQuestSnapshot& Snapshot, TArray<FReditusQuestObjectiveStatus>& Objectives)
    {
        const auto* Row = Definition(Quest);
        if (!Row || !ReadQuest(Quest, Snapshot)) return false;
        Out = *Row;
        for (const auto& Stage : Row->Stages)
            for (const auto& Obj : Stage.Objectives)
                Objectives.push_back({Obj, Stage.StageID == Snapshot.StageID,
                    Runtime.GetCount(Key(Quest), Stage.StageID.Text, Obj.ObjectiveID.Text) >= Obj.RequiredCount});
        return true;
    }
    bool ReportQuestEvent(EReditusQuestObjectiveKind Kind,FName Target,FName Unique)
    {
        assert(Kind == EReditusQuestObjectiveKind::ReachLocation);
        return Runtime.Signal(ReditusQuestRuntime::Kind::ReachLocation, Target.Text, 1, Unique.Text);
    }
    bool CancelQuestDelivery(FDataTableRowHandle Q,FGuid){Deliveries.Remove(Key(Q));return true;}
    bool StartQuest(FDataTableRowHandle Quest,bool Ignore=false);
    bool RefreshQuestInventory(APlayerController* Player);
    bool Prepare(FDataTableRowHandle Q){return Definition(Q)!=nullptr;}
    bool StartInternal(FDataTableRowHandle Q,bool Ignore){if(!Prepare(Q))return false;QuestRows.Values[Key(Q)]=Q;return Runtime.Start(Key(Q),Ignore);}
    APlayerController* GetGameInstance(){return Player;}
    bool GetQuestItemCount(APlayerController* P,FDataTableRowHandle ItemRow,EReditusQuestItemKind Kind,int32& Count)const;
    void FinishInternal(FDataTableRowHandle Q){Runtime.Finish(Key(Q));}
    void FlushChanges(){++Updates;}
    static UReditusQuestSubsystem* GetQuestSystem(struct UReditusQuestObjectiveComponent* Component);
};
struct FReditusQuestObjectiveDefinition { FDataTableRowHandle QuestRow; FName StageID, ObjectiveID; };
struct UReditusQuestObjectiveComponent
{
    FReditusQuestObjectiveDefinition Definition;
    UReditusQuestSubsystem* System = nullptr;
    bool bTurnInQuestOnArrival = false;
    const FReditusQuestObjectiveDefinition* ResolveDefinition() const { return &Definition; }
    bool ReportObjectiveReached(FName UniqueEventID);
};
UReditusQuestSubsystem* UReditusQuestSubsystem::GetQuestSystem(UReditusQuestObjectiveComponent* Component)
{
    return Component->System;
}
namespace ReditusQuestCatalog { const FReditusQuestDefinition* Definition(FDataTableRowHandle Q){return ::Definition(Q);} }
// ACTUAL_ITEM_ROUTING
// ACTUAL_CHOICE_LOOKUP
// ACTUAL_STEP_DELIVERY
// ACTUAL_DELIVERY_CONFIRM
// ACTUAL_START_QUEST
// ACTUAL_REFRESH_INVENTORY
// ACTUAL_TURN_IN
// ACTUAL_ARRIVAL_AND_CLOCK
bool UReditusQuestSubsystem::GetQuestItemCount(APlayerController* P,FDataTableRowHandle ItemRow,EReditusQuestItemKind,int32& Count)const
{EReditusQuestItemKind Kind;Count=0;if(!P||!ReditusQuestItemRouting::ResolveKind(ItemRow,Kind))return false;Count=P->Inventory[ItemRow.RowName.Text];return true;}
int main()
{
    using namespace ReditusQuestRuntime;
    ReditusQuestRuntime::Definition D; D.Key="Quest";
    ReditusQuestRuntime::Stage Decide; Decide.Id="Negotiate";
    Task Agree; Agree.Id="Agree"; Agree.Type=Kind::Choice; Agree.Target="Payment"; Decide.Tasks={Agree};
    Choice C; C.Id="Payment"; ChoiceOption O; O.Id="Accept2000"; O.Value=2000; C.Options={O}; Decide.Choices={C};
    ReditusQuestRuntime::Stage Pay; Pay.Id="Pay"; Pay.Actions={"ExchangeLetter"};
    Task Paid; Paid.Id="Paid"; Paid.Type=Kind::StepAction; Paid.Target="ExchangeLetter"; Pay.Tasks={Paid};
    D.Stages={Decide,Pay}; UReditusQuestSubsystem System; std::string Error; assert(System.Runtime.AddDefinition(D,Error)); assert(System.Runtime.Start("Quest"));
    Table Data; Action Exchange; Exchange.ActionID="ExchangeLetter"; Exchange.GoldRewardChoiceID="Payment";
    Item Letter;Letter.ID="Letter";Exchange.ItemsToRemove={Letter};
    Table Keys,Equipment;
    Keys.Path="/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_KeyItems.DT_KeyItems";Keys.Values.Names.insert("Letter");
    Equipment.Path="/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_Equipment.DT_Equipment";Equipment.Values.Names.insert("Ring");
    Exchange.ItemsToRemove[0].ItemRow={&Keys,FName("Letter")};Exchange.ItemsToRemove[0].Kind=EReditusQuestItemKind::Equipment;
    Item Ring;Ring.ID="Ring";Ring.ItemRow={&Equipment,FName("Ring")};Ring.Kind=EReditusQuestItemKind::KeyItem;Exchange.Rewards.Items={Ring};
    ::Stage Authored; Authored.StageID="Pay"; Authored.Actions={Exchange}; Data.Row.Stages={Authored};
    FDataTableRowHandle Row{&Data,FName("Quest")}; FReditusQuestDelivery Delivery;
    assert(!System.BuildStepDelivery(Row,"ExchangeLetter",Delivery)); // Cannot pay before selection.
    assert(System.Runtime.Choose("Quest","Payment","Accept2000"));
    assert(System.BuildStepDelivery(Row,"ExchangeLetter",Delivery));
    assert(Delivery.Rewards.Gold==2000 && Delivery.ItemsToRemove.size()==1);
    assert(Delivery.ItemsToRemove[0].ID==FName("Letter") && Delivery.ItemsToRemove[0].Amount==1);
    assert(Delivery.ItemsToRemove[0].Kind==EReditusQuestItemKind::KeyItem&&Delivery.Rewards.Items[0].Kind==EReditusQuestItemKind::Equipment);
    Keys.Path="/Game/Unknown.DT_KeyItems";assert(!System.BuildStepDelivery(Row,"ExchangeLetter",Delivery));
    Keys.Path="/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_KeyItems.DT_KeyItems";
    assert(Delivery.Kind==EReditusQuestDeliveryKind::StepAction && Delivery.StageID==FName("Pay"));
    FName Selected; int Value=-1; assert(System.GetQuestChoice(Row,"Payment",Selected,Value)); assert(Value==2000 && Selected==FName("Accept2000"));
    assert(!System.GetQuestChoice(Row,"Other",Selected,Value)); assert(Selected.IsNone() && Value==0);
    Data.Correct=false; assert(!System.BuildStepDelivery(Row,"ExchangeLetter",Delivery)); Data.Correct=true;
    assert(!System.BuildStepDelivery(Row,"Missing",Delivery));
    assert(System.BuildStepDelivery(Row,"ExchangeLetter",Delivery)); Delivery.ReceiptID.ID=7;
    System.Deliveries.Values["Quest"]=Delivery;
    assert(!System.ConfirmQuestDelivery(Row,FGuid{8})); assert(System.Deliveries.Values.size()==1 && System.Runtime.GetState("Quest")==State::Active);
    System.bImporting=true; assert(!System.ConfirmQuestDelivery(Row,FGuid{7})); System.bImporting=false;
    assert(System.ConfirmQuestDelivery(Row,FGuid{7}));
    assert(System.Deliveries.Values.empty() && System.Runtime.GetState("Quest")==State::Ready);
    assert(System.Runtime.FindRecord("Quest")->Actions.count("ExchangeLetter"));
    assert(!System.ConfirmQuestDelivery(Row,FGuid{7})); assert(!System.BuildStepDelivery(Row,"ExchangeLetter",Delivery));
    // Existing final-turn-in confirmation still finishes instead of recording an action.
    Delivery=FReditusQuestDelivery(); Delivery.ReceiptID.ID=9; System.Deliveries.Values["Quest"]=Delivery;
    assert(System.ConfirmQuestDelivery(Row,FGuid{9})); assert(System.Runtime.GetState("Quest")==State::Completed);
    // The same delivery flow handles an item reward on a timed destination.
    {
        ::Table Catalog;
        Catalog.Row.Rewards.Items = {Ring};
        ::Objective Reach;
        Reach.Kind = EReditusQuestObjectiveKind::ReachLocation;
        Reach.ObjectiveID = "ReachGamacia";
        Reach.TargetID = "OdinPrologueArrival";
        ::Stage Travel; Travel.StageID = "Travel"; Travel.Objectives = {Reach}; Catalog.Row.Stages = {Travel};
        FDataTableRowHandle Handle{&Catalog, FName("Quest")};
        ReditusQuestRuntime::Definition Q; Q.Key = "Quest"; Q.HasRewards = true;
        ReditusQuestRuntime::Stage Timed; Timed.Id = "Travel"; Timed.Timer = TimerMode::Deadline; Timed.Seconds = 30;
        Task Arrive; Arrive.Type = Kind::ReachLocation; Arrive.Id = Reach.ObjectiveID.Text; Arrive.Target = Reach.TargetID.Text;
        Timed.Tasks = {Arrive}; Q.Stages = {Timed};

        APlayerController Odin;
        ULevelSequencePlayer Cutscene;
        ALevelSequenceActor Scene; Scene.Player = &Cutscene;
        UWorld World; World.Sequences = {&Scene};
        UReditusQuestSubsystem S;
        S.World = &World; S.Player = &Odin; S.BuiltInRewardsApply = true;
        S.OnQuestDeliveryRequested.Bound = false;
        UReditusQuestObjectiveComponent Destination;
        Destination.System = &S; Destination.Definition = {Handle, "Travel", "ReachGamacia"};
        Destination.bTurnInQuestOnArrival = true;
        assert(S.Runtime.AddDefinition(Q, Error));
        assert(!Destination.ReportObjectiveReached("GamaciaEntry"));
        assert(S.StartQuest(Handle));

        assert(!S.AreQuestTimersPaused());
        S.Runtime.Tick(10);
        Cutscene.Playing = true;
        assert(S.AreQuestTimersPaused());
        if (!S.AreQuestTimersPaused()) S.Runtime.Tick(60);
        Cutscene.Playing = false; Cutscene.Paused = true; Cutscene.Position = 40;
        assert(S.AreQuestTimersPaused());
        if (!S.AreQuestTimersPaused()) S.Runtime.Tick(60);
        assert(S.Runtime.FindRecord("Quest")->TimerRemaining == 20);
        Scene.PlaybackSettings.bPauseAtEnd = true;
        Cutscene.Position = 99.999999; // A final held pose stops pausing the quest.
        assert(!S.AreQuestTimersPaused());
        Cutscene.Reversed = true; Cutscene.Position = Cutscene.Start;
        assert(!S.AreQuestTimersPaused());
        Cutscene.Paused = false;
        World.Paused = true; assert(S.AreQuestTimersPaused()); World.Paused = false;
        S.PauseQuestTimers = true; assert(S.AreQuestTimersPaused()); S.PauseQuestTimers = false;
        S.SequencePlayer = &Cutscene; S.ActiveSequence.PauseQuestTimers = false;
        Cutscene.Playing = true; assert(!S.AreQuestTimersPaused());
        S.ActiveSequence.PauseQuestTimers = true; assert(S.AreQuestTimersPaused());
        S.SequencePlayer = nullptr; Cutscene.Playing = false;
        S.SequenceQueue.push_back({true});
        assert(!S.AreQuestTimersPaused()); // A queue from a different world is stale.
        S.QueueWorld.Value = &World; assert(S.AreQuestTimersPaused()); S.SequenceQueue.clear();

        S.Runtime.Tick(19);
        assert(!S.StartQuest(Handle)); // Repeated prologue handoff cannot restart the timer.
        assert(S.Runtime.FindRecord("Quest")->TimerRemaining == 1);
        assert(Destination.ReportObjectiveReached("GamaciaEntry"));
        assert(S.Runtime.GetState("Quest") == State::Completed && Odin.Inventory["Ring"] == 1);
        assert(!Destination.ReportObjectiveReached("GamaciaEntry") && Odin.Inventory["Ring"] == 1);

        UReditusQuestSubsystem Late;
        Late.World = &World; Late.Player = &Odin; Late.BuiltInRewardsApply = true;
        Destination.System = &Late;
        assert(Late.Runtime.AddDefinition(Q, Error) && Late.StartQuest(Handle));
        Late.Runtime.Tick(30);
        assert(Late.Runtime.GetState("Quest") == State::Failed);
        assert(!Destination.ReportObjectiveReached("GamaciaEntry") && Odin.Inventory["Ring"] == 1);

        UReditusQuestSubsystem Retry;
        Retry.Player = &Odin; Retry.OnQuestDeliveryRequested.Bound = false;
        Destination.System = &Retry;
        assert(Retry.Runtime.AddDefinition(Q, Error) && Retry.StartQuest(Handle));
        assert(!Destination.ReportObjectiveReached("GamaciaEntry"));
        assert(Retry.Runtime.GetState("Quest") == State::Ready && Odin.Inventory["Ring"] == 1);
        assert(!Retry.Runtime.Tick(600));
        Retry.BuiltInRewardsApply = true;
        assert(Destination.ReportObjectiveReached("GamaciaEntry"));
        assert(Retry.Runtime.GetState("Quest") == State::Completed && Odin.Inventory["Ring"] == 2);
    }
    // Native quest start/refresh/turn-in paths: pre-owned item and returning with it later.
    for(bool AlreadyOwned:{false,true}){
        UReditusQuestSubsystem S;APlayerController P;S.Player=&P;
        ::Table Catalog;Catalog.Row.Rewards.Gold=500;
        ::Objective Have;Have.ObjectiveID="HaveLetter";Have.ItemRow={&Keys,FName("Letter")};Have.ItemKind=EReditusQuestItemKind::Equipment;
        ::Stage Carry;Carry.StageID="Carry";Carry.Objectives={Have};Catalog.Row.Stages={Carry};
        Item Cost;Cost.ID="Letter";Cost.ItemRow=Have.ItemRow;Cost.Kind=EReditusQuestItemKind::Equipment;Catalog.Row.TurnInItems={Cost};
        ReditusQuestRuntime::Definition Q;Q.Key="Quest";Q.HasRewards=true;ReditusQuestRuntime::Stage St;St.Id="Carry";
        Task T;T.Id="HaveLetter";T.Type=Kind::CollectItem;T.Target="Letter";St.Tasks={T};Q.Stages={St};
        assert(S.Runtime.AddDefinition(Q,Error));FDataTableRowHandle Handle{&Catalog,FName("Quest")};
        P.Inventory["Letter"]=AlreadyOwned?1:0;assert(S.StartQuest(Handle));
        assert(S.OnQuestDeliveryRequested.Calls==0&&P.Gold==0);
        if(!AlreadyOwned){assert(S.Runtime.GetState("Quest")==State::Active);P.Inventory["Letter"]=1;assert(S.RefreshQuestInventory(&P));}
        assert(S.Runtime.GetState("Quest")==State::Ready);
        // Losing an item before the actual hand-in cannot pay a stale Ready state.
        P.Inventory["Letter"]=0;assert(!S.TurnInQuest(Handle,&P));assert(P.Gold==0&&S.OnQuestDeliveryRequested.Calls==0);
        assert(S.Runtime.GetState("Quest")==State::Active);P.Inventory["Letter"]=1;S.RefreshQuestInventory(&P);
        S.OnQuestDeliveryRequested.Bound=false;assert(!S.TurnInQuest(Handle,&P));assert(P.Gold==0);
        S.OnQuestDeliveryRequested.Bound=true;assert(S.TurnInQuest(Handle,&P));
        assert(S.OnQuestDeliveryRequested.Calls==1&&S.Runtime.GetState("Quest")==State::Ready&&P.Gold==0);
        const auto Receipt=S.OnQuestDeliveryRequested.Last;assert(Receipt.Rewards.Gold==500&&Receipt.ItemsToRemove[0].Kind==EReditusQuestItemKind::KeyItem);
        assert(!S.TurnInQuest(Handle,&P)); // Pending confirmation blocks another delivery.
        // Stand-in for the existing inventory handler: apply costs/rewards, then confirm once.
        --P.Inventory["Letter"];P.Gold+=Receipt.Rewards.Gold;assert(S.ConfirmQuestDelivery(Handle,Receipt.ReceiptID));
        assert(S.Runtime.GetState("Quest")==State::Completed&&P.Gold==500&&P.Inventory["Letter"]==0);
        assert(!S.TurnInQuest(Handle,&P)&&!S.ConfirmQuestDelivery(Handle,Receipt.ReceiptID)&&!S.StartQuest(Handle));
        assert(P.Gold==500&&S.OnQuestDeliveryRequested.Calls==1);
    }
}
