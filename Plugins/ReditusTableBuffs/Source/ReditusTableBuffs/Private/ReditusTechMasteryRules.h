#pragma once

namespace ReditusTechMasteryRules
{
    inline int ResolveEnemyReward(int Override, bool bBoss)
    {
        return Override > 0 ? Override : bBoss ? 2 : 1;
    }

    inline int IncludeEnemyReward(int Current, int Enemy)
    {
        return Enemy > Current ? Enemy : Current;
    }

    // Clamp stored progress and stop at the requirement without overflowing on large rewards.
    inline int Advance(int Previous, int Award, int Required)
    {
        if (Award <= 0 || Required <= 0) return Previous;
        const int Progress = Previous < 0 ? 0 : Previous > Required ? Required : Previous;
        return Award >= Required - Progress ? Required : Progress + Award;
    }
}
