#pragma once
#include <vector>
#include <cstddef>

namespace ReditusQuestRewardText
{
    enum class State { Offered, Received, NotReceived };
    enum class Kind { Gold, Experience, Item };
    struct Entry { Kind Type; int Amount; std::size_t ItemIndex; };
    struct Plan { State Status; std::vector<Entry> Entries; };
    inline Plan Build(int Gold, int Experience, const std::vector<int>& ItemAmounts, State Status)
    {
        Plan Result; Result.Status = Status;
        if (Gold > 0) Result.Entries.push_back({Kind::Gold, Gold, 0});
        if (Experience > 0) Result.Entries.push_back({Kind::Experience, Experience, 0});
        for (std::size_t I = 0; I < ItemAmounts.size(); ++I)
            if (ItemAmounts[I] > 0) Result.Entries.push_back({Kind::Item, ItemAmounts[I], I});
        return Result;
    }
}
