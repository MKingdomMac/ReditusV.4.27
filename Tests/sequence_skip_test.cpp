#include <cassert>
#include <functional>
#include <algorithm>
#include <utility>
#include <cstring>

#define TEXT(Value) Value
#define UE_LOG(...) do {} while (false)
#define AddUniqueDynamic AddUnique
#define RemoveDynamic Remove
struct FKey
{
    int Value;
    bool operator==(FKey Other) const { return Value == Other.Value; }
};
namespace EKeys
{
    const FKey LeftShift{1}, RightShift{2}, Gamepad_RightTrigger{3}, Other{4}, Gamepad_RightTriggerAxis{5};
}
bool SkipKey(const FKey& Key)
{
    return Key == EKeys::LeftShift || Key == EKeys::RightShift || Key == EKeys::Gamepad_RightTrigger;
}
struct UGameInstance;
struct UObject
{
    virtual ~UObject() = default;
    UGameInstance* Instance = nullptr;
    bool Valid = true;
};
template<class T> bool IsValid(T* Object) { return Object && Object->Valid; }
template<class T> T* Cast(UObject* Object) { return dynamic_cast<T*>(Object); }
template<class T> const T* Cast(const UObject* Object) { return dynamic_cast<const T*>(Object); }
struct FReditusSequenceSkipHandoff
{
    std::function<void()> Callback;
    bool IsBound() const { return bool(Callback); }
    void Unbind() { Callback = {}; }
    void ExecuteIfBound() const { if (Callback) Callback(); }
};
struct Event
{
    int Bindings = 0;
    template<class T> void AddUnique(T*, void(T::*)()) { ++Bindings; }
    template<class T> void Remove(T*, void(T::*)()) { Bindings = 0; }
};
struct APlayerController;
struct UWorld : UObject { APlayerController* Controller = nullptr; };
struct FFrameNumber { int Value; explicit FFrameNumber(int ValueIn) : Value(ValueIn) {} };
struct FFrameTime
{
    int Value;
    FFrameTime(FFrameNumber Frame) : Value(Frame.Value) {}
    bool operator>(FFrameTime Other) const { return Value > Other.Value; }
    FFrameTime operator-(FFrameTime Other) const { return FFrameTime(FFrameNumber(Value - Other.Value)); }
};
struct FQualifiedFrameTime { FFrameTime Time; };
enum class EMovieScenePositionType { Frame };
enum class EUpdatePositionMethod { Play, Jump };
struct FMovieSceneSequencePlaybackParams
{
    FFrameTime Frame{FFrameNumber(0)};
    EMovieScenePositionType PositionType = EMovieScenePositionType::Frame;
    EUpdatePositionMethod UpdateMethod = EUpdatePositionMethod::Play;
};
template<class T> struct TWeakObjectPtr
{
    T* Value = nullptr;
    TWeakObjectPtr& operator=(T* Other) { Value = Other; return *this; }
    void Reset() { Value = nullptr; }
    bool IsValid() const { return ::IsValid(Value); }
    T* Get() const { return IsValid() ? Value : nullptr; }
    T* operator->() const { return Value; }
};
struct APlayerController : UObject
{
    UWorld* World = nullptr;
    UObject* CurrentSequence = nullptr;
    UObject* DialogueWidget = nullptr;
    bool bShowMouseCursor = true;
    bool IsLocalController() const { return true; }
    UWorld* GetWorld() const { return World; }
    float GetInputAnalogKeyState(FKey) const { return 0; }
};
struct UReditusSequenceSkipSubsystem;
struct UGameInstance
{
    UReditusSequenceSkipSubsystem* System = nullptr;
    template<class T> T* GetSubsystem() { return System; }
};
struct UGameplayStatics
{
    static APlayerController* GetPlayerController(UWorld* World, int) { return World ? World->Controller : nullptr; }
    static UGameInstance* GetGameInstance(const UObject* Context) { return Context ? Context->Instance : nullptr; }
};
struct UMovieSceneSequencePlayer : UObject
{
    bool Playing = true, Paused = false;
    int Stops = 0;
    int Position = 23, DialogueCloses = 0, Jumps = 0;
    int Plays = 0;
    UWorld* World = nullptr;
    Event OnFinished, OnStop;
    bool IsPlaying() const { return Playing; }
    bool IsPaused() const { return Paused; }
    UWorld* GetWorld() const { return World; }
    FQualifiedFrameTime GetStartTime() const { return {FFrameTime(FFrameNumber(10))}; }
    FQualifiedFrameTime GetEndTime() const { return {FFrameTime(FFrameNumber(200))}; }
    void SetPlaybackPosition(FMovieSceneSequencePlaybackParams Params)
    {
        assert(Params.UpdateMethod == EUpdatePositionMethod::Jump);
        assert(DialogueCloses > 0); // Dialogue closes only after the hold completes.
        Position = Params.Frame.Value; ++Jumps;
    }
    void StopAtCurrentTime() { ++Stops; Playing = Paused = false; }
    void Play() { ++Plays; Playing = true; Paused = false; }
};
struct ALevelSequenceActor : UObject
{
    UMovieSceneSequencePlayer* Player = nullptr;
    UWorld* World = nullptr;
    UWorld* GetWorld() const { return World; }
    UMovieSceneSequencePlayer* GetSequencePlayer() const { return Player; }
};
enum class ESlateVisibility { Hidden };
struct UUserWidget : UObject
{
    APlayerController* Owner = nullptr;
    bool InViewport = true, Visible = true, Blocked = false;
    std::function<void()> OnRemoved;
    APlayerController* GetOwningPlayer() const { return Owner; }
    bool IsInViewport() const { return InViewport; }
    bool IsVisible() const { return Visible; }
    void SetVisibility(ESlateVisibility) { Visible = false; }
    void* GetClass() const { return nullptr; }
};
struct FBoolProperty
{
    void SetPropertyValue_InContainer(UUserWidget* Dialogue, bool Value) { Dialogue->Blocked = Value; }
};
template<class T> T* FindFProperty(void*, const char* Field)
{
    static T Property;
    assert(std::strcmp(Field, "isUIInputBlocked") == 0);
    return &Property;
}
namespace ReditusQuestNPCReflection
{
    UObject* ReadObject(APlayerController* Controller, const char* Field)
    {
        if (!Controller) return nullptr;
        if (std::strcmp(Field, "Current Sequence ") == 0) return Controller->CurrentSequence;
        assert(std::strcmp(Field, "DialogueWidget") == 0);
        return Controller->DialogueWidget;
    }
    bool BindDialogueRemoved(UUserWidget* Widget, UReditusSequenceSkipSubsystem* System, bool Add);
}
struct UReditusSequenceSkipNotice
{
    int Removals = 0;
    UWorld* World = nullptr;
    UWorld* GetWorld() const { return World; }
    void RemoveFromParent() { ++Removals; }
};
struct FMath
{
    static double Clamp(double Value, double Minimum, double Maximum) { return std::max(Minimum, std::min(Value, Maximum)); }
    template<class T> static T Max(T Left, T Right) { return Left > Right ? Left : Right; }
};
struct FPlatformTime
{
    static double Now;
    static double Seconds() { return Now; }
};
double FPlatformTime::Now = 0;

struct UReditusSequenceSkipSubsystem
{
    ALevelSequenceActor* SequenceActor = nullptr;
    UMovieSceneSequencePlayer* SequencePlayer = nullptr;
    FReditusSequenceSkipHandoff OnSkipped;
    UReditusSequenceSkipNotice Popup;
    UReditusSequenceSkipNotice* NoticeWidget = nullptr;
    UWorld* World = nullptr;
    bool Capture = true, bAllowSkip = false, bKeyHeld = false, bTriggerHeld = false, bMouseHeld = false;
    bool bNeedTriggerRelease = false;
    bool bShowingProgress = false, bAutoAdvance = false, bConsumeKeyUntilRelease = false;
    TWeakObjectPtr<UMovieSceneSequencePlayer> AutoSequence;
    TWeakObjectPtr<UWorld> AutoWorld;
    TWeakObjectPtr<UUserWidget> ObservedDialogue;
    TWeakObjectPtr<APlayerController> DialogueController;
    TWeakObjectPtr<UMovieSceneSequencePlayer> DialogueSequence;
    bool bContinueRequested = false, bPlaySequenceCue = false;
    float Progress = 0;
    float LastTriggerValue = 0;
    double HeldSeconds = 0, NoticeSeconds = 0, LastTime = 0;
    int Notices = 0;
    UWorld* GetTickableGameObjectWorld() const { return World; }
    void RegisterInput() {}
    bool CanCapture() const { return Capture && HasActiveSequence(); }
    void ShowBlockedNotice() { ++Notices; NoticeSeconds = 2; Popup.World = World; NoticeWidget = &Popup; bShowingProgress = false; }
    void ShowHoldProgress(float Value) { Progress = Value; bShowingProgress = true; }
    void HideHoldProgress() { bShowingProgress = false; Progress = 0; }
    void DismissSequenceDialogue() { ++SequencePlayer->DialogueCloses; }
    void SetAutoAdvance(UMovieSceneSequencePlayer*, bool);
    bool IsAutoAdvanceEnabled(UMovieSceneSequencePlayer*) const;
    bool Configure(ALevelSequenceActor*, bool, FReditusSequenceSkipHandoff);
    bool HasActiveSequence() const;
    bool IsHoldingSkip() const { return bAllowSkip && (bKeyHeld || bTriggerHeld || bMouseHeld); }
    UMovieSceneSequencePlayer* FindSequencePlayer(APlayerController*) const;
    void ObserveDialogue(UUserWidget*, bool);
    void DialogueContinueRequested(UUserWidget*);
    void HandleDialogueRemoved();
    void HandleDialogueRemovedWithResult(unsigned char);
    void ClearDialogueObservation();
    void UpdateDialogueContinuation();
    void ClearSequence();
    void SequenceEnded();
    bool BeginAttempt();
    bool HandleKey(const FKey&, bool, bool = false);
    bool HandleTrigger(float);
    void SetMouseHeld(bool);
    void Tick(float);
};
using uint8 = unsigned char;
struct UReditusSequenceSkipLibrary
{
    static bool ConfigureLevelSequenceSkipping(const UObject* Context,
        ALevelSequenceActor* Actor, FReditusSequenceSkipHandoff, bool AllowSkip);
};
bool ReditusQuestNPCReflection::BindDialogueRemoved(UUserWidget* Widget, UReditusSequenceSkipSubsystem* System, bool Add)
{
    if (Add) Widget->OnRemoved = [System]() { System->HandleDialogueRemovedWithResult(0); };
    else Widget->OnRemoved = {};
    return true;
}
// ACTUAL_CONTROLLER_ACTOR
// ACTUAL_CONFIGURE
// ACTUAL_HAS_ACTIVE
// ACTUAL_CLEAR
// ACTUAL_SEQUENCE_ENDED
// ACTUAL_ATTEMPT
// ACTUAL_KEY
// ACTUAL_TRIGGER
// ACTUAL_MOUSE
// ACTUAL_TICK
// ACTUAL_AUTO_SEQUENCE
// ACTUAL_CONFIGURE_NODE_AND_CONTINUATION

void Advance(UReditusSequenceSkipSubsystem& System, int Frames = 6)
{
    for (int Index = 0; Index < Frames; ++Index)
    {
        FPlatformTime::Now += .2;
        System.Tick(.2f);
    }
}

int main()
{
    UWorld World, OtherWorld;
    UMovieSceneSequencePlayer Player, OtherPlayer;
    Player.World = &World; OtherPlayer.World = &OtherWorld;
    ALevelSequenceActor Actor, OtherActor;
    Actor.Player = &Player; Actor.World = &World;
    OtherActor.Player = &OtherPlayer; OtherActor.World = &OtherWorld;
    UReditusSequenceSkipSubsystem System; System.World = &World;
    UGameInstance Instance{&System};
    APlayerController Controller;
    World.Controller = &Controller;
    Controller.World = &World;
    Controller.Instance = &Instance;
    int Handoffs = 0;
    FReditusSequenceSkipHandoff Finish{[&]() { ++Handoffs; }};

    assert(!System.Configure(&OtherActor, true, Finish));
    // Reproduce the uploaded PlayLevelSequence graph: Current Sequence is set,
    // Sequence Actor is unwired, and the authored On Skipped is connected.
    Controller.CurrentSequence = &Actor;
    assert(UReditusSequenceSkipLibrary::ConfigureLevelSequenceSkipping(&Controller, nullptr, Finish, false));
    assert(System.SequencePlayer == &Player && !Controller.bShowMouseCursor);
    assert(!System.HandleKey(EKeys::Other, true));
    assert(System.HandleKey(EKeys::LeftShift, true));
    Advance(System, 12);
    assert(Player.Stops == 0 && Handoffs == 0 && System.Notices == 1);
    assert(System.HandleKey(EKeys::LeftShift, true, true));
    assert(System.Notices == 1);
    System.HandleKey(EKeys::LeftShift, false);

    System.HandleTrigger(.8f);
    System.HandleTrigger(.8f);
    assert(System.Notices == 2);
    Advance(System);
    assert(Player.Stops == 0);
    System.HandleTrigger(0);
    System.SetMouseHeld(true);
    assert(System.Notices == 3);
    Advance(System);
    assert(Player.Stops == 0 && Handoffs == 0);
    System.SetMouseHeld(false);

    // Enabling skip requires an authored completion callback.
    assert(!System.Configure(&Actor, true, {}));
    System.HandleKey(EKeys::LeftShift, true);
    Advance(System);
    assert(Handoffs == 0 && Player.Stops == 0);

    assert(UReditusSequenceSkipLibrary::ConfigureLevelSequenceSkipping(&Controller, nullptr, Finish, true));
    System.HandleKey(EKeys::LeftShift, true);
    Advance(System, 2);
    assert(Player.Stops == 0 && System.Progress > .39f && System.Progress < .41f);
    assert(Player.DialogueCloses == 0 && Player.Position == 23); // Holding fills before touching dialogue/timeline.
    System.HandleKey(EKeys::LeftShift, false);
    Advance(System);
    assert(Player.Stops == 0 && !System.bShowingProgress);
    System.HandleKey(EKeys::LeftShift, true);
    Advance(System);
    assert(Player.Stops == 1 && Handoffs == 1 && !System.HasActiveSequence());
    assert(Player.Position == 199 && Player.Jumps == 1 && Player.DialogueCloses == 1);
    assert(System.HandleKey(EKeys::LeftShift, true, true)); // Held repeats cannot reach the old close-dialogue action.
    Advance(System, 10);
    assert(Player.Stops == 1 && Handoffs == 1);
    System.HandleKey(EKeys::LeftShift, false);

    // Paused sequences can use an explicitly enabled skip handoff.
    Player.Paused = true;
    assert(System.Configure(&Actor, true, Finish));
    System.HandleTrigger(.8f);
    Advance(System);
    assert(Player.Stops == 2 && Handoffs == 2);
    Player.Playing = true;
    assert(System.Configure(&Actor, true, Finish));
    System.HandleTrigger(.8f);
    Advance(System);
    assert(Player.Stops == 2 && Handoffs == 2); // A held trigger cannot skip the next scene.
    System.HandleTrigger(0);
    System.SetMouseHeld(true);
    Advance(System);
    assert(Player.Stops == 3 && Handoffs == 3);

    // Focus loss and map travel cancel a partially held skip.
    Player.Playing = true;
    assert(System.Configure(&Actor, true, Finish));
    System.HandleKey(EKeys::LeftShift, true);
    Advance(System, 2);
    System.Capture = false;
    Advance(System);
    System.Capture = true;
    Advance(System);
    assert(Player.Stops == 3);
    System.World = &OtherWorld;
    Advance(System);
    assert(!System.HasActiveSequence() && !System.OnSkipped.IsBound() && Handoffs == 3);

    // Natural finish only clears registration. It never invokes the skip path.
    System.World = &World;
    assert(System.Configure(&Actor, true, Finish));
    System.SequenceEnded();
    assert(!System.OnSkipped.IsBound() && Handoffs == 3);

    Player.Playing = true;
    System.SetAutoAdvance(&Player, true);
    assert(System.IsAutoAdvanceEnabled(&Player) && !System.IsAutoAdvanceEnabled(&OtherPlayer));
    Player.Playing = false; Player.Paused = true;
    assert(System.IsAutoAdvanceEnabled(&Player)); // New dialogue widgets in the same paused scene inherit Auto.
    System.SetAutoAdvance(&Player, false);
    assert(!System.IsAutoAdvanceEnabled(&Player));
    System.SetAutoAdvance(&Player, true);
    Player.Paused = false;
    Advance(System);
    assert(!System.bAutoAdvance && !System.AutoSequence.IsValid());

    // Director creates the dialogue, then pauses its own player. A voiced
    // reply may close from the Blueprint timer, without another widget tick.
    UUserWidget Dialogue, Replacement;
    Dialogue.Owner = Replacement.Owner = &Controller;
    Controller.DialogueWidget = &Dialogue;
    Player.Paused = true;
    System.Capture = false; // Playback resume must not depend on widget focus.
    System.ObserveDialogue(&Dialogue, false);
    assert(Dialogue.OnRemoved);
    Dialogue.InViewport = false;
    Controller.DialogueWidget = nullptr;
    Dialogue.OnRemoved();
    assert(Player.Paused && Player.Plays == 0);
    Advance(System, 1);
    assert(Player.Plays == 1 && !Dialogue.OnRemoved);

    // A normal dialogue advance can display a different visible line first.
    Player.Playing = false; Player.Paused = true;
    Dialogue.InViewport = true; Controller.DialogueWidget = &Dialogue;
    System.ObserveDialogue(&Dialogue, false);
    System.DialogueContinueRequested(&Dialogue);
    Advance(System, 1);
    assert(Player.Plays == 1);
    Dialogue.InViewport = false;
    Controller.DialogueWidget = &Replacement;
    Advance(System, 1);
    assert(Player.Plays == 1);

    // A Play Level Sequence dialogue cue resumes this exploration player even
    // when the battle-only Blueprint branch has no player to resume.
    Dialogue.InViewport = true; Controller.DialogueWidget = &Dialogue;
    System.ObserveDialogue(&Dialogue, true);
    Advance(System, 1);
    assert(Player.Plays == 2 && !Dialogue.Visible && Dialogue.Blocked);

    // The existing manual playpausedsequence can run before the observer.
    Player.Playing = false; Player.Paused = true;
    Dialogue.Visible = true;
    System.ObserveDialogue(&Dialogue, false);
    System.DialogueContinueRequested(&Dialogue);
    Player.Play();
    Advance(System, 1);
    assert(Player.Plays == 3 && !Dialogue.OnRemoved);

    // An explicit registration also supports a caller that does not use the
    // JRPG controller member to locate the player.
    Controller.CurrentSequence = nullptr;
    Player.Playing = false; Player.Paused = true;
    assert(System.Configure(&Actor, false, {}));
    assert(System.FindSequencePlayer(&Controller) == &Player);
    System.ObserveDialogue(&Dialogue, false);
    Dialogue.InViewport = false;
    Controller.DialogueWidget = nullptr;
    Dialogue.OnRemoved();
    Advance(System, 1);
    assert(Player.Plays == 4);
    return 0;
}
