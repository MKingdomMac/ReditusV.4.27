#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

// Portable scheduler used by the Unreal component and by the executable tests.
// No actor, UI, damage, status-effect or resource implementation lives here.
namespace ReditusATBCore
{
    enum class Phase : std::uint8_t
    { Filling, CycleStart, Ready, Preparing, Queued, Executing, CycleEnd, Inactive };
    enum class WorkKind : std::uint8_t { None, CycleStart, Action, CycleEnd };
    struct Work
    {
        WorkKind Kind = WorkKind::None;
        int Unit = -1;
        int Cycle = -1;
        int Action = -1;
    };
    struct Unit
    {
        int ID = -1;
        int Cycle = -1;
        int CompletedCycles = 0;
        int Action = -1;
        double Speed = 0.0;
        double Gauge = 0.0;
        double EligibleAt = 0.0;
        double PreparationTotal = 0.0;
        double PreparationRemaining = 0.0;
        double GaugeMultiplier = 1.0;
        double PreparationMultiplier = 1.0;
        bool PauseGauge = false;
        bool PausePreparation = false;
        bool StartResolved = false;
        bool ReadyAmpClaimed = false;
        Phase State = Phase::Filling;
    };
    struct Settings
    {
        double BaseFillSeconds = 5.0;
        double ReferenceSpeed = 20.0;
        double FillRateMultiplier = 1.0;
        double RefillGauge = 0.0;
        bool ContinueFillingDuringExecution = true;
        bool ContinuePreparingDuringExecution = true;
    };
    inline double NonNegative(double Value)
    { return std::isfinite(Value) && Value > 0.0 ? Value : 0.0; }
    inline double Fraction(double Value)
    { return std::min(1.0, NonNegative(Value)); }

    struct TimingEffect
    {
        double Gauge = 1.0, Preparation = 1.0;
        bool PauseGauge = false, PausePreparation = false;
    };
    // Log-space products avoid overflow and make the final cap independent of
    // the order in which Haste and Slow were added. A zero multiplier stops work.
    inline TimingEffect CombineTimingEffects(const std::vector<TimingEffect>& Effects, double Maximum)
    {
        Maximum = std::max(1.0, NonNegative(Maximum));
        double GaugeLog = 0.0, PreparationLog = 0.0;
        bool ZeroGauge = false, ZeroPreparation = false;
        TimingEffect Result;
        for (const TimingEffect& E : Effects)
        {
            const double G = NonNegative(E.Gauge), P = NonNegative(E.Preparation);
            if (G == 0.0) ZeroGauge = true; else GaugeLog += std::log(G);
            if (P == 0.0) ZeroPreparation = true; else PreparationLog += std::log(P);
            Result.PauseGauge = Result.PauseGauge || E.PauseGauge;
            Result.PausePreparation = Result.PausePreparation || E.PausePreparation;
        }
        const double Limit = std::log(Maximum);
        Result.Gauge = ZeroGauge ? 0.0 : std::exp(std::min(Limit, GaugeLog));
        Result.Preparation = ZeroPreparation ? 0.0 : std::exp(std::min(Limit, PreparationLog));
        return Result;
    }

    class Scheduler
    {
    public:
        // Tokens remain monotonic across Reset, so old callbacks cannot complete
        // work from a restarted battle. Registration IDs are assigned by the host.
        void Reset()
        { Units.clear(); Now = 0.0; Active = Work(); Sent = false; }
        bool Add(int ID, double Speed, double InitialGauge)
        {
            if (ID < 0 || Find(ID) || NextCycle == std::numeric_limits<int>::max()) return false;
            Unit Value;
            Value.ID = ID; Value.Cycle = NextCycle++; Value.Speed = NonNegative(Speed);
            Value.Gauge = Fraction(InitialGauge);
            Units.push_back(Value);
            return true;
        }
        bool Remove(int ID)
        {
            const auto It = std::find_if(Units.begin(), Units.end(), [ID](const Unit& U) { return U.ID == ID; });
            if (It == Units.end()) return false;
            if (Active.Unit == ID) ClearLane();
            Units.erase(It);
            return true;
        }
        Unit* Find(int ID)
        {
            for (Unit& U : Units) if (U.ID == ID) return &U;
            return nullptr;
        }
        const Unit* Find(int ID) const
        {
            for (const Unit& U : Units) if (U.ID == ID) return &U;
            return nullptr;
        }
        const std::vector<Unit>& Snapshot() const { return Units; }
        const Work& ActiveWork() const { return Active; }
        double Time() const { return Now; }
        void SetSpeed(int ID, double Speed)
        { if (Unit* U = Find(ID)) U->Speed = NonNegative(Speed); }
        void SetRates(int ID, double GaugeMultiplier, double PreparationMultiplier, bool PauseGauge, bool PausePreparation)
        {
            if (Unit* U = Find(ID))
            {
                U->GaugeMultiplier = NonNegative(GaugeMultiplier);
                U->PreparationMultiplier = NonNegative(PreparationMultiplier);
                U->PauseGauge = PauseGauge; U->PausePreparation = PausePreparation;
            }
        }
        // Pushback after a cycle's start effects never replays poison/regen/AMP.
        bool PushBack(int ID, double FractionToRemove)
        {
            Unit* U = Find(ID);
            if (!U || !std::isfinite(FractionToRemove) || FractionToRemove<=0.0 || Active.Unit==ID ||
                (U->State!=Phase::Filling && U->State!=Phase::Ready && U->State!=Phase::CycleStart)) return false;
            U->Gauge = Fraction(U->Gauge-FractionToRemove);
            U->State = Phase::Filling;
            return true;
        }

        void Advance(double Delta, const Settings& Config)
        {
            Delta = NonNegative(Delta);
            if (Delta > std::numeric_limits<double>::max() - Now) return;
            const double Before = Now;
            Now += Delta;
            const bool Executing = Active.Kind == WorkKind::Action;
            for (Unit& U : Units)
            {
                if (U.State == Phase::Filling && !U.PauseGauge && (!Executing || Config.ContinueFillingDuringExecution))
                {
                    const double Denominator = NonNegative(Config.ReferenceSpeed) * NonNegative(Config.BaseFillSeconds);
                    const double Rate = Denominator > 0.0 ? NonNegative(U.Speed * NonNegative(Config.FillRateMultiplier) * U.GaugeMultiplier / Denominator) : 0.0;
                    const double Left = std::max(0.0, 1.0 - U.Gauge);
                    if (Left == 0.0 || (Rate > 0.0 && Delta >= Left / Rate))
                    {
                        U.Gauge = 1.0; U.State = U.StartResolved ? Phase::Ready : Phase::CycleStart;
                        U.EligibleAt = Before + (Left > 0.0 ? Left / Rate : 0.0);
                    }
                    else U.Gauge = Fraction(U.Gauge + Delta * Rate);
                }
                if (U.State == Phase::Preparing)
                {
                    if (!U.PausePreparation && (!Executing || Config.ContinuePreparingDuringExecution) && U.PreparationMultiplier>0.0)
                    {
                        if (Delta>=U.PreparationRemaining/U.PreparationMultiplier)
                        {
                            U.EligibleAt = Before + U.PreparationRemaining/U.PreparationMultiplier;
                            U.PreparationRemaining = 0.0; U.State = Phase::Queued;
                        }
                        else U.PreparationRemaining = std::max(0.0,U.PreparationRemaining-Delta*U.PreparationMultiplier);
                    }
                }
            }
        }

        // At most one pending start/effect, action, or end/effect owns the lane.
        // The host acknowledges that exact token after its asynchronous BP work.
        Work Poll()
        {
            if (Active.Kind == WorkKind::None)
            {
                const Unit* Best = nullptr;
                for (const Unit& U : Units)
                    if ((U.State == Phase::CycleStart ||
                         (U.State == Phase::Queued && !U.PausePreparation && U.PreparationMultiplier > 0.0)) &&
                        (!Best || U.EligibleAt < Best->EligibleAt ||
                         (U.EligibleAt == Best->EligibleAt && U.ID < Best->ID))) Best = &U;
                if (!Best) return Work();
                Active.Unit = Best->ID; Active.Cycle = Best->Cycle; Active.Action = Best->Action;
                Active.Kind = Best->State == Phase::CycleStart ? WorkKind::CycleStart : WorkKind::Action;
                if (Active.Kind == WorkKind::Action) Find(Best->ID)->State = Phase::Executing;
                Sent = false;
            }
            if (Sent) return Work();
            Sent = true;
            return Active;
        }
        bool CompleteStart(int ID, int Cycle, bool CanAct)
        {
            Unit* U = Find(ID);
            if (!U || Active.Kind != WorkKind::CycleStart || Active.Unit != ID || Active.Cycle != Cycle) return false;
            U->StartResolved = true;
            if (CanAct) { U->State = Phase::Ready; ClearLane(); }
            else BeginEnd(*U);
            return true;
        }
        // The host has verified that this unit needs no start effects or skip
        // processing. Resolve readiness without touching the executing action.
        bool CompleteEffectFreeStartDuringAction(int ID, int Cycle)
        {
            Unit* U = Find(ID);
            if (!U || Active.Kind!=WorkKind::Action || U->Cycle!=Cycle ||
                U->State!=Phase::CycleStart || U->StartResolved || U->PauseGauge || U->GaugeMultiplier<=0.0)
                return false;
            U->StartResolved = true;
            U->State = Phase::Ready;
            return true;
        }
        // Returning to Ready after a rejected, cancelled or pushed-back command
        // does not grant another award until this unit starts its next cycle.
        bool ClaimReadyAmp(int ID, int Cycle)
        {
            Unit* U = Find(ID);
            if (!U || U->State != Phase::Ready || U->Cycle != Cycle || U->ReadyAmpClaimed) return false;
            U->ReadyAmpClaimed = true;
            return true;
        }
        int Submit(int ID, double PreparationSeconds)
        {
            Unit* U = Find(ID);
            if (!U || U->State != Phase::Ready || !std::isfinite(PreparationSeconds) || PreparationSeconds < 0.0 ||
                NextAction == std::numeric_limits<int>::max()) return -1;
            U->Action = NextAction++;
            U->PreparationTotal = PreparationSeconds;
            U->PreparationRemaining = PreparationSeconds;
            U->EligibleAt = Now + PreparationSeconds;
            U->State = PreparationSeconds > 0.0 ? Phase::Preparing : Phase::Queued;
            return U->Action;
        }
        bool CompleteAction(int Action)
        {
            Unit* U = Find(Active.Unit);
            if (!U || Active.Kind != WorkKind::Action || Active.Action != Action) return false;
            BeginEnd(*U);
            return true;
        }
        bool RejectAction(int Action)
        {
            Unit* U = Find(Active.Unit);
            if (!U || Active.Kind != WorkKind::Action || Active.Action != Action) return false;
            U->Action = -1; U->State = Phase::Ready; ClearLane();
            U->PreparationRemaining = 0.0;
            return true;
        }
        bool CancelPending(int ID)
        {
            Unit* U = Find(ID);
            if (!U || (U->State != Phase::Preparing && U->State != Phase::Queued)) return false;
            U->Action = -1; U->State = Phase::Ready;
            U->PreparationRemaining = 0.0;
            return true;
        }
        bool CompleteEnd(int ID, int Cycle, const Settings& Config)
        {
            Unit* U = Find(ID);
            if (!U || Active.Kind != WorkKind::CycleEnd || Active.Unit != ID || Active.Cycle != Cycle ||
                NextCycle == std::numeric_limits<int>::max() || U->CompletedCycles == std::numeric_limits<int>::max()) return false;
            ++U->CompletedCycles; U->Cycle = NextCycle++; U->Action = -1;
            U->StartResolved = false; U->ReadyAmpClaimed = false; U->PreparationRemaining = 0.0;
            U->Gauge = Fraction(Config.RefillGauge); U->State = Phase::Filling;
            ClearLane();
            return true;
        }
    private:
        std::vector<Unit> Units;
        Work Active;
        bool Sent = false;
        double Now = 0.0;
        int NextCycle = 1;
        int NextAction = 1;
        void ClearLane() { Active = Work(); Sent = false; }
        void BeginEnd(Unit& U)
        {
            U.State = Phase::CycleEnd;
            Active.Kind = WorkKind::CycleEnd; Active.Unit = U.ID;
            Active.Cycle = U.Cycle; Active.Action = U.Action; Sent = false;
        }
    };
}
