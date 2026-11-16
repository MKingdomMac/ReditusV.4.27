#pragma once

// Engine-independent lifecycle used by the live NPC component. A state is
// reused for every resolved battle; there is deliberately no chain-length cap.
namespace ReditusNPCFollowUpPolicy
{
    enum class Phase { Idle, Ready, Sequence, DialogueReady, Dialogue, Complete, Failed };
    enum class Action { Wait, StartSequence, StartDialogue, Finish, Abort };
    struct State
    {
        Phase Current = Phase::Idle;
        bool HasSequence = false, HasDialogue = false, DialogueApproved = false;
        bool Busy() const { return Current != Phase::Idle; }
        bool Begin(bool Sequence, bool Dialogue)
        {
            if (Busy()) return false;
            HasSequence = Sequence; HasDialogue = Dialogue; DialogueApproved = false;
            Current = Phase::Ready; return true;
        }
        Action Next(bool PresentationReady)
        {
            if (Current == Phase::Failed) return Action::Abort;
            if (!PresentationReady) return Action::Wait;
            if (Current == Phase::Complete) return Action::Finish;
            if (Current == Phase::Ready && HasSequence) { Current = Phase::Sequence; return Action::StartSequence; }
            if (Current == Phase::Ready || Current == Phase::DialogueReady)
            {
                if (HasDialogue) { Current = Phase::Dialogue; return Action::StartDialogue; }
                Current = Phase::Complete; return Action::Finish;
            }
            return Action::Wait;
        }
        bool SequenceEnded(bool Completed)
        {
            if (Current != Phase::Sequence) return false;
            Current = Completed ? Phase::DialogueReady : Phase::Failed; return true;
        }
        bool ApproveDialogue()
        {
            if (Current != Phase::Dialogue) return false;
            DialogueApproved = true; return true;
        }
        bool DialogueClosed(bool NormalSignal)
        {
            if (Current != Phase::Dialogue) return false;
            Current = NormalSignal && DialogueApproved ? Phase::Complete : Phase::Failed;
            return true;
        }
        void Cancel() { if (Busy()) Current = Phase::Failed; }
        void Reset() { *this = State(); }
    };
}
