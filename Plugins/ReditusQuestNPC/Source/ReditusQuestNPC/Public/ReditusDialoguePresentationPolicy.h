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
        bool ScriptedSequence = false;
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
        std::uint64_t Line = 0;
        double Elapsed = 0;

    public:
        bool Auto = false;
        bool Log = false;

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
            if (Log && Held) RequireRelease = true;
        }

        static double ReadingSeconds(int Characters)
        {
            const double Seconds = 0.9 + (Characters > 0 ? Characters : 0) / 22.0;
            return Seconds < 1.4 ? 1.4 : (Seconds > 12.0 ? 12.0 : Seconds);
        }

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
                AwaitChange = false;
                return false;
            }
            if (!HasLine || State.Line != Line)
            {
                HasLine = true;
                Line = State.Line;
                Elapsed = 0;
                AwaitChange = false;
            }
            if (State.Choices > 0)
            {
                if (Held) RequireRelease = true;
                Elapsed = 0;
                AwaitChange = false;
                return false;
            }
            if (Log || State.Blocked || State.ScriptedSequence || !State.ContinueReady)
            {
                Elapsed = 0;
                if (!State.ContinueReady) AwaitChange = false;
                return false;
            }
            if (Delta > 0) Elapsed += Delta > .25 ? .25 : Delta;
            if (AwaitChange) return false;
            const bool Skip = IsSkipping() && Elapsed >= .18;
            const bool Autoplay = Auto && !State.VoicePlaying && Elapsed >= ReadingSeconds(State.Characters);
            if (!Skip && !Autoplay) return false;
            AwaitChange = true;
            Elapsed = 0;
            return true;
        }
    };
}
