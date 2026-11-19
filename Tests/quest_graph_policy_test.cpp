#include "ReditusQuestGraphPolicy.h"
#include "ReditusQuestRuntime.h"
#include <cassert>
#include <random>

using namespace ReditusQuestGraphPolicy;

int main()
{
    std::string Error; Plan P;
    const std::vector<Node> Branch{
        {Kind::Start,"",{{"","",1}}},
        {Kind::Stage,"decision",{{"","",4},{"decision","help",2},{"decision","refuse",3}}},
        {Kind::Stage,"help",{{"","",4}}},
        {Kind::Stage,"refuse",{{"","",4}}},
        {Kind::Stage,"return",{{"","",5}}},
        {Kind::Finish,"",{}}
    };
    assert(Compile(Branch,P,Error));
    std::vector<std::string> Expected;
    for(int I:P.Stages)Expected.push_back(Branch[unsigned(I)].ID);
    assert((Expected==std::vector<std::string>{"decision","help","refuse","return"}));
    // Graph array order and canvas positions cannot change the compiled branch order.
    std::mt19937 Random(1047);
    for(int Trial=0;Trial<100;++Trial)
    {
        std::vector<int> Permutation{0,1,2,3,4,5};std::shuffle(Permutation.begin(),Permutation.end(),Random);
        auto Copy=Branch;
        for(unsigned I=0;I<Permutation.size();++I)
        {
            Copy[I]=Branch[unsigned(Permutation[I])];
            for(auto& E:Copy[I].Exits)E.Target=int(std::find(Permutation.begin(),Permutation.end(),E.Target)-Permutation.begin());
        }
        assert(Compile(Copy,P,Error));std::vector<std::string> Actual;
        for(int I:P.Stages)Actual.push_back(Copy[unsigned(I)].ID);
        assert(Actual==Expected);
    }
    auto Reject=[&](std::vector<Node> Bad){assert(!Compile(Bad,P,Error));assert(P.Stages.empty()&&!Error.empty());};
    Reject({});auto Bad=Branch;Bad[0].Type=Kind::Finish;Reject(Bad);
    Bad=Branch;Bad[5].Type=Kind::Stage;Reject(Bad);
    Bad=Branch;Bad[1].ID="";Reject(Bad);Bad[1].ID="bad/id";Reject(Bad);
    Bad=Branch;Bad[2].ID=Bad[1].ID;Reject(Bad);
    Bad=Branch;Bad[1].Exits[0].Target=-1;Reject(Bad);Bad[1].Exits[0].Target=99;Reject(Bad);
    Bad=Branch;Bad[1].Exits[0].Target=1;Reject(Bad);Bad[1].Exits[0].Target=0;Reject(Bad);
    Bad=Branch;Bad[0].Exits.clear();Reject(Bad);
    Bad=Branch;Bad[0].Exits[0].Target=5;Reject(Bad);
    Bad=Branch;Bad[5].Exits.push_back({"","",1});Reject(Bad);
    Bad=Branch;Bad[1].Exits.push_back({"","",4});Reject(Bad);
    Bad=Branch;Bad[1].Exits.push_back(Bad[1].Exits[1]);Reject(Bad);
    Bad=Branch;Bad[1].Exits[1].Option.clear();Reject(Bad);
    Bad=Branch;Bad[1].Exits[1].Target=3;Reject(Bad); // Help node is unreachable.
    Bad=Branch;Bad[4].Exits[0].Target=2;Reject(Bad); // A cycle.

    // Execute the compiler's order/routes with the real quest runtime, and save mid-branch.
    assert(Compile(Branch,P,Error));
    using namespace ReditusQuestRuntime;
    Definition D;D.Key="catalog:hunt";D.HasRewards=true;
    for(int I:P.Stages)
    {
        Stage S;S.Id=Branch[unsigned(I)].ID;
        Task T;T.Id="objective";T.Target=S.Id;
        T.Type=S.Id=="decision"?ReditusQuestRuntime::Kind::Choice:ReditusQuestRuntime::Kind::Custom;
        S.Tasks.push_back(T);
        if(S.Id=="decision")S.Choices.push_back({"decision",{{"help",0},{"refuse",1}}});
        for(const auto& E:Branch[unsigned(I)].Exits)
        {
            const auto& Target=Branch[unsigned(E.Target)];const bool Finish=Target.Type==ReditusQuestGraphPolicy::Kind::Finish;
            if(E.Choice.empty()){S.Finish=Finish;S.Next=Finish?"":Target.ID;}
            else S.Routes.push_back({E.Choice,E.Option,Finish?"":Target.ID,Finish});
        }
        D.Stages.push_back(S);
    }
    for(const std::string Option:{"help","refuse"})
    {
        Engine Runtime;assert(Runtime.AddDefinition(D,Error));assert(Runtime.Start(D.Key));
        assert(Runtime.Choose(D.Key,"decision",Option));assert(Runtime.FindRecord(D.Key)->StageId==Option);
        Engine Loaded;assert(Loaded.AddDefinition(D,Error));assert(Loaded.Import({*Runtime.FindRecord(D.Key)},Error));
        assert(Loaded.Signal(ReditusQuestRuntime::Kind::Custom,Option,1,"first"));
        assert(Loaded.FindRecord(D.Key)->StageId=="return");
        assert(Loaded.Signal(ReditusQuestRuntime::Kind::Custom,"return",1,"second"));
        assert(Loaded.GetState(D.Key)==State::Ready); // No reward just for arriving here.
        assert(Loaded.Finish(D.Key));assert(!Loaded.Finish(D.Key));
    }
}
