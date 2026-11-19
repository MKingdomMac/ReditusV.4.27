#include "ReditusDialoguePresentationPolicy.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <cmath>

#define TEXT(Value) Value
using TCHAR = char;

struct FKey
{
    int Value;
    bool IsGamepadKey() const { return Value >= 100; }
    bool operator==(FKey Other) const { return Value == Other.Value; }
};
namespace EKeys
{
    const FKey Enter{1}, F6{2}, F7{3}, LeftShift{4}, RightShift{5}, Escape{6}, Up{7}, Down{8};
    const FKey Gamepad_FaceButton_Top{100}, Gamepad_FaceButton_Left{101}, Gamepad_RightTrigger{102};
    const FKey Gamepad_FaceButton_Right{103}, Gamepad_DPad_Up{104}, Gamepad_DPad_Down{105};
    const FKey Gamepad_RightTriggerAxis{106}, Gamepad_LeftY{107};
}
struct FMath
{
    static float Abs(float Value) { return std::abs(Value); }
    static float Max(float Left, float Right) { return Left > Right ? Left : Right; }
};
struct FString
{
    std::string Value;
    FString(const char* Text) : Value(Text) {}
    static FString Printf(const char* Format, ...)
    {
        char Buffer[256];
        va_list Args;
        va_start(Args, Format);
        std::vsnprintf(Buffer, sizeof(Buffer), Format, Args);
        va_end(Args);
        return Buffer;
    }
};
struct FText
{
    std::string Value;
    static FText FromString(FString Text) { return {Text.Value}; }
};
struct FLinearColor
{
    float R, G, B, A;
    FLinearColor(float Red, float Green, float Blue, float Alpha) : R(Red), G(Green), B(Blue), A(Alpha) {}
    static const FLinearColor White;
};
const FLinearColor FLinearColor::White(1, 1, 1, 1);
struct FSlateColor
{
    FLinearColor Value;
    explicit FSlateColor(FLinearColor Color) : Value(Color) {}
};
enum class ESlateVisibility { Visible, Collapsed };
struct UObject {};
template<class T> bool IsValid(T* Object) { return Object != nullptr; }
struct PlayerController;
struct UWidget : UObject
{
    ESlateVisibility Visibility = ESlateVisibility::Visible;
    void SetVisibility(ESlateVisibility Value) { Visibility = Value; }
};
struct UUserWidget : UWidget
{
    PlayerController* Owner = nullptr;
    UUserWidget* Parent = nullptr;
    bool bIsFocusable = false, InViewport = true, Enabled = true;
    bool IsInViewport() const { return InViewport; }
    bool GetIsEnabled() const { return Enabled; }
    PlayerController* GetOwningPlayer() const { return Owner; }
    bool HasUserFocus(PlayerController*) const;
    bool HasUserFocusedDescendants(PlayerController*) const;
    void SetUserFocus(PlayerController*);
};
struct UDialogueUserWidget : UUserWidget {};
struct WeakDialogue
{
    UDialogueUserWidget* Value = nullptr;
    UDialogueUserWidget* Get() const { return Value; }
};
struct UButton
{
    bool Enabled = true;
    void SetIsEnabled(bool Value) { Enabled = Value; }
};
struct UTextBlock : UWidget
{
    std::string Text;
    FLinearColor Color = FLinearColor::White;
    void SetText(FText Value) { Text = Value.Value; }
    void SetColorAndOpacity(FSlateColor Value) { Color = Value.Value; }
};
struct Scroll
{
    float Offset = 0;
    float GetScrollOffset() const { return Offset; }
    void SetScrollOffset(float Value) { Offset = Value; }
    void ScrollToEnd() { Offset = 100; }
};
struct PlayerController : UObject
{
    UUserWidget* FocusedWidget = nullptr;
    UObject* DialogueWidget = nullptr;
    bool Local = true;
    bool IsLocalController() const { return Local; }
    float GetInputAnalogKeyState(FKey) const { return 0; }
};
bool UUserWidget::HasUserFocus(PlayerController* Player) const { return Player->FocusedWidget == this; }
bool UUserWidget::HasUserFocusedDescendants(PlayerController* Player) const
{
    for (auto* Focus = Player->FocusedWidget; Focus; Focus = Focus->Parent)
        if (Focus == this) return Player->FocusedWidget != this;
    return false;
}
void UUserWidget::SetUserFocus(PlayerController* Player)
{
    if (bIsFocusable) Player->FocusedWidget = this;
}
struct FSlateApplication
{
    static bool Active;
    static bool IsInitialized() { return true; }
    static const FSlateApplication& Get() { static FSlateApplication App; return App; }
    bool IsActive() const { return Active; }
};
bool FSlateApplication::Active = true;
namespace ReditusQuestNPCReflection
{
    UObject* ReadObject(PlayerController* Player, const char*) { return Player->DialogueWidget; }
}
namespace ReditusDialogueUI
{
    bool ConfirmKey(FKey Key) { return Key == EKeys::Enter; }
    bool Shown(const UWidget* Widget) { return Widget && Widget->Visibility == ESlateVisibility::Visible; }
    struct SkipSystem
    {
        bool Active = false;
        bool Auto = false;
        int Notices = 0, KeyRequests = 0, AnalogRequests = 0;
        bool MouseHeld = false;
        bool HasActiveSequence() const { return Active; }
        bool HandleKey(const FKey&, bool, bool = false) { ++KeyRequests; return Active; }
        bool HandleTrigger(float) { ++AnalogRequests; return Active; }
        void ShowBlockedNotice() { ++Notices; }
        void SetMouseHeld(bool Value) { MouseHeld = Value; }
        void SetAutoAdvance(void*, bool Value) { Auto = Value; }
    };
    SkipSystem ConfiguredScene;
    SkipSystem* SequenceSkip(const void*) { return &ConfiguredScene; }
    int SequenceToken = 0;
    void* SequencePlayer(UDialogueUserWidget*) { return &SequenceToken; }
}
struct UReditusDialoguePresentationWidget : UUserWidget
{
    using Snapshot = ReditusDialoguePresentationPolicy::Snapshot;
    Snapshot State;
    bool UsingGamepad = false, KeyHeld = false, TriggerHeld = false, MouseHeld = false, NeedTriggerRelease = false;
    bool bPresentationReady = true;
    ReditusDialoguePresentationPolicy::Controls Playback;
    UButton AdvanceFace, Auto, Log, Skip;
    UTextBlock AutoText, AdvanceText, LogText, SkipText, StatusText;
    UWidget SequenceWidget, LogWidget;
    Scroll LogScroller;
    PlayerController ControllerInstance;
    UDialogueUserWidget DialogueInstance;
    WeakDialogue Source{&DialogueInstance};
    UButton *AdvanceButton = &AdvanceFace, *AutoButton = &Auto, *LogButton = &Log, *SkipButton = &Skip;
    UTextBlock *AutoLabel = &AutoText, *AdvanceLabel = &AdvanceText, *LogLabel = &LogText, *SkipLabel = &SkipText;
    UTextBlock* StatusLabel = &StatusText;
    UWidget *SequenceControls = &SequenceWidget, *LogLayer = &LogWidget;
    Scroll* LogScroll = &LogScroller;
    UReditusDialoguePresentationWidget()
    {
        Owner = &ControllerInstance;
        Parent = &DialogueInstance;
        bIsFocusable = true;
        DialogueInstance.Owner = &ControllerInstance;
        ControllerInstance.DialogueWidget = &DialogueInstance;
        ControllerInstance.FocusedWidget = this;
    }
    bool CanRouteInput() const;
    bool InputAllowed() const { return CanRouteInput() && !State.Blocked; }
    Snapshot ReadSnapshot() const { auto Value = State; Value.Active = CanRouteInput(); return Value; }
    PlayerController* GetOwningPlayer() { return &ControllerInstance; }
    void RestoreSourceFocus();
    void ToggleAuto();
    void ToggleLog();
    void SkipPressed();
    void SkipReleased();
    int Advances = 0;
    void Advance() { if (InputAllowed()) ++Advances; }
    void UpdateHeld();
    bool RouteKeyDown(const FKey& Key, bool Repeat);
    bool RouteKeyUp(const FKey& Key);
    bool RouteAnalog(const FKey& Key, float Value);
    void RefreshControls();
};

// ACTUAL_CAN_ROUTE
// ACTUAL_RESTORE_FOCUS
// ACTUAL_TOGGLE_AUTO
// ACTUAL_TOGGLE_LOG
// ACTUAL_UPDATE_HELD
// ACTUAL_SKIP_BUTTON
// ACTUAL_ROUTE_KEYS
// ACTUAL_ROUTE_ANALOG
// ACTUAL_REFRESH_CONTROLS

int main()
{
    UReditusDialoguePresentationWidget UI;
    UI.State.Active = true;
    UI.State.Blocked = false;
    UI.State.InLevelSequence = true;
    UI.State.ContinueReady = true;
    UI.State.HasDialogueNode = true;
    UI.ControllerInstance.FocusedWidget = nullptr;
    UI.DialogueInstance.SetUserFocus(&UI.ControllerInstance);
    assert(!UI.CanRouteInput()); // The Blueprint makes its own widget non-focusable.
    UI.RestoreSourceFocus();
    assert(UI.CanRouteInput() && UI.ControllerInstance.FocusedWidget == &UI);
    UI.RefreshControls();
    assert(UI.AdvanceText.Text == "ENTER  Advance" && UI.SkipText.Text == "SHIFT  Hold skip");
    assert(UI.AutoText.Text == "F6  Auto Off" && UI.LogText.Text == "F7  Log");

    // A stale controller display must correct itself even while the briefing owns focus.
    UI.ControllerInstance.FocusedWidget = nullptr;
    UI.UsingGamepad = true;
    assert(!UI.RouteKeyDown(EKeys::Enter, false));
    UI.RefreshControls();
    assert(!UI.UsingGamepad && UI.AdvanceText.Text == "ENTER  Advance");
    assert(!UI.RouteAnalog(EKeys::Gamepad_LeftY, .01f));
    assert(!UI.UsingGamepad);
    assert(!UI.RouteAnalog(EKeys::Gamepad_LeftY, .8f));
    UI.RefreshControls();
    assert(UI.UsingGamepad && UI.AdvanceText.Text == "A  Advance");
    UI.RouteKeyDown(EKeys::Enter, false);
    UI.RouteAnalog(EKeys::Gamepad_LeftY, .02f);
    assert(!UI.UsingGamepad);

    UI.RestoreSourceFocus();
    assert(UI.RouteKeyDown(EKeys::F6, false) && UI.Playback.Auto);
    assert(UI.RouteKeyDown(EKeys::F6, true) && UI.Playback.Auto);
    const int NoticesBefore = ReditusDialogueUI::ConfiguredScene.Notices;
    assert(UI.RouteKeyDown(EKeys::LeftShift, false) && !UI.KeyHeld);
    assert(ReditusDialogueUI::ConfiguredScene.Notices == NoticesBefore + 1);
    assert(UI.RouteKeyUp(EKeys::LeftShift) && !UI.KeyHeld);
    UI.RouteAnalog(EKeys::Gamepad_RightTriggerAxis, .8f);
    UI.RefreshControls();
    assert(!UI.TriggerHeld && UI.UsingGamepad && UI.SkipText.Text == "RT  Hold skip");
    UI.RouteAnalog(EKeys::Gamepad_RightTriggerAxis, 0);
    assert(!UI.TriggerHeld);
    UI.RouteKeyDown(EKeys::Enter, false);

    // Bright, readable waiting captions must retain the original input gates.
    UI.State.Blocked = true;
    UI.RefreshControls();
    assert(!UI.AdvanceFace.Enabled && UI.Log.Enabled && UI.Skip.Enabled);
    assert(UI.SkipText.Color.R >= .75f && UI.SkipText.Color.A == 1.f);
    UI.State.Blocked = false;
    UI.RefreshControls();
    assert(UI.AdvanceFace.Enabled && UI.Log.Enabled && UI.Skip.Enabled);
    assert(UI.SkipText.Color.R == 1.f);

    UI.State.Blocked = true;
    assert(UI.RouteKeyDown(EKeys::F7, false) && UI.Playback.Log);
    assert(UI.LogWidget.Visibility == ESlateVisibility::Visible);
    assert(UI.RouteKeyDown(EKeys::F7, false) && !UI.Playback.Log);
    UI.State.Blocked = false;

    UI.State.Choices = 2;
    UI.RefreshControls();
    assert(!UI.AdvanceFace.Enabled && UI.Skip.Enabled && UI.Log.Enabled);
    UI.Playback.SetHeld(true);
    assert(!UI.Playback.Tick(.25, UI.State));
    UI.State.Choices = 0;
    assert(!UI.Playback.Tick(.25, UI.State)); // Requires releasing skip after a choice.

    UI.State.InLevelSequence = false;
    UI.RefreshControls();
    assert(UI.SequenceWidget.Visibility == ESlateVisibility::Collapsed);
    assert(!UI.Playback.Auto);

    // A configured full-scene skip owns Shift/RT. No line-fast-forward fallback.
    UI.State.InLevelSequence = true;
    UI.State.PlaySequenceCue = true;
    ReditusDialogueUI::ConfiguredScene.Active = true;
    UI.RefreshControls();
    assert(UI.Skip.Enabled);
    UI.KeyHeld = UI.TriggerHeld = false;
    assert(UI.RouteKeyDown(EKeys::LeftShift, false) && !UI.KeyHeld);
    assert(UI.RouteAnalog(EKeys::Gamepad_RightTriggerAxis, .8f) && !UI.TriggerHeld);
    assert(ReditusDialogueUI::ConfiguredScene.KeyRequests > 0 && ReditusDialogueUI::ConfiguredScene.AnalogRequests > 0);
    UI.SkipPressed();
    assert(ReditusDialogueUI::ConfiguredScene.MouseHeld && !UI.MouseHeld);
    UI.SkipReleased();
    assert(!ReditusDialogueUI::ConfiguredScene.MouseHeld);
    ReditusDialogueUI::ConfiguredScene.Active = false;
    UI.SkipPressed();
    assert(!UI.MouseHeld && ReditusDialogueUI::ConfiguredScene.Notices > NoticesBefore);
    UI.State.PlaySequenceCue = false;
    const int AdvanceBefore = UI.Advances;
    assert(UI.RouteKeyDown(EKeys::Enter, false) && UI.Advances == AdvanceBefore + 1);

    UUserWidget NextScreen;
    UI.ControllerInstance.DialogueWidget = &NextScreen;
    UI.ControllerInstance.FocusedWidget = &NextScreen;
    UI.RestoreSourceFocus();
    assert(UI.ControllerInstance.FocusedWidget == &NextScreen && !UI.CanRouteInput());
    return 0;
}
