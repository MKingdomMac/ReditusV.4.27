#pragma once

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

// This is the actual runtime state machine, independent of Unreal UI and saves.
namespace ReditusQuestRuntime
{
    enum class Kind { Talk, CollectItem, DefeatEnemy, ReachLocation, RecruitUnit, Interact, Custom, TimerElapsed, WinBattle, Choice, StepAction };
    enum class State { Unavailable, Available, Active, Ready, Completed, Failed };
    enum class TimerMode { None, Wait, Deadline };
    enum class StageEventKind { Enter, Complete, QuestComplete };
    struct StageEvent { std::string Key, StageId; StageEventKind Type; };
    struct Task
    {
        std::string Id, Target;
        Kind Type = Kind::Talk;
        int Required = 1;
        bool Optional = false;
    };
    struct ChoiceOption { std::string Id; int Value = 0; };
    struct Choice { std::string Id; std::vector<ChoiceOption> Options; };
    struct Selection { std::string Option; int Value = 0; };
    struct Route { std::string ChoiceId, OptionId, Next; bool Finish = false; };
    struct Stage
    {
        std::string Id;
        std::vector<Task> Tasks;
        TimerMode Timer = TimerMode::None;
        double Seconds = 0;
        std::vector<Choice> Choices;
        std::vector<std::string> Actions;
        std::vector<Route> Routes;
        std::string Next;
        bool Finish = false;
    };
    struct Definition
    {
        std::string Key;
        int Version = 1;
        bool HasRewards = false;
        std::vector<std::string> Prerequisites;
        std::vector<Stage> Stages;
    };
    struct Record
    {
        std::string Key, StageId;
        int Version = 1;
        State Status = State::Active;
        std::map<std::string, int> Counts;
        std::set<std::string> Events, Milestones, ObjectiveMilestones;
        bool RewardsPending = false;
        double TimerRemaining = 0;
        std::vector<std::string> Path;
        std::map<std::string, Selection> Choices;
        std::set<std::string> Actions; // Successful handoffs; never replayed on load or retry.
    };
    inline std::string Counter(const std::string& StageId, const std::string& TaskId)
    { return StageId + "/" + TaskId; }
    inline bool ValidId(const std::string& Id)
    { return !Id.empty() && Id.find('/') == std::string::npos; }

    class Engine
    {
        std::map<std::string, Definition> Definitions;
        std::map<std::string, Record> Records;
        std::vector<std::string> Changed;
        std::vector<std::pair<std::string, std::string>> Milestones;
        std::vector<std::pair<std::string, std::string>> ObjectiveMilestones;
        std::vector<std::pair<std::string, std::string>> EnteredStages, Timeouts;
        std::vector<StageEvent> StageEvents;

        void Enter(const Stage& S, Record& R)
        {
            R.StageId = S.Id; R.Status = State::Active; R.Path.push_back(S.Id);
            R.TimerRemaining = S.Timer == TimerMode::None ? 0 : S.Seconds;
            EnteredStages.emplace_back(R.Key, S.Id);
            StageEvents.push_back({R.Key, S.Id, StageEventKind::Enter});
        }

        void ReachedObjective(Record& R, const Stage& S, const Task& T)
        {
            const auto ID = Counter(S.Id, T.Id);
            if (Count(R, S, T) >= T.Required && R.ObjectiveMilestones.insert(ID).second)
                ObjectiveMilestones.emplace_back(R.Key, ID);
        }

        void Mark(const std::string& Key)
        {
            if (std::find(Changed.begin(), Changed.end(), Key) == Changed.end()) Changed.push_back(Key);
        }
        static int StageIndex(const Definition& D, const std::string& Id)
        {
            for (unsigned I = 0; I < D.Stages.size(); ++I) if (D.Stages[I].Id == Id) return int(I);
            return -1;
        }
        static int Count(const Record& R, const Stage& S, const Task& T)
        {
            const auto It = R.Counts.find(Counter(S.Id, T.Id));
            return It == R.Counts.end() ? 0 : It->second;
        }
        static bool Complete(const Record& R, const Stage& S)
        {
            for (const auto& T : S.Tasks) if (!T.Optional && Count(R, S, T) < T.Required) return false;
            return true;
        }
        static int NextIndex(const Definition& D, const Record& R, int Index)
        {
            const auto& S = D.Stages[unsigned(Index)];
            for (const auto& Route : S.Routes)
            {
                const auto Choice = R.Choices.find(Route.ChoiceId);
                if (Choice != R.Choices.end() && Choice->second.Option == Route.OptionId)
                    return Route.Finish ? -1 : StageIndex(D, Route.Next);
            }
            if (S.Finish) return -1;
            return S.Next.empty() ? (unsigned(Index + 1) < D.Stages.size() ? Index + 1 : -1) : StageIndex(D, S.Next);
        }
        bool ApplyScoped(const std::string& Key, Kind Type, const std::string& Target)
        {
            auto It = Records.find(Key); const auto* D = FindDefinition(Key);
            if (It == Records.end() || !D || It->second.Status != State::Active) return false;
            auto& R = It->second; const auto& S = D->Stages[unsigned(StageIndex(*D, R.StageId))];
            bool Updated = false;
            for (const auto& T : S.Tasks) if (T.Type == Type && T.Target == Target && Count(R, S, T) == 0)
            { R.Counts[Counter(S.Id, T.Id)] = 1; ReachedObjective(R, S, T); Updated = true; }
            if (Updated) { Evaluate(*D, R); Mark(Key); }
            return Updated;
        }
        void Evaluate(const Definition& D, Record& R)
        {
            const int Index = StageIndex(D, R.StageId);
            if (Index < 0) return;
            const auto& S = D.Stages[unsigned(Index)];
            if (!Complete(R, S))
            {
                R.Status = State::Active; // A final inventory requirement can be lost before turn-in.
                return;
            }
            if (R.Milestones.insert(S.Id).second)
            {
                Milestones.emplace_back(R.Key, S.Id);
                StageEvents.push_back({R.Key, S.Id, StageEventKind::Complete});
            }
            const int Next = NextIndex(D, R, Index);
            if (Next >= 0) Enter(D.Stages[unsigned(Next)], R);
            else R.Status = State::Ready;
        }
    public:
        bool AddDefinition(const Definition& D, std::string& Error)
        {
            if (D.Key.empty() || D.Version < 1 || D.Stages.empty())
            { Error = "A quest needs a key, positive version and at least one stage."; return false; }
            std::set<std::string> StageIds, ChoiceIds, ActionIds;
            for (const auto& S : D.Stages)
            {
                if (!ValidId(S.Id) || !StageIds.insert(S.Id).second)
                { Error = "Stage IDs must be unique, nonempty and contain no slash."; return false; }
                std::set<std::string> TaskIds; bool RequiredTask = false;
                bool TimerTask = false;
                if (int(S.Timer) < int(TimerMode::None) || int(S.Timer) > int(TimerMode::Deadline)
                    || !std::isfinite(S.Seconds) || (S.Timer != TimerMode::None && (S.Seconds <= 0 || S.Seconds > 31536000)))
                { Error = "Timed stages need a finite, positive duration in seconds."; return false; }
                for (const auto& T : S.Tasks)
                {
                    if (!ValidId(T.Id) || T.Target.empty() || T.Required < 1 || !TaskIds.insert(T.Id).second
                        || int(T.Type) < int(Kind::Talk) || int(T.Type) > int(Kind::StepAction))
                    { Error = "Objectives need unique IDs, a target, valid type and positive count."; return false; }
                    RequiredTask = RequiredTask || !T.Optional;
                    if (T.Type == Kind::TimerElapsed)
                    {
                        if (S.Timer != TimerMode::Wait || T.Required != 1)
                        { Error = "TimerElapsed belongs to a Wait timer and requires count 1."; return false; }
                        TimerTask = TimerTask || !T.Optional;
                    }
                }
                std::set<std::string> LocalChoices, LocalActions;
                for (const auto& C : S.Choices)
                {
                    if (!ValidId(C.Id) || !ChoiceIds.insert(C.Id).second || C.Options.empty())
                    { Error = "Choices need globally unique IDs and authored options."; return false; }
                    LocalChoices.insert(C.Id); std::set<std::string> Options;
                    for (const auto& O : C.Options) if (!ValidId(O.Id) || !Options.insert(O.Id).second)
                    { Error = "Choice options need unique, nonempty IDs."; return false; }
                }
                for (const auto& A : S.Actions)
                    if (!ValidId(A) || !ActionIds.insert(A).second)
                    { Error = "Step action IDs must be unique across the quest."; return false; }
                    else LocalActions.insert(A);
                for (const auto& T : S.Tasks)
                {
                    if ((T.Type == Kind::Choice || T.Type == Kind::StepAction) && (T.Required != 1 || T.Optional))
                    { Error = "Choice/action objectives must be required, count 1."; return false; }
                    if ((T.Type == Kind::Choice && !LocalChoices.erase(T.Target))
                        || (T.Type == Kind::StepAction && !LocalActions.erase(T.Target)))
                    { Error = "Choice/action objectives must each reference one local definition."; return false; }
                }
                if (!LocalChoices.empty() || !LocalActions.empty())
                { Error = "Each choice/action needs its own required objective."; return false; }
                if (!RequiredTask) { Error = "Each stage needs at least one required objective."; return false; }
                if (S.Timer == TimerMode::Wait && !TimerTask)
                { Error = "Wait stages need a required TimerElapsed objective."; return false; }
            }
            for (unsigned I = 0; I < D.Stages.size(); ++I)
            {
                const auto& S = D.Stages[I];
                const auto Forward = [&](const std::string& Target) { return StageIndex(D, Target) > int(I); };
                if ((S.Finish && !S.Next.empty()) || (!S.Next.empty() && !Forward(S.Next)))
                { Error = "Next stage must point forward; ending stages cannot also have Next."; return false; }
                std::set<std::string> Routes;
                for (const auto& Route : S.Routes)
                {
                    bool Found = false;
                    for (unsigned J = 0; J <= I; ++J) for (const auto& C : D.Stages[J].Choices)
                        if (C.Id == Route.ChoiceId) for (const auto& O : C.Options) if (O.Id == Route.OptionId) Found = true;
                    if (!Found || !Routes.insert(Counter(Route.ChoiceId, Route.OptionId)).second
                        || (Route.Finish ? !Route.Next.empty() : !Forward(Route.Next)))
                    { Error = "Routes need an earlier/current authored choice option and a forward target or Finish."; return false; }
                }
            }
            for (const auto& Key : D.Prerequisites)
                if (Key.empty() || Key == D.Key) { Error = "Invalid or self-referencing prerequisite."; return false; }
            const auto Existing = Definitions.find(D.Key);
            if (Existing != Definitions.end())
            {
                if (Existing->second.Version != D.Version) { Error = "Definition changed during this session."; return false; }
                return true;
            }
            Definitions.emplace(D.Key, D); return true;
        }
        const Definition* FindDefinition(const std::string& Key) const
        { auto I = Definitions.find(Key); return I == Definitions.end() ? nullptr : &I->second; }
        const Record* FindRecord(const std::string& Key) const
        { auto I = Records.find(Key); return I == Records.end() ? nullptr : &I->second; }
        const std::map<std::string, Record>& GetRecords() const { return Records; }
        State GetState(const std::string& Key) const
        {
            if (const auto* R = FindRecord(Key)) return R->Status;
            const auto* D = FindDefinition(Key);
            if (!D) return State::Unavailable;
            for (const auto& P : D->Prerequisites)
            { const auto* R = FindRecord(P); if (!R || R->Status != State::Completed) return State::Unavailable; }
            return State::Available;
        }
        bool Start(const std::string& Key, bool IgnorePrerequisites = false)
        {
            const auto* D = FindDefinition(Key);
            if (!D || FindRecord(Key) || (!IgnorePrerequisites && GetState(Key) != State::Available)) return false;
            Record R; R.Key = Key; R.Version = D->Version; Enter(D->Stages.front(), R);
            Records.emplace(Key, R); Mark(Key); return true;
        }
        bool Signal(Kind Type, const std::string& Target, int Amount, const std::string& EventId)
        {
            if (Amount < 1 || Target.empty() || EventId.empty() || Type == Kind::CollectItem || Type == Kind::TimerElapsed || Type == Kind::Choice || Type == Kind::StepAction) return false;
            bool Any = false;
            for (auto& Pair : Records)
            {
                auto& R = Pair.second;
                if ((R.Status != State::Active && R.Status != State::Ready) || R.Events.count(EventId)) continue;
                const auto* D = FindDefinition(R.Key);
                const auto& S = D->Stages[unsigned(StageIndex(*D, R.StageId))];
                bool Updated = false;
                for (const auto& T : S.Tasks)
                    if (T.Type == Type && T.Target == Target)
                    {
                        int& Value = R.Counts[Counter(S.Id, T.Id)];
                        const int Remaining = T.Required - Value;
                        if (Remaining > 0)
                        { Value += std::min(Amount, Remaining); ReachedObjective(R, S, T); Updated = true; }
                    }
                if (Updated)
                { R.Events.insert(EventId); Evaluate(*D, R); Mark(R.Key); Any = true; }
            }
            return Any;
        }
        bool Choose(const std::string& Key, const std::string& ChoiceId, const std::string& OptionId)
        {
            auto I = Records.find(Key); const auto* D = FindDefinition(Key);
            if (!D || I == Records.end() || I->second.Status != State::Active || I->second.Choices.count(ChoiceId)) return false;
            auto& R = I->second; const auto& S = D->Stages[unsigned(StageIndex(*D, R.StageId))];
            for (const auto& C : S.Choices) if (C.Id == ChoiceId) for (const auto& O : C.Options) if (O.Id == OptionId)
            {
                Selection Saved; Saved.Option = O.Id; Saved.Value = O.Value; R.Choices.emplace(C.Id, Saved);
                return ApplyScoped(Key, Kind::Choice, C.Id);
            }
            return false;
        }
        bool CanApplyAction(const std::string& Key, const std::string& StageId, const std::string& ActionId) const
        {
            const auto* D = FindDefinition(Key); const auto* R = FindRecord(Key);
            if (!D || !R || R->Status != State::Active || R->StageId != StageId || R->Actions.count(ActionId)) return false;
            const auto& S = D->Stages[unsigned(StageIndex(*D, StageId))];
            return std::find(S.Actions.begin(), S.Actions.end(), ActionId) != S.Actions.end();
        }
        bool ApplyAction(const std::string& Key, const std::string& StageId, const std::string& ActionId)
        {
            if (!CanApplyAction(Key, StageId, ActionId)) return false;
            Records.find(Key)->second.Actions.insert(ActionId);
            return ApplyScoped(Key, Kind::StepAction, ActionId);
        }
        // Stages not on the selected branch must not count against journal progress.
        std::set<std::string> RelevantStages(const std::string& Key) const
        {
            const auto* D = FindDefinition(Key); const auto* R = FindRecord(Key);
            std::set<std::string> Result;
            if (!D) return Result;
            if (!R) { for (const auto& S : D->Stages) Result.insert(S.Id); return Result; }
            Result.insert(R->Path.begin(), R->Path.end());
            if (R->Status != State::Active) return Result;
            std::vector<int> Queue{StageIndex(*D, R->StageId)}; std::set<int> Seen;
            while (!Queue.empty())
            {
                const int I = Queue.back(); Queue.pop_back();
                if (I < 0 || !Seen.insert(I).second) continue;
                const auto& S = D->Stages[unsigned(I)]; Result.insert(S.Id);
                bool Resolved = false;
                for (const auto& Route : S.Routes)
                {
                    const auto C = R->Choices.find(Route.ChoiceId);
                    if (C == R->Choices.end()) { if (!Route.Finish) Queue.push_back(StageIndex(*D, Route.Next)); }
                    else if (C->second.Option == Route.OptionId)
                    { if (!Route.Finish) Queue.push_back(StageIndex(*D, Route.Next)); Resolved = true; break; }
                }
                if (!Resolved && !S.Finish) Queue.push_back(S.Next.empty()
                    ? (unsigned(I + 1) < D->Stages.size() ? I + 1 : -1) : StageIndex(*D, S.Next));
            }
            return Result;
        }
        bool SetInventory(const std::string& Key, const std::string& TaskId, int Amount)
        {
            auto I = Records.find(Key); const auto* D = FindDefinition(Key);
            if (I == Records.end() || !D || (I->second.Status != State::Active && I->second.Status != State::Ready)) return false;
            auto& R = I->second;
            const auto& S = D->Stages[unsigned(StageIndex(*D, R.StageId))];
            for (const auto& T : S.Tasks)
                if (T.Id == TaskId && T.Type == Kind::CollectItem)
                {
                    const int Value = std::max(0, std::min(Amount, T.Required));
                    if (Value == Count(R, S, T)) return false;
                    R.Counts[Counter(S.Id, T.Id)] = Value; ReachedObjective(R, S, T);
                    Evaluate(*D, R); Mark(Key); return true;
                }
            return false;
        }
        int GetCount(const std::string& Key, const std::string& StageId, const std::string& TaskId) const
        {
            const auto* R = FindRecord(Key); if (!R) return 0;
            const auto I = R->Counts.find(Counter(StageId, TaskId)); return I == R->Counts.end() ? 0 : I->second;
        }
        bool Finish(const std::string& Key)
        {
            auto I = Records.find(Key); const auto* D = FindDefinition(Key);
            if (I == Records.end() || !D || I->second.Status != State::Ready) return false;
            I->second.Status = State::Completed; I->second.TimerRemaining = 0;
            StageEvents.push_back({Key, I->second.StageId, StageEventKind::QuestComplete});
            I->second.RewardsPending = D->HasRewards; Mark(Key); return true;
        }
        bool Acknowledge(const std::string& Key)
        {
            auto I = Records.find(Key);
            if (I == Records.end() || I->second.Status != State::Completed || !I->second.RewardsPending) return false;
            I->second.RewardsPending = false; Mark(Key); return true;
        }
        // Time does not spill into the next stage. All callbacks run after this step.
        bool Tick(double Delta, const std::set<std::string>& Paused = {})
        {
            if (!std::isfinite(Delta) || Delta <= 0) return false;
            bool Any = false;
            for (auto& Pair : Records)
            {
                auto& R = Pair.second;
                if (R.Status != State::Active || R.TimerRemaining <= 0 || Paused.count(R.Key)) continue;
                const auto* D = FindDefinition(R.Key);
                const auto& S = D->Stages[unsigned(StageIndex(*D, R.StageId))];
                R.TimerRemaining = std::max(0.0, R.TimerRemaining - Delta);
                if (R.TimerRemaining > 0) continue;
                if (S.Timer == TimerMode::Deadline)
                { R.Status = State::Failed; Timeouts.emplace_back(R.Key, S.Id); }
                else if (S.Timer == TimerMode::Wait)
                {
                    for (const auto& T : S.Tasks) if (T.Type == Kind::TimerElapsed)
                    { R.Counts[Counter(S.Id, T.Id)] = 1; ReachedObjective(R, S, T); }
                    Evaluate(*D, R);
                }
                Mark(R.Key); Any = true;
            }
            return Any;
        }
        bool RetryFailed(const std::string& Key)
        {
            auto I = Records.find(Key); const auto* D = FindDefinition(Key);
            if (!D || I == Records.end() || I->second.Status != State::Failed) return false;
            if (!I->second.Actions.empty() || !I->second.Choices.empty())
            {
                // Restart this deadline, preserving committed choices, handoffs and
                // earlier stages. Never rewind and re-award inventory/money.
                auto& Saved = I->second;
                Saved.Status = State::Active;
                Saved.TimerRemaining = D->Stages[unsigned(StageIndex(*D, Saved.StageId))].Seconds;
                Mark(Key); return true;
            }
            Record R; R.Key = Key; R.Version = D->Version; Enter(D->Stages.front(), R);
            I->second = R; Mark(Key); return true;
        }
        std::vector<std::string> TakeChanged() { auto Out = Changed; Changed.clear(); return Out; }
        std::vector<std::pair<std::string, std::string>> TakeMilestones()
        { auto Out = Milestones; Milestones.clear(); return Out; }
        std::vector<std::pair<std::string, std::string>> TakeObjectiveMilestones()
        { auto Out = ObjectiveMilestones; ObjectiveMilestones.clear(); return Out; }
        std::vector<std::pair<std::string, std::string>> TakeEnteredStages()
        { auto Out = EnteredStages; EnteredStages.clear(); return Out; }
        std::vector<std::pair<std::string, std::string>> TakeTimeouts()
        { auto Out = Timeouts; Timeouts.clear(); return Out; }
        std::vector<StageEvent> TakeStageEvents()
        { auto Out = StageEvents; StageEvents.clear(); return Out; }
        void Reset()
        { Records.clear(); Changed.clear(); Milestones.clear(); ObjectiveMilestones.clear(); EnteredStages.clear(); Timeouts.clear(); StageEvents.clear(); }

        // Validate into a temporary map. A bad/unsupported save never replaces live state.
        bool Import(const std::vector<Record>& Input, std::string& Error)
        {
            std::map<std::string, Record> Candidate;
            for (auto R : Input)
            {
                const auto* D = FindDefinition(R.Key);
                if (!D || R.Version != D->Version || Candidate.count(R.Key))
                { Error = "Unknown/duplicate quest or mismatched definition version."; return false; }
                const int Current = StageIndex(*D, R.StageId);
                if (Current < 0 || (R.Status != State::Active && R.Status != State::Ready && R.Status != State::Completed && R.Status != State::Failed)
                    || (R.RewardsPending && (R.Status != State::Completed || !D->HasRewards)))
                { Error = "Invalid quest state, stage or pending reward state."; return false; }
                // Version-1 saves had only a linear stage index. Infer that path only
                // for definitions that do not contain branches or economic step actions.
                if (R.Path.empty())
                {
                    for (const auto& S : D->Stages) if (!S.Routes.empty() || !S.Next.empty() || S.Finish || !S.Choices.empty() || !S.Actions.empty())
                    { Error = "Branching saves need their visited stage path."; return false; }
                    for (int I = 0; I <= Current; ++I) R.Path.push_back(D->Stages[unsigned(I)].Id);
                }
                if (R.Path.front() != D->Stages.front().Id || R.Path.back() != R.StageId)
                { Error = "Saved path must begin at the first stage and end at the current stage."; return false; }
                std::set<std::string> Visited;
                int Previous = -1;
                for (const auto& Id : R.Path)
                {
                    const int I = StageIndex(*D, Id);
                    if (I < 0 || I <= Previous || !Visited.insert(Id).second
                        || (Previous >= 0 && NextIndex(*D, R, Previous) != I)
                        || (I != Current && !Complete(R, D->Stages[unsigned(I)])))
                    { Error = "Saved stage path is not a completed authored branch."; return false; }
                    Previous = I;
                }
                std::map<std::string, int> Limits;
                std::set<std::string> KnownChoices, KnownActions;
                for (const auto& S : D->Stages)
                {
                    for (const auto& T : S.Tasks) Limits[Counter(S.Id, T.Id)] = Visited.count(S.Id) ? T.Required : 0;
                    for (const auto& C : S.Choices)
                    {
                        const auto Saved = R.Choices.find(C.Id);
                        if (Saved == R.Choices.end()) continue;
                        bool Matches = false;
                        for (const auto& O : C.Options) if (O.Id == Saved->second.Option && O.Value == Saved->second.Value) Matches = true;
                        if (!Visited.count(S.Id) || !Matches) { Error = "Invalid saved choice option or value."; return false; }
                        KnownChoices.insert(C.Id);
                    }
                    for (const auto& A : S.Actions) if (R.Actions.count(A) && Visited.count(S.Id)) KnownActions.insert(A);
                    for (const auto& T : S.Tasks)
                        if ((T.Type == Kind::Choice && ((Count(R,S,T) == 1) != (R.Choices.count(T.Target) != 0)))
                            || (T.Type == Kind::StepAction && ((Count(R,S,T) == 1) != (R.Actions.count(T.Target) != 0))))
                        { Error = "Choice/action receipt does not match its objective."; return false; }
                }
                if (KnownChoices.size() != R.Choices.size() || KnownActions != R.Actions)
                { Error = "Unknown choice or step action in save."; return false; }
                for (const auto& Pair : R.Counts)
                {
                    auto Limit = Limits.find(Pair.first);
                    if (Limit == Limits.end() || Pair.second < 0 || Pair.second > Limit->second)
                    { Error = "Invalid objective ID or count in save."; return false; }
                }
                const bool Done = Complete(R, D->Stages[unsigned(Current)]);
                const auto& CurrentStage = D->Stages[unsigned(Current)];
                if (!std::isfinite(R.TimerRemaining) || R.TimerRemaining < 0
                    || R.TimerRemaining > CurrentStage.Seconds
                    || (CurrentStage.Timer == TimerMode::None && R.TimerRemaining != 0)
                    || ((R.Status == State::Completed || R.Status == State::Failed) && R.TimerRemaining != 0)
                    || (R.Status == State::Active && CurrentStage.Timer == TimerMode::Deadline && R.TimerRemaining == 0)
                    || (R.Status == State::Failed && (CurrentStage.Timer != TimerMode::Deadline || Done)))
                { Error = "Invalid saved quest timer."; return false; }
                if ((R.Status == State::Active && Done)
                    || ((R.Status == State::Ready || R.Status == State::Completed) && (!Done || NextIndex(*D, R, Current) != -1)))
                { Error = "Objective counts do not match saved quest state."; return false; }
                for (const auto& Id : R.Milestones)
                    if (!Visited.count(Id))
                    { Error = "Unknown or future stage milestone."; return false; }
                for (const auto& Id : R.ObjectiveMilestones)
                {
                    auto Found = Limits.find(Id);
                    if (Found == Limits.end() || Found->second == 0)
                    { Error = "Unknown or future objective milestone."; return false; }
                }
                for (const auto& Id : R.Events)
                    if (Id.empty()) { Error = "Empty event ID."; return false; }
                Candidate.emplace(R.Key, R);
            }
            Records.swap(Candidate); Changed.clear(); Milestones.clear(); ObjectiveMilestones.clear(); EnteredStages.clear(); Timeouts.clear(); StageEvents.clear(); return true;
        }
    };
}
