#include "ReditusQuestWorldRuntime.h"
#include "ReditusQuestRuntime.h"
#include <cassert>
#include <vector>
using namespace ReditusQuestWorld;
int main()
{
    State S; assert(!S.IsRemoved("gate_robot")); assert(!S.MarkRemoved("", Removal::Removed));
    assert(S.MarkRemoved("gate_robot", Removal::Recruited)); assert(S.IsRemoved("gate_robot"));
    assert(S.MarkRemoved("gate_robot", Removal::Defeated));
    assert(S.GetRemoved().at("gate_robot")==Removal::Recruited);
    assert(S.MarkRemoved("bandit_captain", Removal::Defeated));
    assert(!S.IsRemoved("another_robot"));assert(!S.MarkRemoved("bad",static_cast<Removal>(44)));
    assert(!S.RecordRecruitment("")); assert(S.RecordRecruitment("heremusrover")); assert(!S.RecordRecruitment("heremusrover"));
    std::vector<RemovedNPC> Saved;for(const auto& P:S.GetRemoved()){RemovedNPC N;N.Id=P.first;N.Reason=P.second;Saved.push_back(N);}
    std::vector<std::string> Recruits(S.GetRecruitments().begin(),S.GetRecruitments().end());
    State Loaded;std::string Error;assert(Loaded.Import(Saved,Recruits,Error));
    assert(Loaded.IsRemoved("gate_robot") && Loaded.HasRecruitment("heremusrover"));
    assert(!Loaded.RecordRecruitment("heremusrover")); // Save/reload cannot re-grant a recruitment.
    auto Bad=Saved;Bad.push_back(Saved.front());assert(!Loaded.Import(Bad,Recruits,Error));
    assert(Loaded.IsRemoved("gate_robot") && Loaded.HasRecruitment("heremusrover"));
    Bad=Saved;Bad[0].Id="";assert(!Loaded.Import(Bad,Recruits,Error));
    Bad=Saved;Bad[0].Reason=static_cast<Removal>(99);assert(!Loaded.Import(Bad,Recruits,Error));
    assert(!Loaded.Import(Saved,{"heremusrover","heremusrover"},Error));
    assert(!Loaded.Import(Saved,{""},Error));
    assert(Loaded.Import({}, {}, Error));assert(!Loaded.IsRemoved("gate_robot") && !Loaded.HasRecruitment("heremusrover"));
    S.Reset(); assert(S.GetRemoved().empty() && S.GetRecruitments().empty());
    // Winning an encounter is a distinct objective from killing one combat actor.
    using namespace ReditusQuestRuntime;
    Engine E;Definition D;D.Key="RobotQuest";Stage StageOne;StageOne.Id="Recruit";
    Task T;T.Id="RecruitRover";T.Type=Kind::RecruitUnit;T.Target="heremusrover";StageOne.Tasks.push_back(T);D.Stages.push_back(StageOne);
    assert(E.AddDefinition(D,Error));assert(E.Start(D.Key));
    if(S.RecordRecruitment("heremusrover"))assert(E.Signal(Kind::RecruitUnit,"heremusrover",1,"Recruit:Rover"));
    assert(E.GetState(D.Key)==ReditusQuestRuntime::State::Ready);
    assert(!S.RecordRecruitment("heremusrover"));assert(!E.Signal(Kind::RecruitUnit,"heremusrover",1,"Recruit:Rover"));
    D.Key="Challenge";D.Stages[0].Id="Battle";D.Stages[0].Tasks[0].Id="Win";D.Stages[0].Tasks[0].Type=Kind::WinBattle;D.Stages[0].Tasks[0].Target="captain_encounter";
    assert(E.AddDefinition(D,Error));assert(E.Start(D.Key));
    assert(!E.Signal(Kind::DefeatEnemy,"captain_encounter",1,"combat_actor_death"));
    assert(E.GetState(D.Key)==ReditusQuestRuntime::State::Active);
    assert(E.Signal(Kind::WinBattle,"captain_encounter",1,"confirmed_encounter_victory"));
    assert(!E.Signal(Kind::WinBattle,"captain_encounter",1,"confirmed_encounter_victory"));
}
