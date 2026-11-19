#pragma once

#include <algorithm>
#include <set>
#include <string>
#include <vector>

// Pure graph compiler policy, shared by the editor adapter and offline tests.
namespace ReditusQuestGraphPolicy
{
    enum class Kind { Start, Stage, Finish };
    struct Exit { std::string Choice, Option; int Target = -1; };
    struct Node { Kind Type = Kind::Stage; std::string ID; std::vector<Exit> Exits; };
    struct Plan { std::vector<int> Stages; };

    inline bool Compile(const std::vector<Node>& Nodes, Plan& Output, std::string& Error)
    {
        Output = Plan(); Error.clear();
        int Start = -1, Finish = -1;
        std::set<std::string> IDs;
        for (unsigned I = 0; I < Nodes.size(); ++I)
        {
            const auto& N = Nodes[I];
            if (N.Type == Kind::Start)
            {
                if (Start >= 0) { Error = "A quest must have exactly one Start."; return false; }
                Start = int(I);
            }
            else if (N.Type == Kind::Finish)
            {
                if (Finish >= 0) { Error = "A quest must have exactly one Finish."; return false; }
                Finish = int(I);
            }
            else if (N.ID.empty() || N.ID.find('/') != std::string::npos || !IDs.insert(N.ID).second)
            { Error = "Stage IDs must be unique, nonempty and contain no slash."; return false; }
        }
        if (Start < 0 || Finish < 0 || IDs.empty()) { Error = "Add Start, at least one Stage, and Finish."; return false; }
        std::vector<int> Incoming(Nodes.size(), 0);
        for (unsigned I = 0; I < Nodes.size(); ++I)
        {
            const auto& N = Nodes[I]; int Defaults = 0; std::set<std::string> Conditions;
            for (const auto& E : N.Exits)
            {
                if (E.Target < 0 || unsigned(E.Target) >= Nodes.size() || E.Target == int(I) || E.Target == Start)
                { Error = "Every output needs one valid forward destination."; return false; }
                if (E.Choice.empty() != E.Option.empty()) { Error = "A branch needs both Choice ID and Option ID."; return false; }
                if (E.Choice.empty()) ++Defaults;
                else if (!Conditions.insert(E.Choice + "/" + E.Option).second)
                { Error = "Duplicate choice output."; return false; }
                ++Incoming[unsigned(E.Target)];
            }
            if ((N.Type == Kind::Finish && !N.Exits.empty())
                || (N.Type == Kind::Start && (N.Exits.size() != 1 || Nodes[unsigned(N.Exits[0].Target)].Type != Kind::Stage))
                || (N.Type != Kind::Finish && Defaults != 1))
            { Error = "Start connects to one Stage; each Stage needs its Next output; Finish has no output."; return false; }
        }
        std::vector<int> Seen(Nodes.size(), 0), Queue{Start};
        for (unsigned I = 0; I < Queue.size(); ++I)
        {
            const int Index = Queue[I];
            if (Seen[unsigned(Index)]++) continue;
            for (const auto& E : Nodes[unsigned(Index)].Exits) Queue.push_back(E.Target);
        }
        if (std::find(Seen.begin(), Seen.end(), 0) != Seen.end()) { Error = "Remove or connect unreachable nodes."; return false; }
        std::vector<int> Ready{Start}; Plan Result; unsigned Count = 0;
        while (!Ready.empty())
        {
            std::sort(Ready.begin(), Ready.end(), [&](int A, int B) { return Nodes[unsigned(A)].ID > Nodes[unsigned(B)].ID; });
            const int Index = Ready.back(); Ready.pop_back(); ++Count;
            if (Nodes[unsigned(Index)].Type == Kind::Stage) Result.Stages.push_back(Index);
            for (const auto& E : Nodes[unsigned(Index)].Exits) if (--Incoming[unsigned(E.Target)] == 0) Ready.push_back(E.Target);
        }
        if (Count != Nodes.size()) { Error = "Quest loops are not supported; branches must move forward."; return false; }
        Output = Result; return true;
    }
}
