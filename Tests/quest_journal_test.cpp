#include "reflection_standins.h"
#include <set>

using FText = std::string;
template<class T> T&& MoveTemp(T& Value) { return std::move(Value); }
template<class T> T* Cast(UObject* Value) { return dynamic_cast<T*>(Value); }

namespace FMath
{
    using std::max;
    template<class T> T Max(T A, T B) { return std::max(A, B); }
    template<class T> T Clamp(T Value, T Min, T Max) { return std::max(Min, std::min(Value, Max)); }
}

struct FTextProperty : FProperty
{
    using FProperty::FProperty;
    FText Value;
    FText GetPropertyValue_InContainer(const UObject*) const { return Value; }
};
struct FArrayProperty : FProperty
{
    using FProperty::FProperty;
    FProperty* Inner = nullptr;
    std::vector<UClass*>* Entries = nullptr;
    template<class T> T* ContainerPtrToValuePtr(APlayerController*) const
    { return reinterpret_cast<T*>(Entries); }
};
struct FScriptArrayHelper
{
    const std::vector<UClass*>& Entries;
    FScriptArrayHelper(const FArrayProperty*, const void* Array)
        : Entries(*static_cast<const std::vector<UClass*>*>(Array)) {}
    int32 Num() const { return int32(Entries.size()); }
    const void* GetRawPtr(int32 Index) const { return &Entries.at(Index); }
};

template<class T> struct TSet : std::set<T>
{
    bool Contains(T Value) const { return this->count(Value) != 0; }
    void Add(T Value) { this->insert(Value); }
};
enum class EReditusQuestJournalPage : uint8 { MainQuests, SideQuests, CompletedQuests, FailedQuests };
struct FReditusQuestJournalEntry
{
    UClass* BlueprintQuest = nullptr;
    FText Title, Description;
    int32 Progress = 0, Goal = 0;
};

// ACTUAL_JOURNAL_PAGE_AND_BLUEPRINT_READER

void DefineQuest(UClass& Quest, const char* Title, bool Main, int32 Goal)
{
    Quest.Add<FTextProperty>(FName("name"), 0, 0)->Value = Title;
    Quest.Add<FTextProperty>(FName("description"), 0, 0)->Value = "Quest description";
    Quest.Add<FBoolProperty>(FName("IsMainQuest"), 0, 0)->SetPropertyValue_InContainer(&Quest.Defaults, Main);
    Quest.Add<FIntProperty>(FName("steps"), 0, 4)->SetPropertyValue_InContainer(&Quest.Defaults, Goal);
}

int main()
{
    using Page = EReditusQuestJournalPage;
    UClass Main, Side, Finished, Invalid;
    DefineQuest(Main, "Meet Theresa", true, 5);
    DefineQuest(Side, "Return Wedge", false, 2);
    DefineQuest(Finished, "Go home", true, 3);
    std::vector<Entry> Active = {
        {&Main, 2}, {&Invalid, 2}, {&Finished, 2, false}, {nullptr, 1}, {&Side, 12}
    };
    std::vector<UClass*> Completed{&Finished, &Finished, nullptr, &Invalid};
    APlayerController Controller;
    FClassProperty ClassValue(FName("QuestClass"), 0, 0);
    FIntProperty StepValue(FName("QuestProgress"), 0, 0);
    auto* Actives = Controller.Class.Add<FMapProperty>(FName("activeQuests"), 0, 0);
    Actives->KeyProp = &ClassValue;
    Actives->ValueProp = &StepValue;
    Actives->Entries = &Active;
    auto* Completions = Controller.Class.Add<FArrayProperty>(FName("completedQuests"), 0, 0);
    Completions->Inner = &ClassValue;
    Completions->Entries = &Completed;

    TArray<FReditusQuestJournalEntry> Rows;
    ReadBlueprintQuests(&Controller, Page::MainQuests, Rows);
    assert(Rows.Num() == 1 && Rows[0].BlueprintQuest == &Main && Rows[0].Title == "Meet Theresa");
    assert(Rows[0].Description == "Quest description" && Rows[0].Progress == 2 && Rows[0].Goal == 5);
    assert(MatchesPage(Page::MainQuests, true, false, false)); // Active graph main quest can share this page.
    assert(!MatchesPage(Page::MainQuests, true, true, false));

    Rows.Reset();
    ReadBlueprintQuests(&Controller, Page::SideQuests, Rows);
    assert(Rows.Num() == 1 && Rows[0].BlueprintQuest == &Side && Rows[0].Progress == 2 && Rows[0].Goal == 2);
    assert(MatchesPage(Page::SideQuests, false, false, false));
    assert(!MatchesPage(Page::SideQuests, true, false, false));
    assert(!MatchesPage(Page::SideQuests, false, false, true));

    Rows.Reset();
    ReadBlueprintQuests(&Controller, Page::CompletedQuests, Rows);
    assert(Rows.Num() == 1 && Rows[0].BlueprintQuest == &Finished && Rows[0].Progress == Rows[0].Goal);
    assert(MatchesPage(Page::CompletedQuests, true, true, false));
    assert(MatchesPage(Page::CompletedQuests, false, false, true));
    assert(!MatchesPage(Page::CompletedQuests, true, false, false));
    assert(MatchesPage(Page::FailedQuests, false, false, true));

    Rows.Reset();
    ReadBlueprintQuests(&Controller, Page::FailedQuests, Rows);
    assert(Rows.Num() == 0);
    Actives->KeyProp = &StepValue;
    ReadBlueprintQuests(&Controller, Page::MainQuests, Rows);
    assert(Rows.Num() == 0);
    assert(Active[0].Value == 2 && Active[4].Value == 12 && Completed.size() == 4 && Controller.Calls.empty());
}
