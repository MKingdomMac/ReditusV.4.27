#pragma once
#include <vector>

namespace ReditusFetchQuestFlow
{
    // State values match EReditusQuestState. No stock/item ID is a dialogue branch ID.
    enum class State { Unknown, Unavailable, Available, Active, Ready, Completed, Failed };
    enum class Phase { Offer, Reminder, Ready, Completed, Unavailable, Failed, Pending, Error };
    enum class Line { Root, Offer, Accept, NotNow, Reminder, HandIn, GiveItems, Declined, Completed, Unavailable, Failed, Pending, Error };
    enum class Action { None, Accept, HandIn };
    inline Phase Select(State Status, bool CanHandIn, bool Pending, bool ActionFailed)
    {
        if (Status == State::Completed) return Phase::Completed;
        if (Status == State::Failed) return Phase::Failed;
        if (Status == State::Unknown || Status == State::Unavailable) return Phase::Unavailable;
        if (Pending) return Phase::Pending;
        if (ActionFailed) return Phase::Error;
        if (CanHandIn) return Phase::Ready;
        return Status == State::Available ? Phase::Offer : Phase::Reminder;
    }
    inline int Entry(Phase Value)
    {
        switch (Value)
        {
        case Phase::Offer: return 1;
        case Phase::Reminder: return 4;
        case Phase::Ready: return 5;
        case Phase::Completed: return 8;
        case Phase::Unavailable: return 9;
        case Phase::Failed: return 10;
        case Phase::Pending: return 11;
        default: return 12;
        }
    }
    struct Node { int ID; Line Text; bool Player; Action OnEnter; std::vector<int> Links; int RequiredPhase; };
    inline std::vector<Node> Template(Phase Initial, bool AllowHandInDecline = true)
    {
        const std::vector<int> Results = {4, 5, 8, 9, 10, 11, 12};
        return {
            {0, Line::Root, false, Action::None, {Entry(Initial)}, -1},
            {1, Line::Offer, false, Action::None, {2, 3}, int(Phase::Offer)},
            {2, Line::Accept, true, Action::Accept, Results, -1},
            {3, Line::NotNow, true, Action::None, {7}, -1},
            {4, Line::Reminder, false, Action::None, {}, int(Phase::Reminder)},
            {5, Line::HandIn, false, Action::None, AllowHandInDecline ? std::vector<int>{6, 3} : std::vector<int>{6}, int(Phase::Ready)},
            {6, Line::GiveItems, true, Action::HandIn, Results, -1},
            {7, Line::Declined, false, Action::None, {}, -1},
            {8, Line::Completed, false, Action::None, {}, int(Phase::Completed)},
            {9, Line::Unavailable, false, Action::None, {}, int(Phase::Unavailable)},
            {10, Line::Failed, false, Action::None, {}, int(Phase::Failed)},
            {11, Line::Pending, false, Action::None, {}, int(Phase::Pending)},
            {12, Line::Error, false, Action::None, {}, int(Phase::Error)}
        };
    }
}
