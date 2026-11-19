#include "ReditusQuestRuntime.h"
#include <cassert>
#include <climits>
#include <limits>
#include "ReditusQuestClock.h"
using namespace ReditusQuestRuntime;
Task TaskOf(const char* ID, Kind Type, const char* Target, int Goal=1, bool Optional=false)
{Task T;T.Id=ID;T.Type=Type;T.Target=Target;T.Required=Goal;T.Optional=Optional;return T;}
Definition QuestOf(const char* Key, std::vector<Task> Tasks)
{Definition D;D.Key=Key;Stage S;S.Id="First";S.Tasks=Tasks;D.Stages.push_back(S);return D;}
void Add(Engine& E,const Definition& D){std::string Error;assert(E.AddDefinition(D,Error));}
int main()
{
    Engine E;
    auto A=QuestOf("/Game/DT_Quests:Main",{TaskOf("Greet",Kind::Talk,"Guide")});
    Stage Second;Second.Id="Travel";Second.Tasks={TaskOf("Enter",Kind::ReachLocation,"Gate")};A.Stages.push_back(Second);
    auto B=QuestOf("/Game/DT_Quests:FollowUp",{TaskOf("Recruit",Kind::RecruitUnit,"Airloft")});
    B.Prerequisites={A.Key};B.HasRewards=true;Add(E,A);Add(E,B);
    assert(E.GetState(A.Key)==State::Available && E.GetState(B.Key)==State::Unavailable);
    assert(!E.Start(B.Key));assert(E.Start(A.Key));assert(!E.Start(A.Key));
    assert(!E.Signal(Kind::Talk,"Guide",1,""));assert(!E.Signal(Kind::Talk,"Wrong",1,"Wrong"));
    assert(E.Signal(Kind::Talk,"Guide",1,"Talk_Guide"));assert(E.FindRecord(A.Key)->StageId=="Travel");
    assert(!E.Signal(Kind::Talk,"Guide",1,"Talk_Guide"));assert(!E.Finish(A.Key));
    assert(E.Signal(Kind::ReachLocation,"Gate",1,"Entered_Gate"));
    assert(E.GetState(A.Key)==State::Ready && E.GetState(B.Key)==State::Unavailable);
    assert(E.Finish(A.Key));assert(!E.Finish(A.Key));assert(E.GetState(B.Key)==State::Available);
    assert(E.Start(B.Key));assert(E.Signal(Kind::RecruitUnit,"Airloft",1,"Recruit_Airloft"));
    assert(E.Finish(B.Key));assert(E.FindRecord(B.Key)->RewardsPending);
    assert(E.Acknowledge(B.Key));assert(!E.Acknowledge(B.Key));assert(!E.Start(B.Key,true));
    Engine Script;Add(Script,A);Add(Script,B);assert(Script.Start(B.Key,true));
    auto Repeat=QuestOf("Repeat",{TaskOf("One",Kind::Talk,"Same")});
    Stage Again;Again.Id="Again";Again.Tasks={TaskOf("Two",Kind::Talk,"Same")};Repeat.Stages.push_back(Again);
    Add(Script,Repeat);assert(Script.Start(Repeat.Key));
    assert(Script.Signal(Kind::Talk,"Same",1,"Unique"));assert(Script.GetState(Repeat.Key)==State::Active);
    assert(!Script.Signal(Kind::Talk,"Same",1,"Unique")); // No spill into the next stage.
    assert(Script.Signal(Kind::Talk,"Same",1,"Different"));
    Engine Hunting;
    auto Hunt=QuestOf("Hunt",{TaskOf("Kills",Kind::DefeatEnemy,"Boar",3),TaskOf("Bonus",Kind::Interact,"Chest",1,true)});
    Add(Hunting,Hunt);assert(Hunting.Start(Hunt.Key));
    assert(Hunting.Signal(Kind::DefeatEnemy,"Boar",1,"Kill1"));
    assert(!Hunting.Signal(Kind::DefeatEnemy,"Boar",1,"Kill1"));
    assert(!Hunting.Signal(Kind::DefeatEnemy,"Boar",-1,"Negative"));
    assert(Hunting.Signal(Kind::DefeatEnemy,"Boar",INT_MAX,"Kill2"));
    assert(Hunting.GetCount(Hunt.Key,"First","Kills")==3 && Hunting.GetState(Hunt.Key)==State::Ready);
    Engine Inventory;
    auto Items=QuestOf("Items",{TaskOf("Potion",Kind::CollectItem,"Potion",2)});
    Add(Inventory,Items);assert(Inventory.Start(Items.Key));
    assert(!Inventory.Signal(Kind::CollectItem,"Potion",2,"Pickup"));
    assert(Inventory.SetInventory(Items.Key,"Potion",2));assert(Inventory.GetState(Items.Key)==State::Ready);
    assert(Inventory.TakeMilestones().size()==1);
    assert(Inventory.TakeObjectiveMilestones().size()==1);
    assert(Inventory.SetInventory(Items.Key,"Potion",0));assert(Inventory.GetState(Items.Key)==State::Active);
    assert(!Inventory.Finish(Items.Key));assert(Inventory.SetInventory(Items.Key,"Potion",100));
    assert(Inventory.TakeMilestones().empty());assert(Inventory.TakeObjectiveMilestones().empty());assert(Inventory.GetCount(Items.Key,"First","Potion")==2);
    std::vector<Record> Saved;for(const auto& Pair:E.GetRecords())Saved.push_back(Pair.second);
    Engine Loaded;Add(Loaded,A);Add(Loaded,B);std::string Error;
    assert(Loaded.Import(Saved,Error));assert(Loaded.TakeMilestones().empty() && Loaded.TakeChanged().empty() && Loaded.TakeObjectiveMilestones().empty());
    assert(Loaded.GetState(B.Key)==State::Completed);
    assert(!Loaded.Signal(Kind::RecruitUnit,"Airloft",1,"Recruit_Airloft"));
    assert(!Loaded.Finish(B.Key));assert(!Loaded.Acknowledge(B.Key));
    auto Bad=Saved;Bad.push_back(Saved.front());assert(!Loaded.Import(Bad,Error));
    assert(Loaded.GetState(B.Key)==State::Completed);
    Bad=Saved;Bad[0].Version=99;assert(!Loaded.Import(Bad,Error));
    Bad=Saved;Bad[0].StageId="RemovedStage";assert(!Loaded.Import(Bad,Error));
    Bad=Saved;Bad[0].Counts["Unknown/Unknown"]=1;assert(!Loaded.Import(Bad,Error));
    Bad=Saved;Bad[0].Counts.begin()->second=-1;assert(!Loaded.Import(Bad,Error));
    Bad=Saved;Bad[0].Status=State::Unavailable;assert(!Loaded.Import(Bad,Error));
    auto Pending=QuestOf("Pending",{TaskOf("Event",Kind::Custom,"Event")});Pending.HasRewards=true;
    Engine P;Add(P,Pending);P.Start(Pending.Key);P.Signal(Kind::Custom,"Event",1,"Once");P.Finish(Pending.Key);
    Engine P2;Add(P2,Pending);assert(P2.Import({*P.FindRecord(Pending.Key)},Error));
    assert(P2.FindRecord(Pending.Key)->RewardsPending);assert(P2.Acknowledge(Pending.Key));assert(!P2.Acknowledge(Pending.Key));
    P2.Reset();assert(P2.GetRecords().empty());assert(P2.Start(Pending.Key));
    auto Other=QuestOf("/Game/DT_Side:Main",{TaskOf("Event",Kind::Custom,"Other")});
    Add(E,Other);assert(E.Start(Other.Key));assert(E.GetState(A.Key)==State::Completed);
    Definition Invalid=A;Invalid.Key="Invalid";Invalid.Stages[1].Id=Invalid.Stages[0].Id;
    assert(!E.AddDefinition(Invalid,Error));
    Invalid=A;Invalid.Key="OptionalOnly";Invalid.Stages[0].Tasks[0].Optional=true;assert(!E.AddDefinition(Invalid,Error));
    Invalid=A;Invalid.Key="BadGoal";Invalid.Stages[0].Tasks[0].Required=0;assert(!E.AddDefinition(Invalid,Error));
    Invalid=A;Invalid.Key="BadID";Invalid.Stages[0].Tasks[0].Id="Bad/ID";assert(!E.AddDefinition(Invalid,Error));
    Invalid=A;Invalid.Key="BadType";Invalid.Stages[0].Tasks[0].Type=static_cast<Kind>(99);assert(!E.AddDefinition(Invalid,Error));
    Invalid=A;Invalid.Key="Self";Invalid.Prerequisites={"Self"};assert(!E.AddDefinition(Invalid,Error));

    // Real timer engine, including save/reload and deadline cancellation by success.
    Engine Timers;
    auto Wait=QuestOf("Wait60",{TaskOf("Wait",Kind::TimerElapsed,"Timer")});
    Wait.Stages[0].Timer=TimerMode::Wait;Wait.Stages[0].Seconds=60;
    Wait.Stages.push_back(Second);Add(Timers,Wait);assert(Timers.Start(Wait.Key));
    assert(Timers.TakeEnteredStages().size()==1);
    assert(!Timers.Signal(Kind::TimerElapsed,"Timer",1,"FakeTimer"));
    assert(!Timers.Tick(30));assert(Timers.FindRecord(Wait.Key)->TimerRemaining==30);
    Engine Restored;Add(Restored,Wait);assert(Restored.Import({*Timers.FindRecord(Wait.Key)},Error));
    assert(Restored.TakeEnteredStages().empty());assert(Restored.FindRecord(Wait.Key)->TimerRemaining==30);
    assert(!Restored.Tick(-10));assert(!Restored.Tick(std::numeric_limits<double>::quiet_NaN()));
    assert(Restored.Tick(30));assert(Restored.FindRecord(Wait.Key)->StageId=="Travel");
    assert(Restored.GetState(Wait.Key)==State::Active);assert(Restored.GetCount(Wait.Key,"First","Wait")==1);
    assert(Restored.TakeEnteredStages().size()==1);assert(!Restored.Tick(999));
    auto Deadline=QuestOf("ReachWithin5Minutes",{TaskOf("Reach",Kind::ReachLocation,"Gate")});
    Deadline.Stages[0].Timer=TimerMode::Deadline;Deadline.Stages[0].Seconds=300;
    Add(Timers,Deadline);assert(Timers.Start(Deadline.Key));Timers.Tick(299);
    assert(Timers.FindRecord(Deadline.Key)->TimerRemaining==1);
    assert(Timers.Signal(Kind::ReachLocation,"Gate",1,"ReachedInTime"));
    assert(Timers.GetState(Deadline.Key)==State::Ready);Timers.Tick(20);
    assert(Timers.GetState(Deadline.Key)==State::Ready && Timers.FindRecord(Deadline.Key)->TimerRemaining==1);
    assert(Timers.Finish(Deadline.Key));assert(Timers.FindRecord(Deadline.Key)->TimerRemaining==0);
    Engine Failed;Add(Failed,Deadline);assert(Failed.Start(Deadline.Key));assert(Failed.Tick(301));
    assert(Failed.GetState(Deadline.Key)==State::Failed && Failed.TakeTimeouts().size()==1);
    assert(!Failed.Signal(Kind::ReachLocation,"Gate",1,"TooLate"));assert(!Failed.Finish(Deadline.Key));
    assert(!Failed.Tick(10));assert(Failed.TakeTimeouts().empty());
    Engine FailedLoad;Add(FailedLoad,Deadline);assert(FailedLoad.Import({*Failed.FindRecord(Deadline.Key)},Error));
    assert(FailedLoad.GetState(Deadline.Key)==State::Failed && FailedLoad.TakeTimeouts().empty());
    assert(Failed.RetryFailed(Deadline.Key));assert(Failed.GetState(Deadline.Key)==State::Active);
    assert(Failed.FindRecord(Deadline.Key)->TimerRemaining==300);assert(!Failed.RetryFailed(Deadline.Key));
    auto BadClock=*Failed.FindRecord(Deadline.Key);BadClock.TimerRemaining=-1;
    assert(!FailedLoad.Import({BadClock},Error));BadClock.TimerRemaining=301;assert(!FailedLoad.Import({BadClock},Error));
    BadClock.TimerRemaining=std::numeric_limits<double>::infinity();assert(!FailedLoad.Import({BadClock},Error));
    // Five distinct matching enemies within five minutes. Save/reload preserves
    // both partial kill progress and the same remaining deadline, not a new clock.
    auto TimedHunt=QuestOf("FiveBoarsInFiveMinutes",{TaskOf("Kills",Kind::DefeatEnemy,"DeathBoar",5)});
    TimedHunt.Stages[0].Timer=TimerMode::Deadline;TimedHunt.Stages[0].Seconds=300;
    Engine HuntClock;Add(HuntClock,TimedHunt);
    assert(!HuntClock.Signal(Kind::DefeatEnemy,"DeathBoar",1,"BeforeAcceptance"));
    assert(!HuntClock.FindRecord(TimedHunt.Key));assert(HuntClock.Start(TimedHunt.Key));
    assert(HuntClock.FindRecord(TimedHunt.Key)->TimerRemaining==300);
    assert(!HuntClock.Start(TimedHunt.Key));assert(!HuntClock.Finish(TimedHunt.Key));
    assert(!HuntClock.Signal(Kind::DefeatEnemy,"DifferentEnemy",1,"WrongTarget"));
    assert(HuntClock.Signal(Kind::DefeatEnemy,"DeathBoar",1,"Kill1"));
    assert(!HuntClock.Signal(Kind::DefeatEnemy,"DeathBoar",1,"Kill1"));
    HuntClock.Tick(50);assert(HuntClock.Signal(Kind::DefeatEnemy,"DeathBoar",1,"Kill2"));HuntClock.Tick(50);
    Engine HuntReload;Add(HuntReload,TimedHunt);assert(HuntReload.Import({*HuntClock.FindRecord(TimedHunt.Key)},Error));
    assert(HuntReload.GetCount(TimedHunt.Key,"First","Kills")==2&&HuntReload.FindRecord(TimedHunt.Key)->TimerRemaining==200);
    assert(!HuntReload.Signal(Kind::DefeatEnemy,"DeathBoar",1,"Kill1")); // Duplicate is remembered across save.
    assert(!HuntReload.Tick(50,{TimedHunt.Key})&&HuntReload.FindRecord(TimedHunt.Key)->TimerRemaining==200);
    assert(HuntReload.Signal(Kind::DefeatEnemy,"DeathBoar",1,"Kill3"));HuntReload.Tick(50);
    assert(HuntReload.Signal(Kind::DefeatEnemy,"DeathBoar",1,"Kill4"));HuntReload.Tick(149);
    assert(HuntReload.GetState(TimedHunt.Key)==State::Active&&HuntReload.FindRecord(TimedHunt.Key)->TimerRemaining==1);
    assert(!HuntReload.Finish(TimedHunt.Key));
    assert(HuntReload.Signal(Kind::DefeatEnemy,"DeathBoar",1,"Kill5"));
    assert(HuntReload.GetState(TimedHunt.Key)==State::Ready&&!HuntReload.Tick(900));
    assert(HuntReload.TakeTimeouts().empty()&&HuntReload.FindRecord(TimedHunt.Key)->TimerRemaining==1);
    Engine HuntReadyLoad;Add(HuntReadyLoad,TimedHunt);assert(HuntReadyLoad.Import({*HuntReload.FindRecord(TimedHunt.Key)},Error));
    assert(!HuntReadyLoad.Tick(900)&&HuntReadyLoad.GetState(TimedHunt.Key)==State::Ready);
    assert(HuntReadyLoad.Finish(TimedHunt.Key)&&!HuntReadyLoad.Finish(TimedHunt.Key));
    Engine HuntTimeout;Add(HuntTimeout,TimedHunt);assert(HuntTimeout.Start(TimedHunt.Key));
    for(int I=0;I<4;++I)assert(HuntTimeout.Signal(Kind::DefeatEnemy,"DeathBoar",1,"Enemy"+std::to_string(I)));
    assert(HuntTimeout.Tick(300)&&HuntTimeout.GetState(TimedHunt.Key)==State::Failed);
    assert(!HuntTimeout.Signal(Kind::DefeatEnemy,"DeathBoar",1,"LateFifthKill")&&!HuntTimeout.Finish(TimedHunt.Key));
    assert(HuntTimeout.TakeTimeouts().size()==1&&!HuntTimeout.Tick(1)&&HuntTimeout.TakeTimeouts().empty());
    Engine HuntFailedLoad;Add(HuntFailedLoad,TimedHunt);assert(HuntFailedLoad.Import({*HuntTimeout.FindRecord(TimedHunt.Key)},Error));
    assert(HuntFailedLoad.GetState(TimedHunt.Key)==State::Failed&&!HuntFailedLoad.Finish(TimedHunt.Key));
    // Concurrent quests keep independent remaining time and terminal states.
    auto ShortHunt=QuestOf("ShortHunt",{TaskOf("Kills",Kind::DefeatEnemy,"OtherMonster")});
    ShortHunt.Stages[0].Timer=TimerMode::Deadline;ShortHunt.Stages[0].Seconds=60;
    Engine Concurrent;Add(Concurrent,TimedHunt);Add(Concurrent,ShortHunt);
    assert(Concurrent.Start(TimedHunt.Key));Concurrent.Tick(10);assert(Concurrent.Start(ShortHunt.Key));
    Concurrent.Tick(20);
    assert(Concurrent.FindRecord(TimedHunt.Key)->TimerRemaining==270&&Concurrent.FindRecord(ShortHunt.Key)->TimerRemaining==40);
    Concurrent.Tick(40);
    assert(Concurrent.GetState(ShortHunt.Key)==State::Failed&&Concurrent.GetState(TimedHunt.Key)==State::Active);
    assert(Concurrent.FindRecord(TimedHunt.Key)->TimerRemaining==230&&Concurrent.TakeTimeouts().size()==1);
    for(int I=0;I<5;++I)assert(Concurrent.Signal(Kind::DefeatEnemy,"DeathBoar",1,"ConcurrentKill"+std::to_string(I)));
    assert(Concurrent.GetState(TimedHunt.Key)==State::Ready&&!Concurrent.Tick(999));
    assert(Concurrent.FindRecord(TimedHunt.Key)->TimerRemaining==230&&Concurrent.GetState(ShortHunt.Key)==State::Failed);
    assert(Concurrent.Finish(TimedHunt.Key)&&Concurrent.TakeTimeouts().empty());
    auto DeadlineItems=QuestOf("TimedItem",{TaskOf("Item",Kind::CollectItem,"Key")});
    DeadlineItems.Stages[0].Timer=TimerMode::Deadline;DeadlineItems.Stages[0].Seconds=60;
    Add(Failed,DeadlineItems);Failed.Start(DeadlineItems.Key);Failed.Tick(20);Failed.SetInventory(DeadlineItems.Key,"Item",1);
    Failed.Tick(100);assert(Failed.FindRecord(DeadlineItems.Key)->TimerRemaining==40);
    assert(Failed.SetInventory(DeadlineItems.Key,"Item",0));assert(Failed.Tick(40));
    assert(Failed.GetState(DeadlineItems.Key)==State::Failed);
    Invalid=Wait;Invalid.Key="MissingTimerTask";Invalid.Stages[0].Tasks[0].Type=Kind::Talk;
    assert(!Timers.AddDefinition(Invalid,Error));
    Invalid=Wait;Invalid.Key="ZeroDuration";Invalid.Stages[0].Seconds=0;assert(!Timers.AddDefinition(Invalid,Error));
    Invalid=Wait;Invalid.Key="InfiniteDuration";Invalid.Stages[0].Seconds=std::numeric_limits<double>::infinity();assert(!Timers.AddDefinition(Invalid,Error));
    assert(ReditusQuestClock::Display(60,false)=="1:00");assert(ReditusQuestClock::Display(300,false)=="5:00");
    assert(ReditusQuestClock::Display(59.2,false)=="1:00");assert(ReditusQuestClock::Display(59.2,true)=="0:59");
    assert(ReditusQuestClock::Display(0,true)=="0:00");assert(ReditusQuestClock::Display(60,true)=="1:00");
    assert(ReditusQuestClock::Display(-1,false)=="0:00");
    Engine Order;Add(Order,A);assert(Order.Start(A.Key));
    assert(Order.Signal(Kind::Talk,"Guide",1,"IntroTalk"));
    auto Events=Order.TakeStageEvents();assert(Events.size()==3);
    assert(Events[0].Type==StageEventKind::Enter && Events[0].StageId=="First");
    assert(Events[1].Type==StageEventKind::Complete && Events[1].StageId=="First");
    assert(Events[2].Type==StageEventKind::Enter && Events[2].StageId=="Travel");
    assert(Order.Signal(Kind::ReachLocation,"Gate",1,"OrderReach"));assert(Order.Finish(A.Key));
    Events=Order.TakeStageEvents();assert(Events.size()==2);
    assert(Events[0].Type==StageEventKind::Complete && Events[1].Type==StageEventKind::QuestComplete);
    Engine OrderLoad;Add(OrderLoad,A);assert(OrderLoad.Import({*Order.FindRecord(A.Key)},Error));
    assert(OrderLoad.TakeStageEvents().empty());
}
