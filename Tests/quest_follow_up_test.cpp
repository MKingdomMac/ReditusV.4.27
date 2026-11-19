#include <cassert>
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "ReditusNPCFollowUpPolicy.h"
using int32 = int;
#define TEXT(X) X
#define UE_LOG(...) do {} while(false)
struct FString { std::string V; const char* operator*()const{return V.c_str();} };
struct FName {
    std::string V; bool IsNone()const{return V.empty();}
    bool operator==(const FName& R)const{return V==R.V;}
    bool operator!=(const FName& R)const{return !(*this==R);}
    FString ToString()const{return {V};}
};
const FName NAME_None{};
struct FGuid { int V=0; bool IsValid()const{return V!=0;} bool operator==(FGuid R)const{return V==R.V;} };
template<class T>struct TWeakObjectPtr {
    T* P=nullptr; TWeakObjectPtr()=default;TWeakObjectPtr(T* V):P(V){}
    T* Get()const{return P;}bool IsValid()const{return P!=nullptr;}void Reset(){P=nullptr;}
    TWeakObjectPtr& operator=(T* V){P=V;return *this;}
};
template<class T>struct Soft {
    T* P=nullptr;bool IsNull()const{return !P;}
};
template<class T>struct TArray:std::vector<T> {
    using std::vector<T>::vector;using std::vector<T>::operator=;
    int Num()const{return int(this->size());}
    template<class F>void RemoveAll(F Fn){this->erase(std::remove_if(this->begin(),this->end(),Fn),this->end());}
};
template<class T>struct TGuardValue { T& Ref;T Old;TGuardValue(T& R,T V):Ref(R),Old(R){Ref=V;}~TGuardValue(){Ref=Old;} };
struct FDataTableRowHandle { int Row=0; };
enum class EReditusNPCBattleResult { Won, Lost, Escaped, Cancelled };
enum class EReditusNPCRemovalReason { Defeated };
struct UReditusQuestNPCComponent;
struct UWorld;
struct AActor {
    virtual ~AActor()=default;FName Tag;bool Starting=false,HasStarting=false;
    UWorld* World=nullptr;TArray<UReditusQuestNPCComponent*> Components;
    bool ActorHasTag(FName T)const{return Tag==T;}AActor* GetClass(){return this;}
    template<class T>void GetComponents(TArray<T*>& Out){for(auto* C:Components)Out.push_back(C);}
    UWorld* GetWorld(){return World;}
};
struct APlayerController:AActor{bool Explore=true;};
struct UDialogue{};struct ULevelSequence{};
struct UDialogueUserWidget { bool Removed=false;void RemoveFromParent(){Removed=true;} };
struct ULevelSequencePlayer { bool Playing=false,Paused=false;bool IsPlaying()const{return Playing;}bool IsPaused()const{return Paused;} };
struct ALevelSequenceActor:AActor{ULevelSequencePlayer Player;ULevelSequencePlayer* GetSequencePlayer(){return &Player;}};
struct FTimerHandle { int ID=0; };
struct TimerManager {
    int Next=0;std::map<int,std::function<void()>> Pending;
    template<class T>FTimerHandle SetTimerForNextTick(T* O,void(T::*F)()){const int I=++Next;Pending[I]=[O,F]{(O->*F)();};return {I};}
    void ClearTimer(FTimerHandle H){Pending.erase(H.ID);}
    void Tick(){const auto Copy=Pending;Pending.clear();for(const auto& P:Copy)P.second();}
};
struct UWorld{TArray<AActor*> Actors;TimerManager Timers;TimerManager& GetTimerManager(){return Timers;}};
template<class T>bool IsValid(T* P){return P!=nullptr;}
template<class T>struct TActorIterator {
    std::vector<T*> Actors;size_t I=0;
    explicit TActorIterator(UWorld* W){if(W)for(auto* A:W->Actors)if(auto* Tgt=dynamic_cast<T*>(A))Actors.push_back(Tgt);}
    explicit operator bool()const{return I<Actors.size();}void operator++(){++I;}T* operator*()const{return Actors[I];}T* operator->()const{return Actors[I];}
};
struct FBoolProperty {
    bool GetPropertyValue_InContainer(AActor* A){return A->Starting;}
    void SetPropertyValue_InContainer(AActor* A,bool V){A->Starting=V;}
};
template<class T>T* FindFProperty(AActor* A,const char*){static T Field;return A&&A->HasStarting?&Field:nullptr;}
namespace ReditusQuestSequenceGate{bool IsExploring(APlayerController* P){return P&&P->Explore;}}
struct FReditusQuestSequence { Soft<ULevelSequence> Sequence;FName PlacedActorTag;FGuid NPCFollowUpRequestID; };
struct FReditusNPCBattleRequest {
    FGuid RequestID;FName NPCID,EncounterID;APlayerController* Player=nullptr;TWeakObjectPtr<UWorld> RequestWorld;
    bool RemoveNPCOnVictory=false;
};
struct FReditusNPCBattleFollowUp {
    Soft<ULevelSequence> Sequence;FName PlacedSequenceActorTag;UDialogue* Dialogue=nullptr;FName NextEncounterID;
    bool HasWork()const{return !Sequence.IsNull()||!PlacedSequenceActorTag.IsNone()||Dialogue||!NextEncounterID.IsNone();}
};
struct FReditusNPCBattleFollowUpRule { FName EncounterID;EReditusNPCBattleResult Result=EReditusNPCBattleResult::Won;FReditusNPCBattleFollowUp FollowUp; };
struct FReditusQuestNPCDefinition { FReditusNPCBattleFollowUp PostBattleFollowUp;TArray<FReditusNPCBattleFollowUpRule> EncounterFollowUps;FDataTableRowHandle QuestRow; };
struct UReditusQuestSubsystem {
    static UReditusQuestSubsystem* Current;UWorld* World=nullptr;
    TArray<FReditusQuestSequence> SequenceQueue;FReditusQuestSequence ActiveSequence;
    struct {int Calls=0,Success=0;void Broadcast(FName,FName,bool V){++Calls;if(V)++Success;}}OnNPCFollowUpFinished;
    int EndCalls=0,Removals=0;bool RejectSequence=false;std::set<std::string> Removed;
    static UReditusQuestSubsystem* GetQuestSystem(void*){return Current;}
    UWorld* GetTickableGameObjectWorld()const{return World;}
    UReditusQuestNPCComponent* FindNPCComponent(FName)const;
    bool HasNPCDialogueOpen()const;bool HasNPCFollowUps()const;bool HasNPCStoryWork()const;
    void CancelNPCFollowUpSequences(FGuid);void CancelNPCStoryWork();
    bool HasPendingNPCBattle(FName)const{return false;}
    bool CanPresentNPCFollowUp()const{return true;}
    bool HasQueuedQuestSequences()const{return ActiveSequence.NPCFollowUpRequestID.IsValid()||!SequenceQueue.empty();}
    void RefreshQuestInventory(APlayerController*){}
    bool PlayQuestLevelSequence(FReditusQuestSequence S){if(RejectSequence)return false;SequenceQueue.push_back(S);return true;}
    void EndSequence(bool){++EndCalls;ActiveSequence={};}
    bool WasNPCRemoved(FName ID)const{return Removed.count(ID.V)!=0;}
    void RemoveNPCPermanently(FName ID,EReditusNPCRemovalReason){++Removals;Removed.insert(ID.V);}
};
UReditusQuestSubsystem* UReditusQuestSubsystem::Current=nullptr;
struct UReditusQuestNPCComponent {
    UWorld* World=nullptr;FReditusQuestNPCDefinition Definition;FName PersistentNPCID;
    bool bEndingPlay=false,bFinishingFollowUp=false,bTalking=false,bPostBattleDialogue=false,RejectDialogue=false;
    ReditusNPCFollowUpPolicy::State FollowUpState;FReditusNPCBattleFollowUp ActiveFollowUp;FReditusNPCBattleRequest FollowUpRequest;
    EReditusNPCBattleResult FollowUpResult=EReditusNPCBattleResult::Won;
    FDataTableRowHandle FollowUpQuest,LastBattleQuest,PendingBattleQuest;FGuid LastBattleRequestID;
    FName PendingEncounterID;TWeakObjectPtr<APlayerController> PendingBattlePlayer;
    TWeakObjectPtr<AActor> HeldEncounterSource;bool bSourcePreviouslyStarting=false,bRemoveAfterPendingBattle=false;
    FTimerHandle BattleSubmitTimer;UDialogueUserWidget Widget;UDialogueUserWidget* ActiveDialogueWidget=nullptr;
    int Opens=0,Submits=0;FName Submitted;
    UWorld* GetWorld(){return World;}
    const FReditusQuestNPCDefinition* ResolveDefinition(){return &Definition;}
    bool HasPendingBattleFollowUp()const{return FollowUpState.Busy();}bool IsTalking()const{return bTalking;}
    bool BeginBattleFollowUp(const FReditusNPCBattleRequest&,EReditusNPCBattleResult);
    bool CompletePostBattleDialogue();void HandleFollowUpSequenceFinished(FReditusQuestSequence,bool);
    void PumpBattleFollowUp();void FinishBattleFollowUp(bool,bool Notify=true);
    void HoldEncounterSource(FName);void ReleaseEncounterSource();
    bool OpenDialogue(APlayerController*,UDialogue*,FDataTableRowHandle Q,bool Post){if(RejectDialogue)return false;++Opens;bTalking=true;bPostBattleDialogue=Post;ActiveDialogueWidget=&Widget;assert(Q.Row==7);return true;}
    void FinishConversation(bool Notify){if(bPostBattleDialogue)FollowUpState.DialogueClosed(Notify);bTalking=false;bPostBattleDialogue=false;ActiveDialogueWidget=nullptr;}
    void Close(bool Normal){FollowUpState.DialogueClosed(Normal);bTalking=false;bPostBattleDialogue=false;ActiveDialogueWidget=nullptr;}
    void SubmitPendingNPCBattle(){++Submits;Submitted=PendingEncounterID;PendingEncounterID=NAME_None;}
    void ApplyPersistentRemoval(){}
};
// ACTUAL_FOLLOW_UP
int main(){
    UWorld W,OtherWorld;UReditusQuestSubsystem S;S.World=&W;UReditusQuestSubsystem::Current=&S;
    APlayerController PC;PC.World=&W;AActor Owner,Source;Owner.World=&W;Source.World=&W;Source.Tag={"A"};Source.HasStarting=true;
    UReditusQuestNPCComponent C;C.World=&W;C.PersistentNPCID={"npc1"};C.Definition.QuestRow.Row=7;Owner.Components.push_back(&C);
    W.Actors={&PC,&Owner,&Source};
    FReditusNPCBattleRequest R;R.RequestID={1};R.NPCID=C.PersistentNPCID;R.EncounterID={"A"};R.Player=&PC;R.RequestWorld=&W;R.RemoveNPCOnVictory=true;
    UDialogue D;ULevelSequence L;
    assert(!C.BeginBattleFollowUp(R,EReditusNPCBattleResult::Won));
    C.Definition.PostBattleFollowUp.Sequence.P=&L;C.Definition.PostBattleFollowUp.Dialogue=&D;
    assert(C.BeginBattleFollowUp(R,EReditusNPCBattleResult::Won)&&Source.Starting);
    assert(!C.BeginBattleFollowUp(R,EReditusNPCBattleResult::Won));assert(S.HasNPCStoryWork());
    PC.Explore=false;C.PumpBattleFollowUp();assert(S.SequenceQueue.empty()&&C.Opens==0);
    PC.Explore=true;C.PumpBattleFollowUp();assert(S.SequenceQueue.size()==1&&C.Opens==0);
    auto Scene=S.SequenceQueue[0];C.PumpBattleFollowUp();assert(S.SequenceQueue.size()==1);
    auto Wrong=Scene;Wrong.NPCFollowUpRequestID={99};C.HandleFollowUpSequenceFinished(Wrong,true);C.PumpBattleFollowUp();assert(C.Opens==0);
    S.SequenceQueue.clear();C.HandleFollowUpSequenceFinished(Scene,true);C.PumpBattleFollowUp();assert(C.Opens==1&&C.bTalking);
    C.PumpBattleFollowUp();assert(C.Opens==1&&S.Removals==0);assert(C.CompletePostBattleDialogue());
    C.Close(true);S.SequenceQueue.push_back(Wrong);C.PumpBattleFollowUp();assert(S.Removals==0&&Source.Starting);
    S.SequenceQueue.clear();C.PumpBattleFollowUp();assert(S.Removals==1&&!Source.Starting&&!S.HasNPCStoryWork());
    C.HandleFollowUpSequenceFinished(Scene,true);C.PumpBattleFollowUp();assert(S.Removals==1);
    S.Removed.clear();S.Removals=0;C.Definition.PostBattleFollowUp.Sequence.P=nullptr;
    // Closing without the explicit authored terminal marker is cancellation.
    assert(C.BeginBattleFollowUp(R,EReditusNPCBattleResult::Won));C.PumpBattleFollowUp();C.Close(true);C.PumpBattleFollowUp();assert(S.Removals==0&&!C.HasPendingBattleFollowUp());
    assert(C.BeginBattleFollowUp(R,EReditusNPCBattleResult::Won));C.PumpBattleFollowUp();C.CompletePostBattleDialogue();C.Close(false);C.PumpBattleFollowUp();assert(S.Removals==0);
    // A lost/escaped/cancelled battle never inherits the default victory follow-up.
    for(auto Result:{EReditusNPCBattleResult::Lost,EReditusNPCBattleResult::Escaped,EReditusNPCBattleResult::Cancelled})assert(!C.BeginBattleFollowUp(R,Result));
    // Exact result-specific rules can override the default, including an empty stop rule.
    FReditusNPCBattleFollowUpRule Rule;Rule.EncounterID={"A"};Rule.Result=EReditusNPCBattleResult::Lost;Rule.FollowUp.Dialogue=&D;
    C.Definition.EncounterFollowUps.push_back(Rule);assert(C.BeginBattleFollowUp(R,EReditusNPCBattleResult::Lost));C.PumpBattleFollowUp();C.CompletePostBattleDialogue();C.Close(true);C.PumpBattleFollowUp();assert(S.Removals==0);
    C.Definition.EncounterFollowUps.push_back(Rule);assert(C.BeginBattleFollowUp(R,EReditusNPCBattleResult::Lost));C.PumpBattleFollowUp();assert(!C.HasPendingBattleFollowUp()&&S.Removals==0);
    C.Definition.EncounterFollowUps.clear();
    // Thousands of cycles reuse the same live driver without recursion or a fixed count.
    C.Definition.PostBattleFollowUp.Dialogue=nullptr;C.Definition.PostBattleFollowUp.NextEncounterID={"A"};
    for(int I=0;I<10000;++I){R.RequestID={100+I};assert(C.BeginBattleFollowUp(R,EReditusNPCBattleResult::Won));C.PumpBattleFollowUp();assert(S.Removals==0&&!C.HasPendingBattleFollowUp()&&S.HasNPCStoryWork());W.Timers.Tick();assert(C.Submitted.V=="A");}
    assert(C.Submits==10000);
    // An authored dialogue branch overrides automatic continuation.
    C.Definition.PostBattleFollowUp.Dialogue=&D;assert(C.BeginBattleFollowUp(R,EReditusNPCBattleResult::Won));C.PumpBattleFollowUp();
    C.PendingEncounterID={"B"};C.PendingBattlePlayer=&PC;C.PendingBattleQuest.Row=9;C.CompletePostBattleDialogue();C.Close(true);C.PumpBattleFollowUp();
    assert(C.PendingEncounterID.V=="B"&&C.PendingBattleQuest.Row==9);W.Timers.Tick();assert(C.Submitted.V=="B"&&S.Removals==0);
    // Explicit permanent removal wins over a configured continuation.
    assert(C.BeginBattleFollowUp(R,EReditusNPCBattleResult::Won));C.PumpBattleFollowUp();S.Removed.insert("npc1");C.CompletePostBattleDialogue();C.Close(true);C.PumpBattleFollowUp();assert(C.PendingEncounterID.IsNone());S.Removed.clear();S.Removals=0;
    // Stopped sequences, bad assets, disappearing worlds and reset do not auto-remove or start the next battle.
    C.Definition.PostBattleFollowUp.Sequence.P=&L;assert(C.BeginBattleFollowUp(R,EReditusNPCBattleResult::Won));C.PumpBattleFollowUp();Scene=S.SequenceQueue[0];S.SequenceQueue.clear();C.HandleFollowUpSequenceFinished(Scene,false);C.PumpBattleFollowUp();assert(S.Removals==0);
    S.RejectSequence=true;assert(C.BeginBattleFollowUp(R,EReditusNPCBattleResult::Won));C.PumpBattleFollowUp();C.PumpBattleFollowUp();assert(!C.HasPendingBattleFollowUp()&&S.Removals==0);S.RejectSequence=false;
    assert(C.BeginBattleFollowUp(R,EReditusNPCBattleResult::Won));PC.World=&OtherWorld;C.PumpBattleFollowUp();assert(!C.HasPendingBattleFollowUp()&&!Source.Starting);PC.World=&W;
    C.Definition.PostBattleFollowUp.Sequence.P=nullptr;C.RejectDialogue=true;assert(C.BeginBattleFollowUp(R,EReditusNPCBattleResult::Won));C.PumpBattleFollowUp();C.PumpBattleFollowUp();assert(S.Removals==0);C.RejectDialogue=false;
    assert(C.BeginBattleFollowUp(R,EReditusNPCBattleResult::Won));C.PumpBattleFollowUp();const int Before=S.OnNPCFollowUpFinished.Calls;S.CancelNPCStoryWork();assert(!C.bTalking&&C.Widget.Removed&&!S.HasNPCStoryWork()&&S.OnNPCFollowUpFinished.Calls==Before);
    // Cancelling an owned scene never stops an unrelated active sequence.
    FReditusQuestSequence Owned,Unrelated;Owned.NPCFollowUpRequestID={77};Unrelated.NPCFollowUpRequestID={88};S.SequenceQueue={Owned,Unrelated};S.ActiveSequence=Unrelated;
    S.CancelNPCFollowUpSequences({77});assert(S.SequenceQueue.size()==1&&S.EndCalls==0);S.CancelNPCFollowUpSequences({88});assert(S.SequenceQueue.empty()&&S.EndCalls==1);
    // Duplicate NPC identities fail closed; restoring source flags respects their prior value.
    AActor Duplicate;Duplicate.Components.push_back(&C);W.Actors.push_back(&Duplicate);assert(!S.FindNPCComponent(C.PersistentNPCID));W.Actors.pop_back();assert(S.FindNPCComponent(C.PersistentNPCID)==&C);
    Source.Starting=true;C.HoldEncounterSource({"A"});C.ReleaseEncounterSource();assert(Source.Starting);
}
