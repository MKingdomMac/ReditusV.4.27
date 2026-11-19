#include <cassert>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>
struct FName {
    std::string V;bool IsNone()const{return V.empty();}
    bool operator==(const FName& R)const{return V==R.V;}
};
template<class T>struct Weak {T* P=nullptr;T* Get()const{return P;}bool IsValid()const{return P!=nullptr;}T* operator->()const{return P;}};
struct FTimerHandle {int ID=0;};
struct TimerManager {
    int Next=0;std::map<int,std::function<void()>> Work;
    template<class T>FTimerHandle SetTimerForNextTick(T* O,void(T::*Fn)()){int I=++Next;Work[I]=[O,Fn]{(O->*Fn)();};return {I};}
    void ClearTimer(FTimerHandle H){Work.erase(H.ID);}
    void Tick(){const auto Copy=Work;Work.clear();for(const auto& P:Copy)P.second();}
};
struct UReditusQuestNPCComponent;
struct AActor {
    UReditusQuestNPCComponent* NPC=nullptr;
    bool Hidden=false,Collision=true,Destroyed=false;
    bool IsHidden()const{return Hidden;}bool GetActorEnableCollision()const{return Collision;}
    void SetActorHiddenInGame(bool V){Hidden=V;}void SetActorEnableCollision(bool V){Collision=V;}void Destroy(){Destroyed=true;}
    template<class T>T* FindComponentByClass(){return NPC;}
};
struct UWorld {std::vector<AActor*> Actors;TimerManager Timers;TimerManager& GetTimerManager(){return Timers;}};
template<class T>struct TActorIterator {
    UWorld* W;size_t I=0;explicit TActorIterator(UWorld* In):W(In){}
    explicit operator bool()const{return I<W->Actors.size();}void operator++(){++I;}T* operator->()const{return W->Actors[I];}
};
template<class T>bool IsValid(T* P){return P!=nullptr;}
struct Visibility {bool Shown=true;void SetIconVisible(bool V){Shown=V;}void SetVisibility(bool V){Shown=V;}};
struct BattleEntry {struct Request {FName NPCID;Weak<UWorld> RequestWorld;}Value;};
struct UReditusQuestSubsystem {
    static UReditusQuestSubsystem* Current;UWorld* W=nullptr;
    std::vector<BattleEntry> PendingNPCBattles;std::set<std::string> Removed;
    static UReditusQuestSubsystem* GetQuestSystem(void*){return Current;}
    UWorld* GetTickableGameObjectWorld()const{return W;}
    bool WasNPCRemoved(FName ID)const{return Removed.count(ID.V)!=0;}
    bool IsNPCConversationActive(FName)const;
};
UReditusQuestSubsystem* UReditusQuestSubsystem::Current=nullptr;
struct UReditusQuestNPCComponent {
    UWorld* W=nullptr;AActor* Owner=nullptr;FName PersistentNPCID,PendingEncounterID;
    bool bTalking=false,FollowUp=false,bRemovalScheduled=false,bEndingPlay=false;
    bool bHiddenBeforeRemoval=false,bCollisionBeforeRemoval=true,Unique=true;
    Weak<Visibility> MapIcon;Visibility* OverheadQuestMarker=nullptr;FTimerHandle RemovalTimer;
    AActor* GetOwner(){return Owner;}UWorld* GetWorld(){return W;}
    bool ValidatePersistentID(){return Unique;}bool HasPendingBattleFollowUp()const{return FollowUp;}bool IsTalking()const{return bTalking;}
    bool ApplyPersistentRemoval();void DestroyRememberedNPC();void HandlePersistentStateRestored();
    void RefreshQuestNPC(){ApplyPersistentRemoval();}
};
// ACTUAL_NPC_REMOVAL
int main(){
    UWorld W,OldWorld;UReditusQuestSubsystem S;S.W=&W;UReditusQuestSubsystem::Current=&S;
    AActor Actor;UReditusQuestNPCComponent NPC;Actor.NPC=&NPC;NPC.W=&W;NPC.Owner=&Actor;NPC.PersistentNPCID={"speaker"};W.Actors={&Actor};
    Visibility Map,Head;NPC.MapIcon.P=&Map;NPC.OverheadQuestMarker=&Head;
    S.Removed.insert("speaker");assert(NPC.ApplyPersistentRemoval());assert(Actor.Hidden&&!Actor.Collision&&!Actor.Destroyed);
    // A pending battle is registered AFTER removal was queued, before next-tick destruction.
    S.PendingNPCBattles.push_back({{{"speaker"},{&W}}});W.Timers.Tick();
    assert(!Actor.Destroyed&&!Actor.Hidden&&Actor.Collision&&!NPC.bRemovalScheduled&&!Map.Shown&&!Head.Shown);
    assert(S.IsNPCConversationActive({"speaker"})&&S.IsNPCConversationActive({}));
    assert(!S.IsNPCConversationActive({"other"}));
    S.PendingNPCBattles.clear();NPC.FollowUp=true;NPC.RefreshQuestNPC();assert(!Actor.Hidden&&!Actor.Destroyed);
    NPC.FollowUp=false;NPC.bTalking=true;NPC.RefreshQuestNPC();assert(!Actor.Hidden&&!Actor.Destroyed);
    NPC.bTalking=false;NPC.RefreshQuestNPC();assert(Actor.Hidden&&!Actor.Destroyed);W.Timers.Tick();assert(Actor.Destroyed);
    // Direct talking/follow-up and locally queued next encounters each cancel queued destruction.
    for(int Mode=0;Mode<3;++Mode){
        Actor.Destroyed=false;Actor.Hidden=false;Actor.Collision=true;NPC.bRemovalScheduled=false;
        assert(NPC.ApplyPersistentRemoval());
        if(Mode==0)NPC.bTalking=true;else if(Mode==1)NPC.FollowUp=true;else NPC.PendingEncounterID={"next"};
        W.Timers.Tick();assert(!Actor.Destroyed&&!Actor.Hidden&&Actor.Collision);
        NPC.bTalking=false;NPC.FollowUp=false;NPC.PendingEncounterID={};NPC.RefreshQuestNPC();W.Timers.Tick();assert(Actor.Destroyed);
    }
    // A restore before callback cancellation returns the original appearance/collision.
    Actor.Destroyed=false;Actor.Hidden=true;Actor.Collision=false;NPC.bRemovalScheduled=false;
    assert(NPC.ApplyPersistentRemoval());S.Removed.clear();NPC.HandlePersistentStateRestored();W.Timers.Tick();
    assert(!Actor.Destroyed&&Actor.Hidden&&!Actor.Collision&&!NPC.bRemovalScheduled);
    // Stale-world battle records cannot hold this world's actor indefinitely.
    S.PendingNPCBattles.push_back({{{"speaker"},{&OldWorld}}});assert(!S.IsNPCConversationActive({"speaker"}));S.PendingNPCBattles.clear();
    // Duplicate IDs still refuse removal. EndPlay prevents an outstanding callback from destroying.
    S.Removed.insert("speaker");NPC.Unique=false;assert(!NPC.ApplyPersistentRemoval());NPC.Unique=true;
    NPC.ApplyPersistentRemoval();NPC.bEndingPlay=true;W.Timers.Tick();assert(!Actor.Destroyed);
}
