// Tests the actual sequence lookup against the exported object ownership.
// These small objects do not simulate Unreal playback, Slate or Blueprint execution.
#include <cassert>
#include <map>
#include <string>
#define TEXT(Value) Value
struct UObject
{
    virtual ~UObject() = default;
    std::map<std::string, UObject*> Fields;
};
struct UMovieSceneSequencePlayer : UObject
{
    bool Playing = false;
    bool Paused = false;
    bool IsPlaying() const { return Playing; }
    bool IsPaused() const { return Paused; }
};
struct UDialogueUserWidget : UObject
{
    UObject* Owner = nullptr;
    UObject* GetOwningPlayer() const { return Owner; }
};
template<class T> T* Cast(UObject* Object) { return dynamic_cast<T*>(Object); }
bool IsValid(const UObject* Object) { return Object != nullptr; }
namespace ReditusQuestNPCReflection
{
    UObject* ReadObject(UObject* Object, const char* Name)
    {
        if (!Object) return nullptr;
        const auto Found = Object->Fields.find(Name);
        return Found != Object->Fields.end() ? Found->second : nullptr;
    }
}
// ACTUAL_SEQUENCE_LOOKUP
int main()
{
    UDialogueUserWidget Widget;
    UObject Controller, Battle, Actor;
    UMovieSceneSequencePlayer BattleSequence, ExploreSequence;
    Widget.Fields["JrpgController"] = &Controller;
    Controller.Fields["battleController"] = &Battle;
    Battle.Fields["CurrentLevelSequence"] = &BattleSequence;

    // No direct battleController member exists on the dialogue widget.
    BattleSequence.Playing = true;
    assert(SequencePlayer(&Widget) == &BattleSequence);
    BattleSequence.Playing = false; BattleSequence.Paused = true;
    assert(SequencePlayer(&Widget) == &BattleSequence);
    BattleSequence.Paused = false;
    assert(SequencePlayer(&Widget) == nullptr); // A retained finished player.

    Controller.Fields["Current Sequence "] = &Actor;
    Actor.Fields["SequencePlayer"] = &ExploreSequence;
    ExploreSequence.Paused = true;
    assert(SequencePlayer(&Widget) == &ExploreSequence);
    ExploreSequence.Paused = false; ExploreSequence.Playing = true;
    assert(SequencePlayer(&Widget) == &ExploreSequence);
    ExploreSequence.Playing = false;
    assert(SequencePlayer(&Widget) == nullptr);

    // Initialization can precede assignment of JrpgController. Use the owner.
    Widget.Fields.erase("JrpgController"); Widget.Owner = &Controller;
    BattleSequence.Playing = true;
    assert(SequencePlayer(&Widget) == &BattleSequence);
    Battle.Fields["CurrentLevelSequence"] = &Actor; // Wrong object type.
    assert(SequencePlayer(&Widget) == nullptr);
    Widget.Owner = nullptr;
    assert(SequencePlayer(&Widget) == nullptr);
    assert(SequencePlayer(nullptr) == nullptr);
}
