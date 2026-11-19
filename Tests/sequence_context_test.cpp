// Tests the actual sequence lookup against the exported object ownership.
// These small objects do not simulate Unreal playback, Slate or Blueprint execution.
#include <cassert>
#include <map>
#include <string>
#include <functional>
#define TEXT(Value) Value
struct UWorld {};
struct UObject
{
    virtual ~UObject() = default;
    std::map<std::string, UObject*> Fields;
    bool Valid = true;
    UWorld* World = nullptr;
    UWorld* GetWorld() const { return World; }
};
struct APlayerController : UObject
{
    UObject* CurrentSequence = nullptr;
};
struct UMovieSceneSequencePlayer : UObject
{
    bool Playing = false;
    bool Paused = false;
    int Resumes = 0;
    void Play() { ++Resumes; Playing = true; Paused = false; }
    bool IsPlaying() const { return Playing; }
    bool IsPaused() const { return Paused; }
};
struct UDialogueUserWidget : UObject
{
    APlayerController* Owner = nullptr;
    bool InViewport = true, Visible = true;
    std::function<void()> OnContinue;
    bool IsInViewport() const { return InViewport; }
    APlayerController* GetOwningPlayer() const { return Owner; }
};
template<class T> T* Cast(UObject* Object) { return dynamic_cast<T*>(Object); }
bool IsValid(const UObject* Object) { return Object != nullptr && Object->Valid; }
template<class T> struct TWeakObjectPtr
{
    T* Value = nullptr;
    TWeakObjectPtr(T* Object) : Value(Object) {}
    bool IsValid() const { return ::IsValid(Value); }
    T* Get() const { return Value; }
    T* operator->() const { return Value; }
};
namespace ReditusQuestNPCReflection
{
    UObject* ReadObject(UObject* Object, const char* Name)
    {
        if (!Object) return nullptr;
        const auto Found = Object->Fields.find(Name);
        return Found != Object->Fields.end() ? Found->second : nullptr;
    }
    bool CallNoArguments(UDialogueUserWidget* Widget, const char* Function)
    {
        assert(std::string(Function) == "SimulateNextClicked" || std::string(Function) == "MenuConfirm");
        if (Widget->OnContinue) Widget->OnContinue();
        return true;
    }
}
namespace ReditusDialogueUI
{
    struct SequenceSystem
    {
        UDialogueUserWidget* Requested = nullptr;
        UMovieSceneSequencePlayer* FindSequencePlayer(APlayerController* Controller)
        {
            return Controller ? Cast<UMovieSceneSequencePlayer>(Controller->CurrentSequence) : nullptr;
        }
        void DialogueContinueRequested(UDialogueUserWidget* Widget) { Requested = Widget; }
    };
    SequenceSystem System;
    template<class Context> SequenceSystem* SequenceSkip(Context*) { return &System; }
    // ACTUAL_SEQUENCE_LOOKUP
}
struct UReditusDialoguePresentationWidget
{
    TWeakObjectPtr<UDialogueUserWidget> Source{nullptr};
    void ContinueDialogue(bool Manual = false);
};
// ACTUAL_CONTINUE_DIALOGUE
using ReditusDialogueUI::SequencePlayer;
int main()
{
    UDialogueUserWidget Widget;
    APlayerController Controller;
    UObject Battle, Wrong;
    UMovieSceneSequencePlayer BattleSequence, ExploreSequence;
    Widget.Fields["JrpgController"] = &Controller;
    Controller.Fields["battleController"] = &Battle;
    Battle.Fields["CurrentLevelSequence"] = &BattleSequence;

    BattleSequence.Playing = true;
    assert(SequencePlayer(&Widget) == &BattleSequence);
    BattleSequence.Playing = false; BattleSequence.Paused = true;
    assert(SequencePlayer(&Widget) == &BattleSequence);
    BattleSequence.Paused = false;
    assert(SequencePlayer(&Widget) == nullptr);

    // A registered or controller-owned exploration sequence takes priority.
    Controller.CurrentSequence = &ExploreSequence;
    assert(SequencePlayer(&Widget) == &ExploreSequence);
    Widget.Fields.erase("JrpgController"); Widget.Owner = &Controller;
    assert(SequencePlayer(&Widget) == &ExploreSequence);
    Controller.CurrentSequence = nullptr;
    Battle.Fields["CurrentLevelSequence"] = &Wrong;
    assert(SequencePlayer(&Widget) == nullptr);
    assert(SequencePlayer(nullptr) == nullptr);

    UReditusDialoguePresentationWidget UI; UI.Source = &Widget;
    int Continues = 0;
    Widget.OnContinue = [&]() {
        assert(ReditusDialogueUI::System.Requested == &Widget);
        ++Continues;
    };
    UI.ContinueDialogue();
    ReditusDialogueUI::System.Requested = nullptr;
    UI.ContinueDialogue(true);
    assert(Continues == 2);
}
