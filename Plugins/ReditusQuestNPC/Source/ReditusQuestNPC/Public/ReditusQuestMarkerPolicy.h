#pragma once

namespace ReditusQuestMarkerPolicy
{
    // Numeric values match the public EReditusQuestState enum.
    enum class State { Unknown, Unavailable, Available, Active, Ready, Completed, Failed };
    enum class Override { Inherit, Show, Hide };
    struct Rules { bool Available = true, Active = true, Ready = true; };
    inline bool Visible(State Current, const Rules& Quest, Override Stage)
    {
        if (Current == State::Available) return Quest.Available;
        if (Current == State::Ready) return Quest.Ready;
        if (Current != State::Active) return false; // Completed/failed/locked can never leak a marker.
        return Stage == Override::Show || (Stage == Override::Inherit && Quest.Active);
    }
}
