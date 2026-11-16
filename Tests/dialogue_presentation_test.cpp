#include "ReditusDialoguePresentationPolicy.h"
#include <cassert>

using namespace ReditusDialoguePresentationPolicy;

Snapshot Ready()
{
    Snapshot S; S.Active=true;S.Blocked=false;S.ContinueReady=true;S.InLevelSequence=true;S.Line=1;S.Characters=30;return S;
}
bool Run(Controls& C,const Snapshot& S,int Ticks=100)
{
    bool Requested=false;
    for(int I=0;I<Ticks;++I) Requested=C.Tick(.1,S)||Requested;
    return Requested;
}

int main()
{
    auto S=Ready();
    Controls Manual;assert(!Run(Manual,S));
    // Hidden cinematic controls cannot keep advancing an ordinary NPC.
    auto Npc=Ready();Npc.InLevelSequence=false;
    Controls NpcPlayback;NpcPlayback.Auto=true;NpcPlayback.SetHeld(true);
    assert(!Run(NpcPlayback,Npc));assert(!NpcPlayback.Auto);assert(!NpcPlayback.IsSkipping());
    Npc.InLevelSequence=true;++Npc.Line;
    assert(!Run(NpcPlayback,Npc)); // Entering a sequence requires a new request.
    NpcPlayback.Auto=true;assert(Run(NpcPlayback,Npc));

    Controls Auto;Auto.Auto=true;assert(Run(Auto,S));
    assert(!Run(Auto,S)); // One continue per unchanged line, even after a long wait.
    ++S.Line;assert(Run(Auto,S));

    for(int Count=1;Count<=8;++Count)
    {
        auto Choice=Ready();Choice.Choices=Count;
        Controls C;C.Auto=true;C.SetHeld(true);
        assert(!Run(C,Choice)); // Even a single response requires the player's choice.
        Choice.Choices=0;Choice.Line++;
        C.Auto=false;
        assert(!Run(C,Choice)); // Holding skip across the choice never resumes it.
        C.SetHeld(false);C.SetHeld(true);assert(Run(C,Choice));
    }

    for(int Reason=0;Reason<4;++Reason)
    {
        auto Blocked=Ready();
        if(Reason==0)Blocked.Blocked=true;
        if(Reason==1)Blocked.Active=false;
        if(Reason==2)Blocked.ScriptedSequence=true;
        if(Reason==3)Blocked.ContinueReady=false;
        Controls C;C.Auto=true;C.SetHeld(true);assert(!Run(C,Blocked));
    }

    Controls Log;Log.Auto=true;Log.SetHeld(true);Log.ToggleLog();
    assert(!Run(Log,Ready()));Log.ToggleLog();Log.Auto=false;
    assert(!Run(Log,Ready())); // Opening the log requires a fresh skip press.
    Log.SetHeld(false);Log.SetHeld(true);assert(Run(Log,Ready()));

    Controls Voice;Voice.Auto=true;auto Spoken=Ready();Spoken.VoicePlaying=true;
    assert(!Run(Voice,Spoken));Spoken.VoicePlaying=false;assert(Run(Voice,Spoken));
    Controls SkipVoice;SkipVoice.SetHeld(true);Spoken.VoicePlaying=true;assert(Run(SkipVoice,Spoken));

    Controls PausedAuto;PausedAuto.Auto=true;auto Choice=Ready();Choice.Choices=2;
    assert(!Run(PausedAuto,Choice));Choice.Choices=0;Choice.Line++;
    assert(!PausedAuto.Tick(.1,Choice));assert(Run(PausedAuto,Choice));

    Controls Confirm;Confirm.Auto=true;Confirm.ManualInput();
    // Establish a line before observing manual input, as the real widget does.
    Confirm.Tick(.01,S);Confirm.ManualInput();assert(!Run(Confirm,S));S.Line++;
    assert(Run(Confirm,S));

    assert(Controls::ReadingSeconds(0)>=1.4);
    assert(Controls::ReadingSeconds(40000)<=12);
    assert(Controls::ReadingSeconds(120)>Controls::ReadingSeconds(20));
    return 0;
}
