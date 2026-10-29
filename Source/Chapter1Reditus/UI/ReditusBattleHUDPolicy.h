#pragma once

#include <cmath>

/** Shared, engine-independent decisions used by the widget and validation. */
namespace ReditusHUDPolicy
{
    constexpr int MaxActiveUnits = 4;
    struct PartySlotPlan
    {
        int Incoming[MaxActiveUnits] = {-1,-1,-1,-1};
        bool KeepDefeated[MaxActiveUnits] = {false,false,false,false};
    };

    inline bool KeepMissingCard(bool Occupied, bool KnownDefeated, bool ActorValid, bool NewMember, bool RemovedFromRoster)
    {
        if (!Occupied || (NewMember && RemovedFromRoster)) return false;
        return KnownDefeated || (!ActorValid && !NewMember);
    }

    /** Match identities before filling vacancies, preserving positions when
     * the gameplay array compacts on death or appends a revived actor.
     */
    inline PartySlotPlan PlanPartySlots(int Count, const bool Matches[MaxActiveUnits][MaxActiveUnits],
        const bool Occupied[MaxActiveUnits], const bool Defeated[MaxActiveUnits])
    {
        PartySlotPlan Plan;
        Count=Count<0 ? 0 : (Count>MaxActiveUnits ? MaxActiveUnits : Count);
        bool Used[MaxActiveUnits] = {false,false,false,false};
        for (int Slot=0; Slot<MaxActiveUnits; ++Slot)
        {
            for (int Item=0; Item<Count; ++Item)
                if (!Used[Item] && Matches[Slot][Item])
                {
                    Plan.Incoming[Slot]=Item; Used[Item]=true; break;
                }
            Plan.KeepDefeated[Slot]=Plan.Incoming[Slot]<0 && Occupied[Slot] && Defeated[Slot];
        }
        for (int Item=0; Item<Count; ++Item)
        {
            if (Used[Item]) continue;
            int Vacancy=-1;
            // Replace a removed member in its old position before using an
            // originally empty position. Death is not a removal here.
            for (int Slot=0; Slot<MaxActiveUnits; ++Slot)
                if (Plan.Incoming[Slot]<0 && !Plan.KeepDefeated[Slot] && Occupied[Slot]) { Vacancy=Slot; break; }
            if (Vacancy<0)
                for (int Slot=0; Slot<MaxActiveUnits; ++Slot)
                    if (Plan.Incoming[Slot]<0 && !Plan.KeepDefeated[Slot]) { Vacancy=Slot; break; }
            // A new actual combatant must fit even if four old KO cards were
            // cached. Explicit replacement reuses a defeated position.
            if (Vacancy<0)
                for (int Slot=0; Slot<MaxActiveUnits; ++Slot)
                    if (Plan.KeepDefeated[Slot]) { Vacancy=Slot; break; }
            if (Vacancy>=0)
            {
                Plan.Incoming[Vacancy]=Item;
                Plan.KeepDefeated[Vacancy]=false;
                Used[Item]=true;
            }
        }
        return Plan;
    }
    /** Actor changes can arrive before the new actor is ready for UI selection.
     * Keep the previous marker on a failed attempt so the next refresh retries.
     */
    template<typename SelectionID,typename SelectFunction>
    inline bool FollowChangedTurn(SelectionID Current,SelectionID& Previous,bool IsPlayer,SelectFunction TrySelect)
    {
        if (!IsPlayer) { Previous=SelectionID(); return false; }
        if (Current==Previous || !TrySelect(Current)) return false;
        Previous=Current;
        return true;
    }
    inline float Ratio(float Value, float Maximum)
    {
        if (!std::isfinite(Value) || !std::isfinite(Maximum) || Maximum <= 0.f) return 0.f;
        const float Result = Value / Maximum;
        return Result < 0.f ? 0.f : (Result > 1.f ? 1.f : Result);
    }

    inline bool CanCommand(bool HasUnit, float HP, bool CanAct, bool Ready, bool CommandsEnabled)
    {
        return HasUnit && std::isfinite(HP) && HP > 0.f && CanAct && Ready && CommandsEnabled;
    }

    inline bool CanOverdrive(bool BaseAllowed, bool Capability, bool Entered)
    {
        return BaseAllowed && Capability && Entered;
    }

    /** B is Back while choosing commands/targets; its existing Block mapping
     * owns enemy reaction windows. A dedicated keyboard key has no conflict.
     */
    inline bool PreferBackInput(bool IsBackKey, bool IsBlockKey, bool ReactionWindow)
    {
        return IsBackKey && (!IsBlockKey || !ReactionWindow);
    }

    inline bool MeaningfulAnalog(float Previous, float Current)
    {
        return std::isfinite(Current) && std::fabs(Current) > 0.25f &&
            std::fabs(Current - Previous) > 0.10f;
    }

    inline int Wrap(int Index, int Count)
    {
        if (Count <= 0) return -1;
        const int Result = Index % Count;
        return Result < 0 ? Result + Count : Result;
    }

    /** Four main commands in a 2x2 grid, three secondary commands below. */
    inline int NextGridCommand(int Current, int Horizontal, int Vertical, const bool* Enabled)
    {
        const int Rows[] = {0,0,1,1,2,2,2};
        const int Columns[] = {0,1,0,1,0,1,2};
        Current=Wrap(Current,7);
        const int Row=Rows[Current];
        const int Column=Columns[Current];
        if (Horizontal!=0)
        {
            const int Count=Row==2 ? 3 : 2;
            for (int Step=1; Step<=Count; ++Step)
            {
                const int NextColumn=Wrap(Column+Horizontal*Step,Count);
                for (int I=0; I<7; ++I)
                    if (Rows[I]==Row && Columns[I]==NextColumn && Enabled[I]) return I;
            }
        }
        else if (Vertical!=0)
        {
            for (int Step=1; Step<=3; ++Step)
            {
                const int NextRow=Wrap(Row+Vertical*Step,3);
                int Best=-1;
                int Distance=100;
                for (int I=0; I<7; ++I)
                {
                    const int Delta=Columns[I]-Column;
                    const int Absolute=Delta<0 ? -Delta : Delta;
                    if (Rows[I]==NextRow && Enabled[I] && Absolute<Distance) { Best=I; Distance=Absolute; }
                }
                if (Best>=0) return Best;
            }
        }
        return Current;
    }
}
