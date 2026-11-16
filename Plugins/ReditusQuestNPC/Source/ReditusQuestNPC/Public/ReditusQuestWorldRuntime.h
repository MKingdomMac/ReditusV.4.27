#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>

namespace ReditusQuestWorld
{
    enum class Removal { Removed, Defeated, Recruited };
    struct RemovedNPC { std::string Id; Removal Reason = Removal::Removed; };
    class State
    {
        std::map<std::string, Removal> Removed;
        std::set<std::string> Recruited;
    public:
        bool IsRemoved(const std::string& Id) const { return Removed.count(Id) != 0; }
        bool MarkRemoved(const std::string& Id, Removal Reason)
        {
            if (Id.empty() || int(Reason) < 0 || int(Reason) > int(Removal::Recruited)) return false;
            Removed.emplace(Id, Reason); return true; // Preserve the original reason on repeats.
        }
        bool HasRecruitment(const std::string& Character) const { return Recruited.count(Character) != 0; }
        bool RecordRecruitment(const std::string& Character)
        { return !Character.empty() && Recruited.insert(Character).second; }
        const std::map<std::string, Removal>& GetRemoved() const { return Removed; }
        const std::set<std::string>& GetRecruitments() const { return Recruited; }
        void Reset() { Removed.clear(); Recruited.clear(); }
        bool Import(const std::vector<RemovedNPC>& NPCs, const std::vector<std::string>& Characters, std::string& Error)
        {
            State Candidate;
            for (const auto& NPC : NPCs)
                if (Candidate.IsRemoved(NPC.Id) || !Candidate.MarkRemoved(NPC.Id, NPC.Reason))
                { Error = "Invalid/duplicate persistent NPC ID or removal reason."; return false; }
            for (const auto& Character : Characters)
                if (!Candidate.RecordRecruitment(Character))
                { Error = "Invalid/duplicate recruitment receipt."; return false; }
            *this = Candidate; return true;
        }
    };
}
