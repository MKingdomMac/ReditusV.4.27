#pragma once

#include <algorithm>
#include <cmath>

// No engine dependencies: target health and the live counter are separate.
// Damage/healing change Target; only the live counter determines death.
namespace ReditusRollingHP
{
    inline double Positive(double Value)
    { return std::isfinite(Value) && Value>0.0 ? Value : 0.0; }
    inline double Clamp(double Value,double Maximum)
    { return std::min(Positive(Maximum),Positive(Value)); }

    struct Settings
    {
        double DamageFullBarSeconds=3.0;
        double HealingFullBarSeconds=0.6;
        double MinimumLethalDrainSeconds=1.0;
    };

    class Meter
    {
    public:
        void Reset(double Health,double Maximum)
        {
            Max=Positive(Maximum); Live=Target=Clamp(Health,Max);
            DamageRate=0.0; WasLethal=false;
        }
        void SetMaximum(double Maximum)
        { Max=Positive(Maximum); Live=Clamp(Live,Max); Target=Clamp(Target,Max); }
        double Counter() const { return Live; }
        double Destination() const { return Target; }
        bool IsDead() const { return Live<=0.0; }
        bool IsFalling() const { return Live>Target; }
        bool IsRising() const { return Live<Target; }
        bool IsLethal() const { return Target<=0.0 && Live>0.0; }
        int CounterInteger() const
        {
            // A positive fractional counter must not publish 0 and kill early.
            return static_cast<int>(std::ceil(Live));
        }
        void SetTarget(double Health,const Settings& Tuning)
        {
            Target=Clamp(Health,Max);
            // Extra hits while already falling do not restart a rescue window.
            if (IsFalling() && DamageRate<=0.0)
            {
                const double Seconds=Positive(Tuning.DamageFullBarSeconds);
                DamageRate=Seconds>0.0 ? Max/Seconds : Max;
            }
            if (IsLethal() && !WasLethal)
            {
                const double Minimum=Positive(Tuning.MinimumLethalDrainSeconds);
                if (Minimum>0.0) DamageRate=std::min(DamageRate,Live/Minimum);
            }
            WasLethal=IsLethal();
            if (!IsFalling()) DamageRate=0.0;
        }
        void Advance(double Seconds,const Settings& Tuning)
        {
            Seconds=Positive(Seconds);
            if (Seconds<=0.0) return;
            if (IsFalling())
            {
                if (DamageRate<=0.0)
                {
                    const double Duration=Positive(Tuning.DamageFullBarSeconds);
                    DamageRate=Duration>0.0 ? Max/Duration : Max;
                }
                Live=std::max(Target,Live-DamageRate*Seconds);
            }
            else if (IsRising())
            {
                const double Duration=Positive(Tuning.HealingFullBarSeconds);
                Live=Duration>0.0 ? std::min(Target,Live+(Max/Duration)*Seconds) : Target;
            }
            if (!IsFalling()) DamageRate=0.0;
            WasLethal=IsLethal();
        }
        void FinishBattle()
        {
            // Stop unprocessed damage at victory/flee; finish pending healing.
            Live=Target=std::max(Live,Target); DamageRate=0.0; WasLethal=false;
        }
    private:
        double Live=0.0,Target=0.0,Max=0.0,DamageRate=0.0;
        bool WasLethal=false;
    };
}
