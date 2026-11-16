#pragma once
#include <cstdint>
#include <limits>
#include <vector>

namespace ReditusFetchQuestExchange
{
    struct Item { std::int64_t Owned = 0, Cost = 0, Reward = 0; };
    // Costs must be owned before rewards, even when they refer to the same item.
    // A failed preflight leaves all output balances untouched.
    inline bool Plan(const std::vector<Item>& Items, std::int64_t Gold, std::int64_t GoldReward,
        std::vector<std::int32_t>& After, std::int32_t& GoldAfter)
    {
        const auto Max = std::int64_t(std::numeric_limits<std::int32_t>::max());
        if (Gold < 0 || Gold > Max || GoldReward < 0 || GoldReward > Max || Gold + GoldReward > Max) return false;
        std::vector<std::int32_t> Candidate;
        for (const auto& Item : Items)
        {
            if (Item.Owned < 0 || Item.Owned > Max || Item.Cost < 0 || Item.Cost > Max
                || Item.Reward < 0 || Item.Reward > Max || Item.Cost > Item.Owned
                || Item.Owned - Item.Cost + Item.Reward > Max) return false;
            Candidate.push_back(std::int32_t(Item.Owned - Item.Cost + Item.Reward));
        }
        After = Candidate; GoldAfter = std::int32_t(Gold + GoldReward); return true;
    }
}
