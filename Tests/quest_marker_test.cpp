#include "ReditusQuestMarkerPolicy.h"
#include "MapQuestMarkerPolicy.h"
#include <cassert>
#include <string>
#include <vector>

// Small reflected-data stand-ins; the adapter below is extracted unchanged
// from ReditusQuestSubsystem.cpp. These are not substitutes for a UHT build.
struct FName { std::string Value; bool IsNone() const { return Value.empty(); } bool operator==(const FName& Other) const { return Value == Other.Value; } };
struct FDataTableRowHandle { bool DataTable = true; FName RowName{"Quest"}; };
enum class EReditusQuestState { Unknown, Unavailable, Available, Active, ReadyToDeliver, Completed, Failed };
enum class EReditusQuestMarkerVisibility { InheritQuest, Show, Hide };
struct FReditusQuestMarkerRules {
    bool ShowWhenAvailable = true, ShowWhileActive = true, ShowWhenReadyToTurnIn = true;
    bool ShowOnMinimap = true, ShowOverhead = true;
};
struct Stage { FName StageID{"Current"}; EReditusQuestMarkerVisibility NPCMarkerVisibility = EReditusQuestMarkerVisibility::InheritQuest; };
struct FReditusQuestDefinition { FReditusQuestMarkerRules Markers; bool IsMainQuest = false; std::string Title = "Title"; std::vector<Stage> Stages{Stage{}}; };
struct FReditusQuestSnapshot { EReditusQuestState State = EReditusQuestState::Available; FName StageID{"Current"}; };
struct FReditusQuestMarkerState { bool Visible = false, MinimapVisible = false, OverheadVisible = false, IsMainQuest = false; std::string Title; };
static FReditusQuestDefinition Authored;
static bool Valid = true;
static const FReditusQuestDefinition* Definition(FDataTableRowHandle) { return Valid ? &Authored : nullptr; }
struct UReditusQuestSubsystem {
    FReditusQuestSnapshot Current;
    bool ReadQuest(FDataTableRowHandle, FReditusQuestSnapshot& Snapshot) { Snapshot = Current; return Valid; }
    bool GetQuestNPCMarkerState(FDataTableRowHandle Quest, FReditusQuestMarkerState& Marker);
};
// ACTUAL_MARKER_ADAPTER

int main()
{
    UReditusQuestSubsystem System; FReditusQuestMarkerState Marker; FDataTableRowHandle Quest;
    // Quest type determines both symbols at every lifecycle state, never turn-in.
    for (bool Main : {false, true}) for (int State = 0; State != 7; ++State)
    {
        Authored.IsMainQuest = Main; System.Current.State = static_cast<EReditusQuestState>(State);
        assert(System.GetQuestNPCMarkerState(Quest, Marker));
        const bool Expected = State == 2 || State == 3 || State == 4;
        assert(Marker.Visible == Expected && Marker.MinimapVisible == Expected && Marker.OverheadVisible == Expected);
        assert(Marker.IsMainQuest == Main && Marker.Title == "Title");
        assert(MapQuestMarkerPolicy::Symbol(Marker.IsMainQuest) == (Main ? '!' : '?'));
    }
    System.Current.State = EReditusQuestState::Active;
    Authored.Markers.ShowWhileActive = false;
    System.GetQuestNPCMarkerState(Quest, Marker); assert(!Marker.Visible);
    Authored.Stages[0].NPCMarkerVisibility = EReditusQuestMarkerVisibility::Show;
    System.GetQuestNPCMarkerState(Quest, Marker); assert(Marker.Visible);
    Authored.Markers.ShowWhileActive = true;
    Authored.Stages[0].NPCMarkerVisibility = EReditusQuestMarkerVisibility::Hide;
    System.GetQuestNPCMarkerState(Quest, Marker); assert(!Marker.Visible);
    Authored.Stages[0].NPCMarkerVisibility = EReditusQuestMarkerVisibility::Show;
    // Explicit stage Show must not leak a marker after completion/failure/lock.
    for (auto State : {EReditusQuestState::Completed, EReditusQuestState::Failed, EReditusQuestState::Unavailable})
    { System.Current.State = State; System.GetQuestNPCMarkerState(Quest, Marker); assert(!Marker.Visible); }
    System.Current.State = EReditusQuestState::ReadyToDeliver;
    Authored.Markers.ShowWhenReadyToTurnIn = false;
    System.GetQuestNPCMarkerState(Quest, Marker); assert(!Marker.Visible);
    System.Current.State = EReditusQuestState::Available;
    Authored.Markers.ShowWhenAvailable = false;
    System.GetQuestNPCMarkerState(Quest, Marker); assert(!Marker.Visible);
    Authored.Markers.ShowWhenAvailable = true; Authored.Markers.ShowOnMinimap = false;
    System.GetQuestNPCMarkerState(Quest, Marker); assert(Marker.Visible && !Marker.MinimapVisible && Marker.OverheadVisible);
    Authored.Markers.ShowOverhead = false; Authored.Markers.ShowOnMinimap = true;
    System.GetQuestNPCMarkerState(Quest, Marker); assert(Marker.MinimapVisible && !Marker.OverheadVisible);
    Valid = false;
    assert(!System.GetQuestNPCMarkerState(Quest, Marker)); assert(!Marker.Visible && !Marker.MinimapVisible && !Marker.OverheadVisible);
    Valid = true; Quest.RowName.Value.clear();
    assert(!System.GetQuestNPCMarkerState(Quest, Marker)); assert(!Marker.Visible);
    Quest.RowName.Value = "Quest"; Quest.DataTable = false;
    assert(!System.GetQuestNPCMarkerState(Quest, Marker));
    // Real default artwork: distinct symbols, transparent border, body + outline.
    const auto Main = MapQuestMarkerPolicy::Raster(true), Side = MapQuestMarkerPolicy::Raster(false);
    assert(Main != Side);
    for (auto Pixels : {Main, Side})
    {
        int Body = 0, Outline = 0, Clear = 0;
        for (int Y = 0; Y < MapQuestMarkerPolicy::Height; ++Y) for (int X = 0; X < MapQuestMarkerPolicy::Width; ++X)
        {
            auto P = Pixels[unsigned(Y * MapQuestMarkerPolicy::Width + X)];
            assert(P <= 2); Body += P == 2; Outline += P == 1; Clear += P == 0;
            if (X == 0 || X == MapQuestMarkerPolicy::Width-1 || Y == 0 || Y == MapQuestMarkerPolicy::Height-1) assert(P == 0);
        }
        assert(Body > 0 && Outline > 0 && Clear > Body);
    }
}
