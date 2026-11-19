// Actual fetch builder/name accessor and presentation adapters, compiled with
// typed host stand-ins. This checks data/guards, not Slate rendering or UHT.
#include "ReditusFetchQuestFlow.h"
#include <cassert>
#include <memory>
#include <string>
#include <vector>
#define TEXT(V) V
#define UE_LOG(...) do {} while(false)
using int32=int;using FString=std::string;
const int NAME_None=0,RF_Transient=1;
struct Object{virtual ~Object()=default;};
std::vector<std::unique_ptr<Object>> Allocations;
template<class T,class... A>T* NewObject(A&&...){auto* P=new T;Allocations.emplace_back(P);return P;}
template<class T>struct TArray:std::vector<T>{void Add(const T& V){this->push_back(V);}int Num()const{return int(this->size());}};
struct FText
{
    std::string Value,Identity;
    bool IsEmpty()const{return Value.empty();}bool EqualTo(const FText& R)const{return Value==R.Value;}
    std::string ToString()const{return Value;}static FText GetEmpty(){return {};}
};
struct FVector2D{FVector2D()=default;FVector2D(float,float){}};
enum class EDialogueNodeType{DNT_None,Portrait};
struct UReditusFetchQuestCondition:Object{int ExpectedPhase=0;};
struct UReditusFetchQuestActionEvent:Object{int FlowAction=0;};
struct FDialogueNode
{
    int id=-1;bool isPlayer=false,bDrawBubbleComment=false;EDialogueNodeType NodeType=EDialogueNodeType::DNT_None;
    FText Text;FString SpeakerName;FVector2D Coordinates;TArray<int> Links;
    TArray<UReditusFetchQuestCondition*> Conditions;TArray<UReditusFetchQuestActionEvent*> Events;
};
struct UDialogue:Object{FText Name;TArray<FDialogueNode> Data;int NextNodeId=0;};
struct APlayerController{};
struct FDataTableRowHandle{bool Valid=true;};
struct FReditusQuestSnapshot{};
struct FReditusFetchQuestText
{
    FText SpeakerName,Offer{"Offer sentence only","OfferKey"},Reminder{"Reminder","ReminderKey"},
        HandIn{"Hand-in sentence only","HandInKey"},Completed{"Completed","CompletedKey"};
};
struct FReditusQuestNPCDefinition
{
    FDataTableRowHandle QuestRow;FText DisplayName{"NPC display name","DisplayKey"};
    FReditusFetchQuestText FetchQuestText;EDialogueNodeType FetchSpeakerType=EDialogueNodeType::DNT_None;
};
struct UReditusQuestSubsystem
{
    static UReditusQuestSubsystem* Current;static UReditusQuestSubsystem* GetQuestSystem(void*){return Current;}
    bool ReadQuest(FDataTableRowHandle Q,FReditusQuestSnapshot&){return Q.Valid;}
    // No reward-summary API: the dialogue builder must not append quest info.
};
UReditusQuestSubsystem* UReditusQuestSubsystem::Current=nullptr;
struct FReditusQuestDefinition{bool AllowDecline=true;};
FReditusQuestDefinition QuestDefinition;
const FReditusQuestDefinition* FetchDefinition(FDataTableRowHandle Q){return Q.Valid?&QuestDefinition:nullptr;}
bool AllowHandInDecline(const FReditusQuestDefinition& Row){return Row.AllowDecline;}
FText FetchLine(const FReditusFetchQuestText& Text,ReditusFetchQuestFlow::Line Line)
{
    using L=ReditusFetchQuestFlow::Line;
    if(Line==L::Offer)return Text.Offer;
    if(Line==L::HandIn)return Text.HandIn;
    if(Line==L::Reminder)return Text.Reminder;
    if(Line==L::Completed)return Text.Completed;
    return {};
}
struct UDialogueUserWidget;
struct UReditusQuestNPCComponent
{
    UDialogue* ActiveFetchDialogue=nullptr;UDialogueUserWidget* ActiveDialogueWidget=nullptr;
    bool bEndingPlay=false,bTalking=false;
    ReditusFetchQuestFlow::Phase Phase=ReditusFetchQuestFlow::Phase::Offer;
    ReditusFetchQuestFlow::Phase GetFetchQuestPhase(APlayerController*)const{return Phase;}
    bool bFetchActionFailed=false;
    UDialogue* BuildFetchQuestDialogue(APlayerController*,const FReditusQuestNPCDefinition&);
    bool ReadFetchSpeakerName(const UDialogueUserWidget*,FText&)const;
};
struct AActor{UReditusQuestNPCComponent* NPC=nullptr;template<class T>T* FindComponentByClass()const{return NPC;}};
bool IsValid(const AActor* V){return V!=nullptr;}
struct UDialogueUserWidget{AActor* NPCActor=nullptr;};
template<class T>struct Weak{T* Value=nullptr;bool IsValid()const{return Value!=nullptr;}T* Get()const{return Value;}T* operator->()const{return Value;}};
enum class ESlateVisibility{Collapsed,HitTestInvisible};
struct UTextBlock
{
    FText Text;int Writes=0;ESlateVisibility Visibility=ESlateVisibility::Collapsed;
    const FText& GetText()const{return Text;}void SetText(FText V){Text=V;++Writes;}void SetVisibility(ESlateVisibility V){Visibility=V;}
};
struct UUserWidget{};
bool IsValid(UUserWidget* V){return V!=nullptr;}
struct ReplyList{UUserWidget* Value=nullptr;UUserWidget* GetChildAt(int I){return I==0?Value:nullptr;}};
struct UReditusDialoguePresentationWidget
{
    Weak<UDialogueUserWidget> Source;UTextBlock* Speaker=nullptr;
    ReplyList* Replies=nullptr;bool Allowed=false;int Selected=0,Called=0,FocusRestored=0;
    struct{bool Log=false;int ManualCalls=0;void ManualInput(){++ManualCalls;}}Playback;
    void RefreshSpeakerName();void ChooseReply(int,UUserWidget*);
    bool InputAllowed()const{return Allowed;}void SelectReply(int,UUserWidget*){++Selected;}
    void CallReply(int){++Called;}void RestoreSourceFocus(){++FocusRestored;}
};
// ACTUAL_FETCH_BUILDER_AND_NAME
// ACTUAL_REFRESH_SPEAKER
// ACTUAL_CHOOSE_REPLY
int main()
{
    UReditusQuestSubsystem System;UReditusQuestSubsystem::Current=&System;
    UReditusQuestNPCComponent NPC;FReditusQuestNPCDefinition Definition;APlayerController Player;
    auto* Dialogue=NPC.BuildFetchQuestDialogue(&Player,Definition);assert(Dialogue);
    assert((Dialogue->Data[1].Links==std::vector<int>{2,3}));
    assert((Dialogue->Data[5].Links==std::vector<int>{6,3}));
    QuestDefinition.AllowDecline=false;NPC.Phase=ReditusFetchQuestFlow::Phase::Ready;
    Dialogue=NPC.BuildFetchQuestDialogue(&Player,Definition);assert(Dialogue);
    assert((Dialogue->Data[0].Links==std::vector<int>{5}));
    assert((Dialogue->Data[1].Links==std::vector<int>{2,3})); // Initial offer still permits decline.
    assert((Dialogue->Data[5].Links==std::vector<int>{6})); // One reply at hunt turn-in.
    assert(Dialogue->Data[6].Events.size()==1&&Dialogue->Data[6].Events[0]->FlowAction==int(ReditusFetchQuestFlow::Action::HandIn));
    QuestDefinition.AllowDecline=true;NPC.Phase=ReditusFetchQuestFlow::Phase::Offer;
    Dialogue=NPC.BuildFetchQuestDialogue(&Player,Definition);
    assert(Dialogue->Name.Identity=="DisplayKey"&&Dialogue->Data[1].SpeakerName=="NPC display name");
    assert(Dialogue->Data[1].Text.Value=="Offer sentence only"&&Dialogue->Data[1].Text.Identity=="OfferKey");
    assert(Dialogue->Data[5].Text.Value=="Hand-in sentence only"&&Dialogue->Data[5].Text.Identity=="HandInKey");
    Definition.FetchQuestText.SpeakerName={"Courier","CourierKey"};
    Dialogue=NPC.BuildFetchQuestDialogue(&Player,Definition);
    assert(Dialogue->Name.Value=="Courier"&&Dialogue->Name.Identity=="CourierKey");
    assert(Dialogue->Data[1].NodeType==EDialogueNodeType::DNT_None&&Dialogue->Data[1].SpeakerName=="Courier");
    assert(Dialogue->Data[2].SpeakerName.empty());
    Definition.FetchSpeakerType=EDialogueNodeType::Portrait;
    Dialogue=NPC.BuildFetchQuestDialogue(&Player,Definition);
    assert(Dialogue->Data[1].NodeType==EDialogueNodeType::Portrait&&Dialogue->Name.Value=="Courier");
    AActor Owner;Owner.NPC=&NPC;UDialogueUserWidget Widget;Widget.NPCActor=&Owner;
    NPC.ActiveDialogueWidget=&Widget;UTextBlock Label;UReditusDialoguePresentationWidget Presenter;Presenter.Source.Value=&Widget;Presenter.Speaker=&Label;
    Presenter.RefreshSpeakerName();assert(Label.Writes==0); // Conversation not active yet.
    NPC.bTalking=true;Presenter.RefreshSpeakerName();
    assert(Label.Text.Value=="Courier"&&Label.Text.Identity=="CourierKey"&&Label.Visibility==ESlateVisibility::HitTestInvisible);
    const int Writes=Label.Writes;Presenter.RefreshSpeakerName();assert(Label.Writes==Writes);
    // A portrait/legacy update hiding the label cannot hide an authored name.
    Label.Visibility=ESlateVisibility::Collapsed;Presenter.RefreshSpeakerName();assert(Label.Visibility==ESlateVisibility::HitTestInvisible);
    Dialogue->Name={"Courrier","CourierKey"};Presenter.RefreshSpeakerName();assert(Label.Text.Value=="Courrier"&&Label.Text.Identity=="CourierKey");
    UDialogueUserWidget Other;FText Name;assert(!NPC.ReadFetchSpeakerName(&Other,Name));
    NPC.bEndingPlay=true;Dialogue->Name={"Must not apply","Stop"};Presenter.RefreshSpeakerName();assert(Label.Text.Value=="Courrier");NPC.bEndingPlay=false;
    NPC.ActiveFetchDialogue=nullptr;Label.Text={"Legacy speaker","LegacyKey"};Presenter.RefreshSpeakerName();assert(Label.Text.Value=="Legacy speaker");
    NPC.ActiveFetchDialogue=Dialogue;Dialogue->Name={};Presenter.RefreshSpeakerName();assert(Label.Text.IsEmpty()&&Label.Visibility==ESlateVisibility::Collapsed);
    // Readable choice buttons still cannot bypass the real action guard.
    ReplyList List;UUserWidget Reply,Stale;List.Value=&Reply;Presenter.Replies=&List;
    Presenter.ChooseReply(0,&Reply);assert(Presenter.Called==0);
    Presenter.Allowed=true;Presenter.Playback.Log=true;Presenter.ChooseReply(0,&Reply);assert(Presenter.Called==0);
    Presenter.Playback.Log=false;Presenter.ChooseReply(0,&Stale);assert(Presenter.Called==0);
    Presenter.ChooseReply(1,&Reply);assert(Presenter.Called==0);
    Presenter.ChooseReply(0,&Reply);assert(Presenter.Called==1&&Presenter.Selected==1&&Presenter.FocusRestored==1&&Presenter.Playback.ManualCalls==1);
}
