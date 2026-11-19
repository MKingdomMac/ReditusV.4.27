#pragma once

#include <cstdint>

// The runtime uses this policy; tests exercise the same code without Unreal.
namespace ReditusDialoguePresentationPolicy
{
    struct Snapshot
    {
        bool Active = false;
        bool Blocked = true;
        bool ContinueReady = false;
        bool VoicePlaying = false;
        bool HasDialogueNode = false;
        bool PlaySequenceCue = false;
        bool InLevelSequence = false;
        int Choices = 0;
        int Characters = 0;
        std::uint64_t Line = 0;
    };

    class Controls
    {
        bool Held = false;
        bool RequireRelease = false;
        bool AwaitChange = false;
        bool HasLine = false;
        bool HeardVoice = false;
        std::uint64_t Line = 0;
        double Elapsed = 0;
        double AfterVoice = 0;

    public:
        bool Auto = false;
        bool Log = false;

        void ToggleAuto()
        {
            Auto = !Auto;
            Elapsed = 0;
            AfterVoice = 0;
        }

        void SetHeld(bool Value)
        {
            Held = Value;
            if (!Held) RequireRelease = false;
        }

        bool IsSkipping() const { return Held && !RequireRelease && !Log; }

        void ManualInput()
        {
            Elapsed = 0;
            AwaitChange = true;
        }

        void ToggleLog()
        {
            Log = !Log;
            Elapsed = 0;
            AfterVoice = 0;
            if (Log && Held) RequireRelease = true;
        }

        static double ReadingSeconds(int Characters)
        {
            const double Seconds = .6 + (Characters > 0 ? Characters : 0) / 38.0;
            return Seconds < 1.0 ? 1.0 : (Seconds > 4.0 ? 4.0 : Seconds);
        }

        static double VoicePauseSeconds() { return .25; }

        // A request means "use the existing continue handler once". No choice
        // index is ever emitted by automatic playback or held skip.
        bool Tick(double Delta, const Snapshot& State)
        {
            if (!State.Active || !State.InLevelSequence)
            {
                if (!State.InLevelSequence) Auto = false;
                Held = false;
                RequireRelease = false;
                Elapsed = 0;
                AfterVoice = 0;
                AwaitChange = false;
                return false;
            }
            if (!HasLine || State.Line != Line)
            {
                HasLine = true;
                Line = State.Line;
                Elapsed = 0;
                AwaitChange = false;
                HeardVoice = false;
                AfterVoice = 0;
            }
            if (State.VoicePlaying)
            {
                HeardVoice = true;
                AfterVoice = 0;
            }
            if (State.Choices > 0)
            {
                if (Held) RequireRelease = true;
                Elapsed = 0;
                AfterVoice = 0;
                AwaitChange = false;
                return false;
            }
            if (Log || State.Blocked || !State.HasDialogueNode || !State.ContinueReady)
            {
                Elapsed = 0;
                AfterVoice = 0;
                if (!State.ContinueReady) AwaitChange = false;
                return false;
            }
            const double Step = Delta > 0 ? (Delta > .25 ? .25 : Delta) : 0;
            Elapsed += Step;
            if (HeardVoice && !State.VoicePlaying) AfterVoice += Step;
            if (AwaitChange) return false;
            const bool Skip = IsSkipping() && Elapsed >= .18;
            const bool Autoplay = Auto && !State.VoicePlaying
                && (HeardVoice ? AfterVoice >= VoicePauseSeconds() : Elapsed >= ReadingSeconds(State.Characters));
            if (!Skip && !Autoplay) return false;
            AwaitChange = true;
            Elapsed = 0;
            return true;
        }
    };
}
