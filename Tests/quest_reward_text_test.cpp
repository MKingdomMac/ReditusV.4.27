#include "ReditusQuestRewardText.h"
#include <cassert>
#include <climits>
int main()
{
    using namespace ReditusQuestRewardText;
    const auto Offered = Build(500, 0, {}, State::Offered);
    assert(Offered.Status == State::Offered && Offered.Entries.size() == 1);
    assert(Offered.Entries[0].Type == Kind::Gold && Offered.Entries[0].Amount == 500);
    assert(Build(500, 0, {}, State::Received).Status == State::Received);
    assert(Build(500, 0, {}, State::NotReceived).Status == State::NotReceived);
    assert(Build(0, 0, {}, State::Offered).Entries.empty());
    assert(Build(-1, -1, {0, -1}, State::Received).Entries.empty());
    const std::vector<int> Items = {2, 0, 1};
    const auto Mixed = Build(2500, 100, Items, State::Offered);
    assert(Mixed.Entries.size() == 4 && Mixed.Entries[1].Type == Kind::Experience);
    assert(Mixed.Entries[2].Type == Kind::Item && Mixed.Entries[2].Amount == 2 && Mixed.Entries[2].ItemIndex == 0);
    assert(Mixed.Entries[3].ItemIndex == 2 && Mixed.Entries[3].Amount == 1);
    const auto Large = Build(INT_MAX, INT_MAX, {INT_MAX}, State::Received);
    assert(Large.Entries.size() == 3 && Large.Entries[2].Amount == INT_MAX);
    for (int I = 0; I < 1000; ++I) assert(Build(500, 0, Items, State::Offered).Status == State::Offered);
    assert(Items[0] == 2 && Items[1] == 0 && Items[2] == 1);
}
