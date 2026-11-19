#include "ReditusQuestRuntime.h"
#include <cassert>
#include <string>
using namespace ReditusQuestRuntime;
static Task Objective(const char* Id, Kind Type, const char* Target) { Task T; T.Id=Id; T.Type=Type; T.Target=Target; return T; }
static void Add(Engine& E, const Definition& D) { std::string Error; assert(E.AddDefinition(D,Error)); }
static Definition Extortion()
{
    Definition D; D.Key="Extortion";
    Stage Letter; Letter.Id="Letter"; Letter.Actions={"GiveLetter"}; Letter.Tasks={Objective("Receive",Kind::StepAction,"GiveLetter")};
    Stage Bargain; Bargain.Id="Bargain"; Bargain.Tasks={Objective("Agree",Kind::Choice,"Payment")};
    Choice C; C.Id="Payment";
    for (int Amount : {1,1500,1750,2000,2500}) { ChoiceOption O; O.Id=std::to_string(Amount); O.Value=Amount; C.Options.push_back(O); }
    Bargain.Choices={C};
    Stage Handoff; Handoff.Id="Exchange"; Handoff.Actions={"ExchangeLetter"}; Handoff.Tasks={Objective("Paid",Kind::StepAction,"ExchangeLetter")};
    Route Low; Low.ChoiceId="Payment"; Low.OptionId="1"; Low.Next="LowOutcome"; Handoff.Routes={Low};
    Stage Normal; Normal.Id="NormalOutcome"; Normal.Tasks={Objective("Back",Kind::Talk,"Wedge")}; Normal.Finish=true;
    Stage LowOutcome=Normal; LowOutcome.Id="LowOutcome";
    D.Stages={Letter,Bargain,Handoff,Normal,LowOutcome}; return D;
}
static void RoundTrip(Engine& E, const Definition& D)
{
    Engine Loaded; Add(Loaded,D); std::string Error;
    assert(Loaded.Import({*E.FindRecord(D.Key)},Error));
    assert(Loaded.TakeChanged().empty() && Loaded.TakeStageEvents().empty() && Loaded.TakeMilestones().empty() && Loaded.TakeObjectiveMilestones().empty());
    assert(Loaded.FindRecord(D.Key)->Path == E.FindRecord(D.Key)->Path);
    assert(Loaded.FindRecord(D.Key)->Actions == E.FindRecord(D.Key)->Actions);
    E=Loaded;
}
int main()
{
    const auto D=Extortion(); std::string Error;
    for (int Amount : {1,1500,1750,2000,2500})
    {
        Engine E; Add(E,D); assert(E.Start(D.Key)); RoundTrip(E,D);
        assert(!E.Choose(D.Key,"Payment",std::to_string(Amount))); // Wrong stage.
        assert(!E.Signal(Kind::StepAction,"GiveLetter",1,"ForgedGift"));
        assert(E.ApplyAction(D.Key,"Letter","GiveLetter")); RoundTrip(E,D);
        assert(!E.ApplyAction(D.Key,"Letter","GiveLetter"));
        assert(!E.Choose(D.Key,"Payment","Unknown")); assert(E.FindRecord(D.Key)->Choices.empty());
        assert(!E.Signal(Kind::Choice,"Payment",1,"Bypass"));
        assert(E.Choose(D.Key,"Payment",std::to_string(Amount)));
        assert(E.FindRecord(D.Key)->Choices.at("Payment").Value==Amount); RoundTrip(E,D);
        assert(!E.Choose(D.Key,"Payment","2500"));
        assert(!E.ApplyAction(D.Key,"Bargain","ExchangeLetter"));
        assert(E.CanApplyAction(D.Key,"Exchange","ExchangeLetter"));
        assert(E.ApplyAction(D.Key,"Exchange","ExchangeLetter")); RoundTrip(E,D);
        const std::string Outcome=Amount==1 ? "LowOutcome" : "NormalOutcome";
        assert(E.FindRecord(D.Key)->StageId==Outcome);
        assert(E.RelevantStages(D.Key).size()==4); // Other branch cannot reduce progress.
        assert(!E.ApplyAction(D.Key,"Exchange","ExchangeLetter"));
        assert(E.Signal(Kind::Talk,"Wedge",1,"ReturnToWedge")); RoundTrip(E,D);
        assert(E.GetState(D.Key)==State::Ready && E.Finish(D.Key)); RoundTrip(E,D);
        assert(E.GetState(D.Key)==State::Completed);
        assert(E.FindRecord(D.Key)->Actions.size()==2);
        auto Bad=*E.FindRecord(D.Key); Bad.Choices["Payment"].Value=999999;
        assert(!E.Import({Bad},Error)); assert(E.GetState(D.Key)==State::Completed);
        Bad=*E.FindRecord(D.Key); Bad.Actions.erase("ExchangeLetter"); assert(!E.Import({Bad},Error));
        Bad=*E.FindRecord(D.Key); Bad.Path[3]=Amount==1?"NormalOutcome":"LowOutcome"; Bad.StageId=Bad.Path[3]; assert(!E.Import({Bad},Error));
        Bad=*E.FindRecord(D.Key); Bad.Path.insert(Bad.Path.begin()+2,"NormalOutcome"); assert(!E.Import({Bad},Error));
        Bad=*E.FindRecord(D.Key); Bad.Path.clear(); assert(!E.Import({Bad},Error));
        Bad=*E.FindRecord(D.Key); Bad.Actions.insert("UnknownGift"); assert(!E.Import({Bad},Error));
        Bad=*E.FindRecord(D.Key); Bad.Choices["NotAuthored"]=Selection{}; assert(!E.Import({Bad},Error));
    }
    Engine Reject;
    auto Bad=D; Bad.Key="Backwards"; Bad.Stages[2].Routes[0].Next="Letter"; assert(!Reject.AddDefinition(Bad,Error));
    Bad=D; Bad.Key="UnknownChoice"; Bad.Stages[2].Routes[0].ChoiceId="Missing"; assert(!Reject.AddDefinition(Bad,Error));
    Bad=D; Bad.Key="UnknownOption"; Bad.Stages[2].Routes[0].OptionId="Missing"; assert(!Reject.AddDefinition(Bad,Error));
    Bad=D; Bad.Key="FutureChoice"; Bad.Stages[0].Routes=Bad.Stages[2].Routes; assert(!Reject.AddDefinition(Bad,Error));
    Bad=D; Bad.Key="DuplicateChoice"; Bad.Stages[3].Choices=Bad.Stages[1].Choices; assert(!Reject.AddDefinition(Bad,Error));
    Bad=D; Bad.Key="OptionalAction"; Bad.Stages[0].Tasks[0].Optional=true; assert(!Reject.AddDefinition(Bad,Error));
    Bad=D; Bad.Key="NoActionObjective"; Bad.Stages[2].Tasks[0].Target="Other"; assert(!Reject.AddDefinition(Bad,Error));
    Bad=D; Bad.Key="ConflictingEnd"; Bad.Stages[3].Next="LowOutcome"; assert(!Reject.AddDefinition(Bad,Error));
    // Handoff waits may span a deadline; retry never rewinds an earlier gift.
    auto Timed=D; Timed.Key="Timed"; Timed.Stages[2].Timer=TimerMode::Deadline; Timed.Stages[2].Seconds=5;
    Engine E; Add(E,Timed); E.Start(Timed.Key); E.ApplyAction(Timed.Key,"Letter","GiveLetter"); E.Choose(Timed.Key,"Payment","2000");
    assert(!E.Tick(10,{Timed.Key})); assert(E.FindRecord(Timed.Key)->TimerRemaining==5);
    assert(E.Tick(5)); assert(E.GetState(Timed.Key)==State::Failed); RoundTrip(E,Timed);
    assert(E.RetryFailed(Timed.Key)); assert(E.FindRecord(Timed.Key)->StageId=="Exchange");
    assert(E.FindRecord(Timed.Key)->Choices.at("Payment").Value==2000);
    assert(!E.ApplyAction(Timed.Key,"Letter","GiveLetter"));
    assert(E.ApplyAction(Timed.Key,"Exchange","ExchangeLetter")); RoundTrip(E,Timed);
    // Two remembered decisions, and another quest with the same IDs, stay independent.
    auto Multiple=D; Multiple.Key="Multiple";
    Choice Plan; Plan.Id="Plan"; ChoiceOption Mercy; Mercy.Id="Mercy"; Mercy.Value=7; Plan.Options={Mercy};
    Multiple.Stages[3].Choices={Plan}; Multiple.Stages[3].Tasks.push_back(Objective("Plan",Kind::Choice,"Plan"));
    Engine Two; Add(Two,Multiple); auto Separate=Multiple; Separate.Key="Separate"; Add(Two,Separate);
    Two.Start(Multiple.Key); Two.Start(Separate.Key);
    Two.ApplyAction(Multiple.Key,"Letter","GiveLetter"); Two.Choose(Multiple.Key,"Payment","2000");
    assert(Two.FindRecord(Separate.Key)->StageId=="Letter" && Two.FindRecord(Separate.Key)->Choices.empty());
    Two.ApplyAction(Multiple.Key,"Exchange","ExchangeLetter");
    assert(Two.Choose(Multiple.Key,"Plan","Mercy"));
    assert(Two.GetState(Multiple.Key)==State::Active); // Still needs the Talk objective.
    // RoundTrip only this quest, so its copy deliberately does not contain Separate.
    RoundTrip(Two,Multiple); assert(Two.FindRecord(Multiple.Key)->Choices.size()==2);
    assert(!Two.Choose(Multiple.Key,"Plan","Mercy"));
    assert(Two.Signal(Kind::Talk,"Wedge",1,"TalkAfterPlan")); assert(Two.GetState(Multiple.Key)==State::Ready);
    // Existing linear saves still load with no path field.
    Definition Linear; Linear.Key="Legacy"; Stage One; One.Id="One"; One.Tasks={Objective("Hi",Kind::Talk,"NPC")}; Linear.Stages={One};
    Engine Old; Add(Old,Linear); Old.Start(Linear.Key); auto Saved=*Old.FindRecord(Linear.Key); Saved.Path.clear(); assert(Old.Import({Saved},Error));
    assert(Old.FindRecord(Linear.Key)->Path.size()==1);
}
