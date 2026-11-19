#include "ReditusFetchQuestFlow.h"
#include "ReditusFetchQuestExchange.h"
#include <cassert>
#include <set>

int main()
{
    using namespace ReditusFetchQuestFlow;
    assert(Select(State::Available,false,false,false)==Phase::Offer);
    assert(Select(State::Available,true,false,false)==Phase::Ready); // Already owned, before accepting.
    assert(Select(State::Active,false,false,false)==Phase::Reminder);
    assert(Select(State::Active,true,false,false)==Phase::Ready);
    assert(Select(State::Ready,false,false,false)==Phase::Reminder); // Sold/lost the item.
    assert(Select(State::Ready,true,true,false)==Phase::Pending);
    assert(Select(State::Ready,true,false,true)==Phase::Error);
    assert(Select(State::Completed,true,true,true)==Phase::Completed); // No repeated hand-in.
    assert(Select(State::Unavailable,true,false,false)==Phase::Unavailable);
    assert(Select(State::Failed,true,false,false)==Phase::Failed);
    for(auto PhaseValue:{Phase::Offer,Phase::Reminder,Phase::Ready,Phase::Completed,Phase::Unavailable,Phase::Failed,Phase::Pending,Phase::Error})
    for(bool AllowDecline:{false,true})
    {
        const auto Nodes=Template(PhaseValue,AllowDecline);assert(Nodes.size()==13);
        assert(Nodes[0].Links==std::vector<int>{Entry(PhaseValue)});
        assert((Nodes[1].Links==std::vector<int>{2,3})); // Offer always permits Not now.
        assert(Nodes[5].Links==(AllowDecline?std::vector<int>{6,3}:std::vector<int>{6}));
        std::set<int> IDs;
        for(const auto& N:Nodes){assert(IDs.insert(N.ID).second);for(int Link:N.Links)assert(Link>=0&&Link<int(Nodes.size()));}
        assert(Nodes[3].OnEnter==Action::None&&Nodes[7].OnEnter==Action::None); // Decline never starts/pays.
        assert(Nodes[2].OnEnter==Action::Accept&&Nodes[6].OnEnter==Action::HandIn);
        for(int Result:Nodes[6].Links)assert(Nodes[Result].RequiredPhase>=0); // Exactly one conditional reply.
        assert(Nodes[8].Links.empty()&&Nodes[8].OnEnter==Action::None);
        for(int Result:Nodes[6].Links)assert(Nodes[Result].OnEnter!=Action::HandIn); // No automatic payout loop.
    }
    using ReditusFetchQuestExchange::Item; using ReditusFetchQuestExchange::Plan;
    std::vector<std::int32_t> After;std::int32_t Gold=0;
    assert(Plan({Item{1,1,0},Item{0,0,2}},100,500,After,Gold));
    assert((After==std::vector<std::int32_t>{0,2})&&Gold==600);
    assert(Plan({Item{3,2,4}},0,0,After,Gold)&&After[0]==5);
    const auto Before=After;const auto OldGold=Gold;
    assert(!Plan({Item{0,1,10}},0,0,After,Gold)); // Reward cannot finance missing cost.
    assert(!Plan({Item{1,1,0},Item{0,2,0}},0,0,After,Gold));
    assert(!Plan({Item{-1,0,1}},0,0,After,Gold));
    const auto Max=std::int64_t(std::numeric_limits<std::int32_t>::max());
    assert(!Plan({Item{Max,0,1}},0,0,After,Gold));
    assert(!Plan({},Max,1,After,Gold)); assert(!Plan({},-1,0,After,Gold));
    assert(!Plan({},0,-1,After,Gold));assert(!Plan({Item{0,-1,0}},0,0,After,Gold));
    assert(After==Before&&Gold==OldGold);
    assert(Plan({Item{Max,Max,Max}},0,Max,After,Gold)&&After[0]==Max&&Gold==Max);
}
