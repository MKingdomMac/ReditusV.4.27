#include <cassert>
#include <functional>
#include <algorithm>
#include <utility>
#include <cstring>
#include <string>
#include <vector>

#define TEXT(Value) Value
#define UE_LOG(...) do {} while (false)
#define AddUniqueDynamic AddUnique
#define RemoveDynamic Remove
#define NSLOCTEXT(Namespace, Key, Value) Value
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
    const void* Identity = nullptr;
    bool IsBound() const { return bool(Callback); }
    void Unbind() { Callback = {}; }
    void ExecuteIfBound() const { if (Callback) Callback(); }
};
struct Event
{
    std::function<void()> Observer;
    template<class T> void AddUnique(T* Object, void(T::*Function)())
    { Observer = [Object, Function]() { (Object->*Function)(); }; }
    template<class T> void Remove(T*, void(T::*)()) { Observer = {}; }
    std::vector<FReditusSequenceSkipHandoff> Authored;
    bool Contains(const FReditusSequenceSkipHandoff& Delegate) const
    {
        return std::any_of(Authored.begin(), Authored.end(), [&](const FReditusSequenceSkipHandoff& Binding)
            { return Binding.Identity == Delegate.Identity; });
    }
    void Broadcast()
    {
        const auto Snapshot = *this;
        if (Snapshot.Observer) Snapshot.Observer();
        for (const auto& Binding : Snapshot.Authored) Binding.ExecuteIfBound();
    }
};
struct APlayerController;
struct UWorld : UObject { APlayerController* Controller = nullptr; bool bIsTearingDown = false, Paused = false; };
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
    TWeakObjectPtr() = default;
    explicit TWeakObjectPtr(T* Other) : Value(Other) {}
    TWeakObjectPtr& operator=(T* Other) { Value = Other; return *this; }
    void Reset() { Value = nullptr; }
    bool IsValid() const { return ::IsValid(Value); }
    T* Get() const { return IsValid() ? Value : nullptr; }
    T* operator->() const { return Value; }
};
struct FInputModeGameOnly {};
struct APlayerController : UObject
{
    UWorld* World = nullptr;
    UObject* CurrentSequence = nullptr;
    UObject* DialogueWidget = nullptr;
    bool bShowMouseCursor = true;
    bool GameOnly = false, InputEnabled = false, Exploring = true;
    bool LegacyBlocked = false;
    int MoveLocks = 0, LookLocks = 0;
    int InputModeRestores = 0;
    bool IsLocalController() const { return true; }
    UWorld* GetWorld() const { return World; }
    float GetInputAnalogKeyState(FKey) const { return 0; }
    void SetInputMode(FInputModeGameOnly) { GameOnly = true; ++InputModeRestores; }
    void EnableInput(APlayerController* Controller) { assert(Controller == this); InputEnabled = true; }
    bool IsMoveInputIgnored() const { return MoveLocks > 0; }
    bool IsLookInputIgnored() const { return LookLocks > 0; }
    void SetIgnoreMoveInput(bool Block) { MoveLocks = std::max(0, MoveLocks + (Block ? 1 : -1)); }
    void SetIgnoreLookInput(bool Block) { LookLocks = std::max(0, LookLocks + (Block ? 1 : -1)); }
};
namespace ReditusQuestSequenceGate
{
    bool IsExploring(APlayerController* Controller) { return Controller->Exploring; }
}
struct UReditusSequenceSkipSubsystem;
struct UGameInstance
{
    UReditusSequenceSkipSubsystem* System = nullptr;
    bool HasViewport = true;
    template<class T> T* GetSubsystem() { return System; }
    bool GetGameViewportClient() const { return HasViewport; }
};
struct UGameplayStatics
{
    static APlayerController* GetPlayerController(UWorld* World, int) { return World ? World->Controller : nullptr; }
    static APlayerController* GetPlayerController(const UObject* Object, int);
    static UGameInstance* GetGameInstance(const UObject* Context) { return Context ? Context->Instance : nullptr; }
    static bool IsGamePaused(UWorld* World) { return World->Paused; }
};
struct UMovieSceneSequencePlayer : UObject
{
    bool Playing = true, Paused = false;
    int Stops = 0;
    int Position = 23, DialogueCloses = 0, Jumps = 0;
    int Plays = 0;
    UWorld* World = nullptr;
    Event OnPlay, OnPlayReverse, OnFinished, OnStop;
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
    void StopAtCurrentTime() { ++Stops; Playing = Paused = false; OnStop.Broadcast(); }
    void Play() { ++Plays; Playing = true; Paused = false; OnPlay.Broadcast(); }
};
struct ALevelSequenceActor : UObject
{
    UMovieSceneSequencePlayer* Player = nullptr;
    UWorld* World = nullptr;
    struct Settings { bool bDisableMovementInput = false, bDisableLookAtInput = false; } PlaybackSettings;
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
struct UReditusSequenceSkipNotice : UObject
{
    int Removals = 0, BlockedNotices = 0;
    bool OnScreen = false, AllowViewportAttach = true;
    std::string Message;
    float Progress = -1;
    UWorld* World = nullptr;
    UWorld* GetWorld() const { return World; }
    bool IsInViewport() const { return OnScreen; }
    void AddToViewport(int) { OnScreen = AllowViewportAttach; }
    void RemoveFromParent() { OnScreen = false; ++Removals; }
    void SetMessage(const char* Text)
    {
        Message = Text;
        Progress = -1;
        if (Message == "This scene can't be skipped.") ++BlockedNotices;
    }
    void SetProgress(float Value) { Progress = Value; }
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
    TWeakObjectPtr<ALevelSequenceActor> LastUnregisteredSequence;
    TWeakObjectPtr<APlayerController> PendingInputController;
    bool bUsesControllerSequence = false, bPlaybackStarted = false;
    bool bSequenceMayOwnMoveLock = false, bSequenceMayOwnLookLock = false;
    bool bRestoreLegacyInput = false, bRestoreMoveInput = false, bRestoreLookInput = false;
    bool bContinueRequested = false, bPlaySequenceCue = false;
    float LastTriggerValue = 0;
    double HeldSeconds = 0, NoticeSeconds = 0, LastTime = 0;
    UWorld* GetTickableGameObjectWorld() const { return World; }
    UGameInstance* GetGameInstance() const { return World->Controller->Instance; }
    void RegisterInput() {}
    bool CanCapture() const { return Capture && HasActiveSequence(); }
    UReditusSequenceSkipNotice* EnsureNoticeWidget();
    bool ShowSkipPrompt();
    void ShowBlockedNotice();
    void ShowHoldProgress(float Value);
    void HideHoldProgress();
    void RestoreGameplayInput();
    void DismissSequenceDialogue()
    {
        auto* Actor = Cast<ALevelSequenceActor>(World->Controller->CurrentSequence);
        assert(Actor && Actor->Player);
        ++Actor->Player->DialogueCloses;
    }
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
    void SequenceStarted();
    void SequenceEnded();
    bool BeginAttempt();
    bool HandleKey(const FKey&, bool, bool = false);
    bool HandleTrigger(float);
    void SetMouseHeld(bool);
    void Tick(float);
};
APlayerController* UGameplayStatics::GetPlayerController(const UObject* Object, int)
{
    const auto* Game = GetGameInstance(Object);
    return Game && Game->System ? GetPlayerController(Game->System->World, 0) : nullptr;
}
bool IsLegacyInputBlocked(APlayerController* Controller) { return IsValid(Controller) && Controller->LegacyBlocked; }
void UnblockLegacyInput(APlayerController* Controller) { Controller->LegacyBlocked = false; }
template<class T> T* CreateWidget(APlayerController* Player)
{
    auto& Popup = Player->Instance->System->Popup;
    Popup.World = Player->World;
    return &Popup;
}
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
// ACTUAL_NOTICE_LIFECYCLE
// ACTUAL_RESTORE_INPUT
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
    int Handoffs = 0, FinishListeners = 0, NewFinishListeners = 0;
    FReditusSequenceSkipHandoff Finish{[&]() { ++Handoffs; }, &Handoffs};

    assert(!System.Configure(&OtherActor, true, Finish));
    // Reproduce the uploaded PlayLevelSequence graph: Current Sequence is set,
    // Sequence Actor is unwired, and the authored On Skipped is connected.
    Controller.CurrentSequence = &Actor;
    assert(UReditusSequenceSkipLibrary::ConfigureLevelSequenceSkipping(&Controller, nullptr, Finish, false));
    assert(System.SequencePlayer == &Player && !Controller.bShowMouseCursor);
    assert(!System.Popup.OnScreen);
    assert(!System.HandleKey(EKeys::Other, true));
    assert(System.HandleKey(EKeys::LeftShift, true));
    Advance(System, 12);
    assert(Player.Stops == 0 && Handoffs == 0 && System.Popup.BlockedNotices == 1 && !System.Popup.OnScreen);
    assert(System.HandleKey(EKeys::LeftShift, true, true));
    assert(System.Popup.BlockedNotices == 1);
    System.HandleKey(EKeys::LeftShift, false);

    System.HandleTrigger(.8f);
    System.HandleTrigger(.8f);
    assert(System.Popup.BlockedNotices == 2);
    Advance(System);
    assert(Player.Stops == 0);
    System.HandleTrigger(0);
    System.SetMouseHeld(true);
    assert(System.Popup.BlockedNotices == 3);
    Advance(System);
    assert(Player.Stops == 0 && Handoffs == 0);
    System.SetMouseHeld(false);

    // Enabling skip requires an authored completion callback.
    assert(!System.Configure(&Actor, true, {}));
    System.HandleKey(EKeys::LeftShift, true);
    Advance(System);
    assert(Handoffs == 0 && Player.Stops == 0);

    Player.Playing = false;
    Actor.PlaybackSettings = {true, true};
    Controller.MoveLocks = 0;
    Controller.LookLocks = 1; // The sequence does not own this pre-existing lock.
    UObject CallingActor;
    CallingActor.Instance = &Instance;
    assert(UReditusSequenceSkipLibrary::ConfigureLevelSequenceSkipping(&CallingActor, nullptr, Finish, true));
    Advance(System, 12);
    assert(System.SequenceActor == &Actor && !System.HasActiveSequence());
    assert(!System.Popup.OnScreen);
    // Playback itself exposes the prompt before any dialogue exists or a tick runs.
    Player.Playing = true;
    Controller.MoveLocks = 1;
    Player.OnPlay.Broadcast();
    assert(System.Popup.OnScreen && System.Popup.Message == "Hold Shift or RT to skip");
    System.Popup.RemoveFromParent();
    System.Popup.AllowViewportAttach = false;
    Advance(System, 1);
    assert(!System.Popup.OnScreen);
    System.Popup.AllowViewportAttach = true;
    Advance(System, 1);
    assert(System.Popup.OnScreen);
    Advance(System, 12);
    assert(System.Popup.OnScreen && System.Popup.Progress < 0.f);
    // The exported controller binds the same callback after Play. A stop
    // listener can replace the live bindings before the old handoff runs.
    Player.OnFinished.Authored = {Finish, {[&]() {
        assert(!Player.Playing && !System.Popup.OnScreen && !Controller.GameOnly && !Controller.InputEnabled);
        Controller.Exploring = true;
        ++FinishListeners;
    }, &FinishListeners}};
    Player.OnStop.Authored = {{[&]() {
        Player.OnFinished.Authored = {{[&]() { ++NewFinishListeners; }, &NewFinishListeners}};
    }, nullptr}};
    System.HandleKey(EKeys::LeftShift, true);
    Advance(System, 2);
    assert(Player.Stops == 0 && System.Popup.Progress > .39f && System.Popup.Progress < .41f);
    assert(Player.DialogueCloses == 0 && Player.Position == 23); // Holding fills before touching dialogue/timeline.
    System.HandleKey(EKeys::LeftShift, false);
    Advance(System);
    assert(Player.Stops == 0 && !System.bShowingProgress && System.Popup.OnScreen && System.Popup.Progress < 0.f);
    Controller.Exploring = false;
    Controller.LegacyBlocked = true;
    System.HandleKey(EKeys::LeftShift, true);
    Advance(System);
    assert(Player.Stops == 1 && Handoffs == 1 && !System.HasActiveSequence());
    assert(Player.Position == 199 && Player.Jumps == 1 && Player.DialogueCloses == 1);
    assert(FinishListeners == 1 && NewFinishListeners == 0);
    Advance(System, 1);
    assert(Controller.GameOnly && Controller.InputEnabled && Controller.InputModeRestores == 1);
    assert(!Controller.LegacyBlocked && Controller.MoveLocks == 0 && Controller.LookLocks == 1);
    Player.OnStop.Authored.clear();
    assert(System.HandleKey(EKeys::LeftShift, true, true)); // Held repeats cannot reach the old close-dialogue action.
    Advance(System, 10);
    assert(Player.Stops == 1 && Handoffs == 1);
    System.HandleKey(EKeys::LeftShift, false);

    // Distinct skip callbacks keep their own handoff and omit finish listeners.
    Player.Paused = true;
    assert(System.Configure(&Actor, true, Finish));
    System.HandleTrigger(.8f);
    Advance(System);
    assert(Player.Stops == 2 && Handoffs == 2);
    assert(FinishListeners == 1 && NewFinishListeners == 0);
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
    assert(System.Popup.OnScreen);
    Player.OnFinished.Authored.clear();
    Player.Playing = false;
    Player.OnFinished.Broadcast();
    assert(!System.OnSkipped.IsBound() && !System.Popup.OnScreen && Handoffs == 3);

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

    // If stopping the old scene opens another dialogue or starts a new scene,
    // the old input mode cleanup must leave that new presentation in control.
    System.Capture = true;
    Controller.CurrentSequence = &Actor;
    Controller.InputEnabled = Controller.GameOnly = false;
    const int RestoresBefore = Controller.InputModeRestores;
    bool HandoffRan = false;
    FReditusSequenceSkipHandoff LastFinish{[&]() { HandoffRan = true; }, &HandoffRan};
    assert(System.Configure(&Actor, true, LastFinish));
    Player.OnStop.Authored = {{[&]() { Controller.DialogueWidget = &Replacement; }, nullptr}};
    System.HandleKey(EKeys::LeftShift, true);
    Advance(System);
    assert(HandoffRan && !Controller.InputEnabled && Controller.InputModeRestores == RestoresBefore);
    System.HandleKey(EKeys::LeftShift, false);

    Controller.DialogueWidget = nullptr;
    Player.Playing = true;
    HandoffRan = false;
    assert(System.Configure(&Actor, true, LastFinish));
    OtherActor.World = OtherPlayer.World = &World;
    OtherPlayer.Playing = false;
    Player.OnStop.Authored = {{[&]() {
        Controller.CurrentSequence = &OtherActor;
        assert(System.Configure(&OtherActor, true, LastFinish));
    }, nullptr}};
    System.HandleKey(EKeys::LeftShift, true);
    Advance(System, 5);
    // Floating point accumulation may reach one second on the next tick.
    if (!HandoffRan) Advance(System, 1);
    assert(HandoffRan && System.SequenceActor == &OtherActor && Controller.InputModeRestores == RestoresBefore);
    OtherPlayer.Playing = true;
    OtherPlayer.OnPlayReverse.Broadcast();
    assert(System.Popup.OnScreen);
    OtherPlayer.Playing = false;
    OtherPlayer.OnStop.Broadcast();
    assert(!System.Popup.OnScreen && !System.SequenceActor);

    // A new lock explicitly taken by the authored finish graph stays in place.
    Controller.CurrentSequence = &Actor;
    assert(System.Configure(&Actor, true, {[&]() { Controller.LegacyBlocked = true; }, &Controller}));
    Player.OnStop.Authored.clear();
    Player.Playing = true;
    Player.OnPlay.Broadcast();
    System.HandleKey(EKeys::LeftShift, false);
    System.HandleKey(EKeys::LeftShift, true);
    Advance(System, 7);
    assert(Controller.LegacyBlocked && !Controller.InputEnabled && Controller.InputModeRestores == RestoresBefore);
    return 0;
}
