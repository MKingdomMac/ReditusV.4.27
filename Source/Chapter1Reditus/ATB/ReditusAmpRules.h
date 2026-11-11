#pragma once

namespace ReditusAmpRules
{
    // Capacity is fixed per unit. Spending changes available AMP only.
    inline int Award(int Available, int Gain, int Capacity)
    {
        if (Capacity <= 0) return 0;
        const int Current = Available < 0 ? 0 : (Available > Capacity ? Capacity : Available);
        if (Gain <= 0) return Current;
        return Gain >= Capacity - Current ? Capacity : Current + Gain;
    }
}
