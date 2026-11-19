#include "ReditusQuestRuntime.h"
#include <cassert>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <cctype>

using int32=int;using uint8=unsigned char;
#define TEXT(X) X
#define TCHAR_TO_UTF8(X) X
#define UTF8_TO_TCHAR(X) X
#define USTRUCT(...)
#define UCLASS(...)
#define UENUM(...)
#define UPROPERTY(...)
#define UFUNCTION(...)
#define GENERATED_BODY() public: static const void* StaticStruct(){static int Tag;return &Tag;}
#define REDITUSQUESTNPC_API
#define WITH_EDITOR 0
#define WITH_EDITORONLY_DATA 0

struct FString
{
    std::string V;
    FString()=default;FString(const char* S):V(S){}FString(const std::string& S):V(S){}
    const char* operator*()const{return V.c_str();}
    bool IsEmpty()const{return V.empty();}void Reset(){V.clear();}
    bool Contains(const char* S)const{return V.find(S)!=std::string::npos;}
    FString ToLower()const{auto S=V;for(auto& C:S)C=char(std::tolower(static_cast<unsigned char>(C)));return S;}
    static FString FromInt(int I){return std::to_string(I);}
    friend FString operator+(const FString& A,const FString& B){return A.V+B.V;}
    bool operator<(const FString& B)const{return V<B.V;}
};
struct FName
{
    FString V;FName()=default;FName(const char* S):V(FString(S).ToLower()){}
    bool IsNone()const{return V.IsEmpty()||V.V=="none";}FString ToString()const{return V;}
    bool operator==(const FName& B)const{return V.V==B.V.V;}bool operator!=(const FName& B)const{return !(*this==B);}
    bool operator<(const FName& B)const{return V<B.V;}
};
static FName NAME_None;
struct FText{};struct FGuid{};struct FTableRowBase{};struct UObject{};struct UDataAsset:UObject{};
struct UBlueprintFunctionLibrary:UObject{};struct UDialogue:UObject{int Events=7;};
template<class T>using TSoftObjectPtr=T*;template<class T>using TWeakObjectPtr=T*;
template<class T>struct TArray:std::vector<T>{using std::vector<T>::vector;using std::vector<T>::operator=;int Num()const{return int(this->size());}void Reset(){this->clear();}};
template<class T>struct TSet:std::set<T>{bool Contains(const T& K)const{return this->count(K)!=0;}void Add(const T& K){this->insert(K);}void Remove(const T& K){this->erase(K);}};
struct UDataTable
{
    const void* Type=nullptr;FString Path;std::map<FName,const void*> Rows;
    const void* GetRowStruct()const{return Type;}FString GetPathName()const{return Path;}
    template<class T>const T* FindRow(FName Name,const char*,bool)const
    {auto It=Rows.find(Name);return Type==T::StaticStruct()&&It!=Rows.end()?static_cast<const T*>(It->second):nullptr;}
};
struct FDataTableRowHandle
{
    const UDataTable* DataTable=nullptr;FName RowName;
    bool operator==(const FDataTableRowHandle& B)const{return DataTable==B.DataTable&&RowName==B.RowName;}
};
enum class EReditusQuestState:uint8{Unknown,Unavailable,Available,Active,ReadyToDeliver,Completed,Failed};
// ACTUAL_SYSTEM_TYPES
// ACTUAL_ASSET_TYPES
// ACTUAL_CATALOG_TYPES

namespace ReditusQuestItemRouting
{
    bool ResolveKind(FDataTableRowHandle R,EReditusQuestItemKind& K)
    {K=EReditusQuestItemKind::Misc;return R.DataTable&&!R.RowName.IsNone();}
}
// ACTUAL_CATALOG
// ACTUAL_DEFINITION_COMPILER
struct UReditusQuestSubsystem{static FString Key(FDataTableRowHandle Quest);};
// ACTUAL_QUEST_KEY

int main()
{
    using namespace ReditusQuestCatalog;
    UDataTable Simple,Catalog;Simple.Type=FReditusQuestDefinition::StaticStruct();Simple.Path="/Game/DT_Quests";
    Catalog.Type=FReditusQuestCatalogRow::StaticStruct();Catalog.Path="/Game/DT_QuestCatalog";
    FReditusQuestDefinition Hunt;Hunt.Rewards.Gold=500;
    FReditusQuestStageDefinition Stage;Stage.StageID="Hunt";
    FReditusQuestTaskDefinition Kill;Kill.ObjectiveID="Kill";Kill.Kind=EReditusQuestObjectiveKind::DefeatEnemy;Kill.TargetID="DeathBoar";
    Stage.Objectives.push_back(Kill);Hunt.Stages.push_back(Stage);Simple.Rows["Test_DefeatBoss"]=&Hunt;
    FDataTableRowHandle Direct{&Simple,"Test_DefeatBoss"};assert(Definition(Direct)==&Hunt);
    FReditusQuestCatalogRow Alias;Alias.Kind=EReditusQuestCatalogKind::SimpleRow;Alias.SimpleQuest=Direct;
    Catalog.Rows["HuntAlias"]=&Alias;FDataTableRowHandle AliasHandle{&Catalog,"HuntAlias"};
    assert(Canonical(AliasHandle)==Direct&&Definition(AliasHandle)==&Hunt);
    assert(UReditusQuestSubsystem::Key(AliasHandle).V==UReditusQuestSubsystem::Key(Direct).V);
    // An alias cannot create a second started quest or change legacy saved keys.
    std::string Error;ReditusQuestRuntime::Definition D;
    const auto ID=UReditusQuestSubsystem::Key(AliasHandle);
    FString Why;assert(BuildDefinition(Hunt,ID,D,Why));
    ReditusQuestRuntime::Engine Engine;assert(Engine.AddDefinition(D,Error));assert(Engine.Start(D.Key));
    assert(!Engine.Start(UReditusQuestSubsystem::Key(Direct).V));
    assert(Engine.Signal(ReditusQuestRuntime::Kind::DefeatEnemy,"deathboar",1,"kill_1"));
    ReditusQuestRuntime::Engine Reloaded;assert(Reloaded.AddDefinition(D,Error));
    assert(Reloaded.Import({*Engine.FindRecord(D.Key)},Error));assert(Reloaded.GetState(D.Key)==ReditusQuestRuntime::State::Ready);

    UReditusQuestAsset Graph;Graph.QuestID="GraphHunt";Graph.CompiledDefinition=Hunt;
    Graph.RegisteredQuest={&Catalog,"GraphHunt"};FReditusQuestCatalogRow GraphRow;GraphRow.GraphQuest=&Graph;
    Catalog.Rows["GraphHunt"]=&GraphRow;auto GraphHandle=Graph.RegisteredQuest;
    assert(!Definition(GraphHandle));Graph.Compiled=true;
    assert(Canonical(GraphHandle)==GraphHandle&&Definition(GraphHandle)==&Graph.CompiledDefinition);
    FDataTableRowHandle Found;assert(UReditusQuestCatalogLibrary::FindQuestInCatalog(&Catalog,"GraphHunt",Found)&&Found==GraphHandle);
    assert(UReditusQuestCatalogLibrary::FindQuestInCatalog(&Catalog,"HuntAlias",Found)&&Found==Direct);
    assert(!UReditusQuestCatalogLibrary::FindQuestInCatalog(&Simple,"Test_DefeatBoss",Found)&&!Found.DataTable);
    Catalog.Rows["DuplicateGraph"]=&GraphRow;assert(!Definition({&Catalog,"DuplicateGraph"}));
    GraphRow.SimpleQuest=Direct;assert(!Definition(GraphHandle));GraphRow.SimpleQuest={};
    Graph.QuestID="ChangedID";assert(!Definition(GraphHandle));Graph.QuestID="GraphHunt";
    Graph.RegisteredQuest=AliasHandle;assert(!Definition(GraphHandle));Graph.RegisteredQuest=GraphHandle;
    Alias.SimpleQuest=GraphHandle;assert(!Definition(AliasHandle));Alias.SimpleQuest=Direct;
    Alias.GraphQuest=&Graph;assert(!Definition(AliasHandle));Alias.GraphQuest=nullptr;
    assert(!Definition({nullptr,"Missing"})&&!Definition({&Simple,"Missing"}));

    UDialogue Recruitment,Offer,Ready;Graph.AvailableDialogue=&Offer;Graph.ReadyToDeliverDialogue=&Ready;
    FReditusQuestStageConversation ConversationEntry;ConversationEntry.StageID="Hunt";ConversationEntry.Dialogue=&Recruitment;
    Graph.StageConversations.push_back(ConversationEntry);
    assert(Conversation(GraphHandle,EReditusQuestState::Active,"Hunt")==&Recruitment);
    assert(Conversation(GraphHandle,EReditusQuestState::Available,NAME_None)==&Offer);
    assert(Conversation(GraphHandle,EReditusQuestState::ReadyToDeliver,"Hunt")==&Ready);
    assert(!Conversation(GraphHandle,EReditusQuestState::Active,"Other"));
    assert(!Conversation(Direct,EReditusQuestState::Active,"Hunt"));
    assert(Recruitment.Events==7); // Resolving a conversation never edits or executes it.

    // Shared native adapter, not a parallel reimplementation of quest validation.
    auto Invalid=Hunt;Invalid.Rewards.Gold=-1;assert(!BuildDefinition(Invalid,ID,D,Why));
    Invalid=Hunt;Invalid.Stages[0].StageID=NAME_None;assert(BuildDefinition(Invalid,ID,D,Why));
    ReditusQuestRuntime::Engine BadEngine;assert(!BadEngine.AddDefinition(D,Error));
    Invalid=Hunt;Invalid.Stages[0].Timer.Mode=EReditusQuestTimerMode::Deadline;
    Invalid.Stages[0].Timer.DurationSeconds=300;assert(BuildDefinition(Invalid,ID,D,Why));
    assert(BadEngine.AddDefinition(D,Error));
    Invalid=Hunt;Invalid.Stages[0].Objectives[0].Kind=EReditusQuestObjectiveKind::CollectItem;
    assert(!BuildDefinition(Invalid,ID,D,Why));
    Invalid=Hunt;FReditusQuestStepAction Action;Action.ActionID="Pay";Action.GoldRewardChoiceID="MissingChoice";
    Invalid.Stages[0].Actions.push_back(Action);assert(!BuildDefinition(Invalid,ID,D,Why));
}
