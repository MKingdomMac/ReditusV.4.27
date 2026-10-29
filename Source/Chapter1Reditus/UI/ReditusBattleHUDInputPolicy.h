#pragma once

#include <cmath>

namespace ReditusHUDInputPolicy
{
    enum class Surface { Inactive, Commands, List, Target };
    constexpr int Pass = -1;
    constexpr int Confirm = -2;
    constexpr int Back = -3;

    inline bool ShouldDispatchPress(bool AlreadyPressed,bool Repeat,bool AllowRepeat)
    {
        return !AlreadyPressed || (Repeat && AllowRepeat);
    }

    // Slots are bottom, left, right, top. The same right/B key is Items in
    // commands and Back in lists; A confirms only inside the list.
    inline int FaceTarget(Surface View,int Face,bool Alternate)
    {
        if (Face<0 || Face>3 || View==Surface::Inactive) return Pass;
        if (View==Surface::List || View==Surface::Target) return Face==0 ? Confirm : Face==2 ? Back : Pass;
        return Face+(Alternate ? 4 : 0);
    }

    // Values verified against the project's Blueprint E_SelectType export.
    // A missing field is -1; the global mode is then the compatibility fallback.
    inline bool LegacyTargetSurface(bool PartyOwner,bool Alive,bool InputBlocked,int Selection,int BattleMode)
    {
        return PartyOwner && Alive && !InputBlocked && (Selection==1 || (Selection<0 && BattleMode==2));
    }

    inline bool LegacyCommandSurface(bool CommandMode,int Selection)
    {
        return CommandMode && (Selection<0 || Selection==3);
    }

    enum class CommandRoute { None, Legacy, Blueprint };
    inline CommandRoute ChooseCommandRoute(bool RouteLegacy,bool BlueprintBound)
    {
        return RouteLegacy ? CommandRoute::Legacy : BlueprintBound ? CommandRoute::Blueprint : CommandRoute::None;
    }

    inline bool CanRollbackPendingMenu(bool SameOwner,bool Alive,bool InputBlocked,int OriginalSelection,int CurrentSelection)
    {
        return SameOwner && Alive && !InputBlocked && OriginalSelection==3 && CurrentSelection==0;
    }

    /** Legacy Cancel callbacks restart the current turn; never run one for an expired menu owner. */
    inline bool CanCancelOwnedMenu(bool SameOwner,bool Alive,bool InputBlocked)
    {
        return SameOwner && Alive && !InputBlocked;
    }

    inline int Wrap(int Index,int Count)
    {
        if (Count<=0) return -1;
        const int Result=Index%Count;
        return Result<0 ? Result+Count : Result;
    }

    inline int ListMove(int Current,int Horizontal,int Vertical,int Count)
    {
        if (Count<=0) return -1;
        if (Current<0 || Current>=Count) return 0;
        const int Row=Current/2, Column=Current%2, LastRow=(Count-1)/2;
        if (Vertical!=0)
        {
            const int NextRow=Vertical<0 ? (Row>0 ? Row-1 : Row) : (Row<LastRow ? Row+1 : Row);
            const int Next=NextRow*2+Column;
            return Next<Count ? Next : Count-1;
        }
        if (Horizontal<0) return Column>0 ? Current-1 : Current;
        if (Horizontal>0) return Column==0 && Current+1<Count ? Current+1 : Current;
        return Current;
    }

    inline int PageMove(int Current,int Direction,int Count,int PerPage)
    {
        if (Count<=0 || PerPage<=0) return -1;
        if (Current<0 || Current>=Count) Current=0;
        const int Next=Current+Direction*PerPage;
        return Next<0 ? 0 : Next>=Count ? Count-1 : Next;
    }

    inline int CrossMove(int Current,int Horizontal,int Vertical,const bool Enabled[4])
    {
        const int Target=Vertical<0 ? 3 : Vertical>0 ? 0 : Horizontal<0 ? 1 : Horizontal>0 ? 2 : Current;
        return Target>=0 && Target<4 && Enabled[Target] ? Target : Current;
    }

    /** Digital trigger events and analog edges share one pressed-key latch. */
    inline int TriggerEdge(float Previous,float Current)
    {
        if (!std::isfinite(Current) || !std::isfinite(Previous)) return 0;
        if (Previous<0.55f && Current>=0.55f) return 1;
        if (Previous>0.35f && Current<=0.35f) return -1;
        return 0;
    }
}
