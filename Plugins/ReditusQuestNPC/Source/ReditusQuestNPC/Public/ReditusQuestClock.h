#pragma once
#include <algorithm>
#include <cmath>
#include <string>

namespace ReditusQuestClock
{
    inline std::string Display(double Seconds, bool CountUp)
    {
        if (!std::isfinite(Seconds)) Seconds = 0;
        Seconds = std::max(0.0, std::min(31536000.0, Seconds));
        const int Whole = int(CountUp ? std::floor(Seconds) : std::ceil(Seconds));
        const int Remainder = Whole % 60;
        return std::to_string(Whole / 60) + ":" + (Remainder < 10 ? "0" : "") + std::to_string(Remainder);
    }
}
