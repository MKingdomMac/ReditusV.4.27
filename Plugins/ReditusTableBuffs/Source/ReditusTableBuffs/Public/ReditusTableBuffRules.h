#pragma once

#include <cmath>
#include <limits>

namespace ReditusTableBuffRules
{
    inline bool ValidGuardUp(double Multiplier, int Cycles)
    {
        return std::isfinite(Multiplier) && Multiplier >= 1.0 && Multiplier <= 10.0 &&
            Cycles >= 1 && Cycles <= 99;
    }

    inline int DefenseWithGuardUp(int BaseDefense, double Multiplier)
    {
        if (BaseDefense <= 0 || !ValidGuardUp(Multiplier, 1))
            return BaseDefense;
        const double Scaled = BaseDefense * Multiplier;
        const int Maximum = (std::numeric_limits<int>::max)();
        return Scaled >= Maximum ? Maximum : static_cast<int>(Scaled);
    }

    inline bool ValidEffectScale(double Multiplier)
    {
        return std::isfinite(Multiplier) && Multiplier >= static_cast<double>(0.1f) && Multiplier <= 10.0;
    }

    inline int ScaleDefense(int BaseDefense, double Delta)
    {
        if (BaseDefense <= 0 || !std::isfinite(Delta)) return BaseDefense;
        // Ignore float authoring noise (for example, 0.7f at defense 100).
        const double AuthoredDelta = std::round(Delta * 1000000.0) / 1000000.0;
        const double Scaled = std::nextafter(BaseDefense * (1.0 + AuthoredDelta),
            (std::numeric_limits<double>::infinity)());
        const int Maximum = (std::numeric_limits<int>::max)();
        return Scaled >= Maximum ? Maximum : (Scaled <= 0.0 ? 0 : static_cast<int>(Scaled));
    }

    inline float PeriodicDamageFraction(float Percent)
    {
        return std::isfinite(Percent) && Percent >= 0.1f && Percent <= 100.f ? Percent / 100.f : 0.f;
    }

    enum class EOwnerEnd { Ignored, CastingCycle, Advanced, Expired };

    // The pilot self-casts during its own ATB cycle. That cycle does not
    // consume one of the future full cycles. A refresh follows the same rule.
    struct FGuardUpLifetime
    {
        int Remaining = 0;
        int LastEndToken = 0;
        bool bSkipCastingCycle = false;
        int SkipEndToken = 0;

        void StartOrRefresh(int Cycles)
        {
            if (Remaining <= 0) LastEndToken = 0;
            Remaining = Cycles;
            bSkipCastingCycle = true;
            SkipEndToken = 0;
        }

        void StartOrRefreshForTarget(int Cycles, int InProgressToken)
        {
            if (Remaining <= 0) LastEndToken = 0;
            Remaining = Cycles;
            bSkipCastingCycle = false;
            SkipEndToken = InProgressToken;
        }

        EOwnerEnd FinishOwnerCycle(int Token)
        {
            if (Remaining <= 0 || Token <= LastEndToken) return EOwnerEnd::Ignored;
            LastEndToken = Token;
            if (bSkipCastingCycle || Token == SkipEndToken)
            {
                bSkipCastingCycle = false;
                SkipEndToken = 0;
                return EOwnerEnd::CastingCycle;
            }
            return --Remaining == 0 ? EOwnerEnd::Expired : EOwnerEnd::Advanced;
        }
    };
}
