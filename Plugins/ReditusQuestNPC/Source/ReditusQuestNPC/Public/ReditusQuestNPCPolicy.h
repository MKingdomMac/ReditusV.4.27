#pragma once

// Engine-independent policy: the runtime uses these same functions.
namespace ReditusQuestNPCPolicy
{
    enum class State { Unknown, Unavailable, Available, Active, ReadyToDeliver, Completed, Failed };
    enum class Action { Start, Advance, TurnIn };

    inline bool InRange(int Progress, int Minimum, int MaximumExclusive)
    {
        return Progress >= Minimum && (MaximumExclusive < 0 || Progress < MaximumExclusive);
    }

    inline bool RuleMatches(State Actual, State Required, bool AnyState, int Progress,
                            int Minimum, int MaximumExclusive)
    {
        return Actual != State::Unknown && (AnyState || Actual == Required)
            && InRange(Progress, Minimum, MaximumExclusive);
    }

    inline bool ActionAllowed(State Actual, Action Requested)
    {
        switch (Requested)
        {
        case Action::Start: return Actual == State::Available;
        case Action::Advance: return Actual == State::Active;
        case Action::TurnIn: return Actual == State::ReadyToDeliver;
        }
        return false;
    }

    inline bool ObjectiveVisible(State Actual, int Progress, int Minimum, int MaximumExclusive)
    {
        return Actual == State::Active && InRange(Progress, Minimum, MaximumExclusive);
    }
}
