#include <cassert>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <algorithm>
struct FName {
    std::string Value;
    bool IsNone() const { return Value.empty(); }
    bool operator==(const FName& R)const{return Value==R.Value;}
};
namespace EEndPlayReason { enum Type { RemovedFromWorld }; }
struct FTimerHandle { int ID=0; };
struct TimerManager {
    int Next=0;std::map<int,std::function<void()>> Pending;
    template<class T> FTimerHandle SetTimerForNextTick(T* O,void(T::*F)()) {
        const int ID=++Next;Pending[ID]=[O,F]{(O->*F)();};return {ID};
    }
    void ClearTimer(FTimerHandle H){Pending.erase(H.ID);}
    template<class T>void SetTimer(FTimerHandle& H,T* O,void(T::*F)(),float,bool){ClearTimer(H);H=SetTimerForNextTick(O,F);}
    void Tick(){auto Work=Pending;Pending.clear();for(auto& P:Work)P.second();}
};
struct World { TimerManager Timers;TimerManager& GetTimerManager(){return Timers;} };
struct AActor {
    bool Hidden=false,Collision=true,Destroyed=false;
    bool IsHidden()const{return Hidden;}bool GetActorEnableCollision()const{return Collision;}
    void SetActorHiddenInGame(bool V){Hidden=V;}void SetActorEnableCollision(bool V){Collision=V;}
    void Destroy(){Destroyed=true;}
};
bool IsValid(AActor* A){return A && !A->Destroyed;}
struct UActorComponent {
    struct { bool bCanEverTick=true; }PrimaryComponentTick;
    AActor* Owner=nullptr;World* W=nullptr;
    AActor* GetOwner(){return Owner;}World* GetWorld(){return W;}
    void BeginPlay(){}void EndPlay(EEndPlayReason::Type){}
};
template<class... Args>struct Event {
    struct Listener{void* Owner;std::function<void(Args...)> Call;};std::vector<Listener> List;
    template<class T>void AddUniqueDynamic(T* O,void(T::*F)(Args...)){
        for(auto& P:List)if(P.Owner==O)return;
        List.push_back({O,[O,F](Args... A){(O->*F)(A...);}});
    }
    template<class T>void RemoveDynamic(T* O,void(T::*)(Args...)){
        List.erase(std::remove_if(List.begin(),List.end(),[O](const Listener& P){return P.Owner==O;}),List.end());
    }
    void Broadcast(Args... A){const auto Copy=List;for(const auto& P:Copy)P.Call(A...);}
};
struct UReditusQuestSubsystem {
    static UReditusQuestSubsystem* Current;
    std::set<std::string> Removed;
    std::set<std::string> Talking;
    Event<FName> OnNPCRemoved;Event<> OnQuestStateRestored;
    static UReditusQuestSubsystem* GetQuestSystem(void*){return Current;}
    bool WasNPCRemoved(FName ID)const{return Removed.count(ID.Value)!=0;}
    bool IsNPCConversationActive(FName ID)const{return Talking.count(ID.Value)!=0;}
    void Remove(FName ID){Removed.insert(ID.Value);OnNPCRemoved.Broadcast(ID);}
};
UReditusQuestSubsystem* UReditusQuestSubsystem::Current=nullptr;
struct UReditusNPCLinkedActorComponent:UActorComponent {
    using Super=UActorComponent;
    FName LinkedNPCID;FTimerHandle RemovalTimer;
    bool bEndingPlay=false,bRemovalScheduled=false,bPreviouslyHidden=false,bPreviouslyColliding=true;
    UReditusNPCLinkedActorComponent();
    void BeginPlay();void EndPlay(EEndPlayReason::Type);
    void RefreshRemoval();void HandleNPCRemoved(FName);void FinishRemoval();
};
// ACTUAL_LINKED_ACTOR
int main(){
    UReditusQuestSubsystem S;UReditusQuestSubsystem::Current=&S;World W;
    AActor Enemy,Prop,Other;UReditusNPCLinkedActorComponent E,P,O;
    E.Owner=&Enemy;E.W=&W;E.LinkedNPCID={"testnpc_01"};E.BeginPlay();
    P.Owner=&Prop;P.W=&W;P.LinkedNPCID=E.LinkedNPCID;P.BeginPlay();
    O.Owner=&Other;O.W=&W;O.LinkedNPCID={"othernpc"};O.BeginPlay();
    assert(!Enemy.Hidden && !Enemy.Destroyed && W.Timers.Pending.empty());
    S.Remove({"different"});assert(!Enemy.Hidden);
    S.Remove(E.LinkedNPCID);
    assert(Enemy.Hidden && !Enemy.Collision && !Enemy.Destroyed);
    assert(Prop.Hidden && !Prop.Collision && !Other.Hidden);
    assert(W.Timers.Pending.size()==2);
    S.OnNPCRemoved.Broadcast(E.LinkedNPCID);assert(W.Timers.Pending.size()==2);
    W.Timers.Tick();assert(Enemy.Destroyed && Prop.Destroyed && !Other.Destroyed);
    // A newly loaded map checks the same restored record immediately.
    AActor Reloaded;UReditusNPCLinkedActorComponent R;R.Owner=&Reloaded;R.W=&W;R.LinkedNPCID=E.LinkedNPCID;R.BeginPlay();
    assert(Reloaded.Hidden && !Reloaded.Destroyed);W.Timers.Tick();assert(Reloaded.Destroyed);
    // Importing an older state before the deferred destruction restores appearance/collision.
    AActor Old;Old.Hidden=true;Old.Collision=false;UReditusNPCLinkedActorComponent C;
    C.Owner=&Old;C.W=&W;C.LinkedNPCID=E.LinkedNPCID;C.BeginPlay();
    S.Removed.clear();S.OnQuestStateRestored.Broadcast();W.Timers.Tick();
    assert(!Old.Destroyed && Old.Hidden && !Old.Collision);
    AActor Travel;UReditusNPCLinkedActorComponent T;T.Owner=&Travel;T.W=&W;T.LinkedNPCID={"travel"};T.BeginPlay();
    S.Remove(T.LinkedNPCID);T.EndPlay(EEndPlayReason::RemovedFromWorld);W.Timers.Tick();assert(!Travel.Destroyed);
    AActor NoID;UReditusNPCLinkedActorComponent N;N.Owner=&NoID;N.W=&W;N.BeginPlay();S.OnQuestStateRestored.Broadcast();
    assert(!NoID.Hidden && !NoID.Destroyed && !N.PrimaryComponentTick.bCanEverTick);
    // A removal event in a dialogue node keeps both linked visuals until the conversation closes.
    AActor SpeakerVisual;UReditusNPCLinkedActorComponent D;D.Owner=&SpeakerVisual;D.W=&W;D.LinkedNPCID={"speaker"};D.BeginPlay();
    S.Talking.insert("speaker");S.Remove(D.LinkedNPCID);W.Timers.Tick();
    assert(!SpeakerVisual.Hidden && !SpeakerVisual.Destroyed);
    S.Talking.clear();W.Timers.Tick();assert(SpeakerVisual.Hidden && !SpeakerVisual.Destroyed);
    W.Timers.Tick();assert(SpeakerVisual.Destroyed);
    // Removal was queued FIRST; the battle/follow-up lifetime hold begins before its callback runs.
    AActor Late;UReditusNPCLinkedActorComponent L;L.Owner=&Late;L.W=&W;L.LinkedNPCID={"late"};L.BeginPlay();
    S.Remove(L.LinkedNPCID);assert(Late.Hidden&&!Late.Destroyed);
    S.Talking.insert("late");W.Timers.Tick();assert(!Late.Hidden&&Late.Collision&&!Late.Destroyed);
    W.Timers.Tick();assert(!Late.Hidden&&!Late.Destroyed);
    S.Talking.erase("late");W.Timers.Tick();assert(Late.Hidden&&!Late.Destroyed);
    W.Timers.Tick();assert(Late.Destroyed);
}
