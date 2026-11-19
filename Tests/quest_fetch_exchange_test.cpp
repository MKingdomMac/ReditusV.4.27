// Compile the actual reflected exchange against typed stand-ins. This does not
// execute Unreal reflection, delegates, Blueprint inventory menus or SaveGame.
#include "ReditusQuestRuntime.h"
#include "ReditusFetchQuestExchange.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <map>
#include <set>
using int32=int;using TCHAR=char;
#define TEXT(V) V
#define TCHAR_TO_UTF8(V) V
const int INDEX_NONE=-1;
struct FString:std::string{using std::string::string;FString(const std::string& V):std::string(V){}const char* operator*()const{return c_str();}};
template<class T>struct TGuardValue{T& Ref;T Before;TGuardValue(T& R,T V):Ref(R),Before(R){R=V;}~TGuardValue(){Ref=Before;}};
template<class T>struct TArray:std::vector<T>{using std::vector<T>::vector;int Num()const{return int(this->size());}int Add(const T& V){this->push_back(V);return Num()-1;}};
struct FName
{
    std::string Value;FName()=default;FName(const char* V):Value(V){std::transform(Value.begin(),Value.end(),Value.begin(),[](unsigned char C){return char(std::tolower(C));});}
    bool IsNone()const{return Value.empty();}bool operator==(const FName& R)const{return Value==R.Value;}bool operator<(const FName& R)const{return Value<R.Value;}
};
struct Rows{std::set<FName> Values;bool Contains(FName V)const{return Values.count(V)>0;}};
struct UDataTable{FString Path;Rows Values;const Rows& GetRowMap()const{return Values;}FString GetPathName()const{return Path;}};
struct FDataTableRowHandle{UDataTable* DataTable=nullptr;FName RowName;};
struct FProperty{virtual ~FProperty()=default;};
struct FNameProperty:FProperty{FName GetPropertyValue(const void* P)const{return *static_cast<const FName*>(P);}};
struct FIntProperty:FProperty
{
    int32 Value=0;int32 GetPropertyValue(const void* P)const{return *static_cast<const int32*>(P);}
    int32 GetPropertyValue_InContainer(void*)const{return Value;}void SetPropertyValue_InContainer(void*,int32 V){Value=V;}
};
struct Entry{FName Key;int32 Amount=0;bool Valid=true;};
struct FMapProperty:FProperty{FProperty* KeyProp=nullptr;FProperty* ValueProp=nullptr;std::vector<Entry> Values;template<class T>T* ContainerPtrToValuePtr(void*){return reinterpret_cast<T*>(this);}};
struct FakeClass{std::map<FName,FProperty*> Properties;};
struct APlayerController{FakeClass Class;bool Local=true;FakeClass* GetClass(){return &Class;}bool IsLocalController()const{return Local;}};
bool IsValid(APlayerController* P){return P!=nullptr;}
template<class T>T* FindFProperty(FakeClass* C,FName N){auto I=C->Properties.find(N);return I==C->Properties.end()?nullptr:dynamic_cast<T*>(I->second);}
template<class T>T* CastField(FProperty* P){return dynamic_cast<T*>(P);}
struct FScriptMapHelper
{
    FMapProperty* Map;FScriptMapHelper(FMapProperty* P,void*):Map(P){}
    int GetMaxIndex()const{return int(Map->Values.size());}bool IsValidIndex(int I)const{return Map->Values[unsigned(I)].Valid;}
    const void* GetKeyPtr(int I)const{return &Map->Values[unsigned(I)].Key;}const void* GetValuePtr(int I)const{return &Map->Values[unsigned(I)].Amount;}
    bool RemovePair(const void* P){for(auto& E:Map->Values)if(E.Valid&&E.Key==*static_cast<const FName*>(P)){E.Valid=false;return true;}return false;}
    void AddPair(const void* K,const void* V){for(auto& E:Map->Values)if(E.Valid&&E.Key==*static_cast<const FName*>(K)){E.Amount=*static_cast<const int32*>(V);return;}Map->Values.push_back({*static_cast<const FName*>(K),*static_cast<const int32*>(V),true});}
};
enum class EReditusQuestItemKind{Usable,Misc,KeyItem,Equipment};
struct FReditusQuestItemAmount{FDataTableRowHandle ItemRow;int Amount=1;EReditusQuestItemKind Kind=EReditusQuestItemKind::Misc;};
enum class EReditusQuestDeliveryKind{TurnIn,StepAction};
struct FGuid{int Value=1;bool operator!=(const FGuid& R)const{return Value!=R.Value;}};
struct FReditusQuestDelivery{FDataTableRowHandle Quest;FGuid ReceiptID;EReditusQuestDeliveryKind Kind=EReditusQuestDeliveryKind::TurnIn;TArray<FReditusQuestItemAmount> ItemsToRemove;struct{int Gold=0,Experience=0;TArray<FReditusQuestItemAmount> Items;}Rewards;};
struct UReditusQuestSubsystem
{
    ReditusQuestRuntime::Engine Runtime;bool bImporting=false,bApplyingNPCAction=false,bAllowBuiltInFetchDelivery=false;
    struct{FReditusQuestDelivery Value;bool Exists=true;const FReditusQuestDelivery* Find(const FString&)const{return Exists?&Value:nullptr;}}Deliveries;
    bool RejectConfirm=false;int Confirmed=0;
    FString Key(FDataTableRowHandle)const{return "fetch";}
    bool ApplyBuiltInFetchDelivery(APlayerController*,const FReditusQuestDelivery&,FString&);
    bool ConfirmQuestDelivery(FDataTableRowHandle,FGuid){if(RejectConfirm)return false;assert(bApplyingNPCAction);++Confirmed;Deliveries.Exists=false;return true;}
    bool TurnInFetchQuest(FDataTableRowHandle,APlayerController*);
    bool TurnInDialogueQuest(FDataTableRowHandle,APlayerController*);
    bool TurnInQuest(FDataTableRowHandle,APlayerController*){return bAllowBuiltInFetchDelivery;}
};
// ACTUAL_ITEM_ROUTING
// ACTUAL_FETCH_EXCHANGE
static int Count(FMapProperty& Map,const char* Name,bool* Exists=nullptr){for(const auto& E:Map.Values)if(E.Valid&&E.Key==FName(Name)){if(Exists)*Exists=true;return E.Amount;}if(Exists)*Exists=false;return 0;}
int main()
{
    UDataTable Keys,Usable;Keys.Path="/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_KeyItems.DT_KeyItems";Usable.Path="/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_UsableItems.DT_UsableItems";
    Keys.Values.Values.insert("Shared");Usable.Values.Values.insert("Shared");
    FNameProperty Name;FIntProperty Quantity,Gold;Gold.Value=100;FProperty Wrong;
    FMapProperty K,U;K.KeyProp=U.KeyProp=&Name;K.ValueProp=U.ValueProp=&Quantity;
    K.Values={{FName("Shared"),1,true}};U.Values={{FName("Shared"),9,true}};
    APlayerController Player;Player.Class.Properties={{FName("KeyItems"),&K},{FName("usableItems"),&U},{FName("gold"),&Gold}};
    UReditusQuestSubsystem S;ReditusQuestRuntime::Definition D;D.Key="fetch";ReditusQuestRuntime::Stage Stage;Stage.Id="give";
    ReditusQuestRuntime::Task Task;Task.Id="item";Task.Type=ReditusQuestRuntime::Kind::CollectItem;Task.Target="letter";Task.Required=1;Stage.Tasks={Task};D.Stages={Stage};
    std::string E;assert(S.Runtime.AddDefinition(D,E));assert(S.Runtime.Start("fetch"));S.Runtime.SetInventory("fetch","item",1);
    FReditusQuestDelivery Delivery;Delivery.ItemsToRemove.push_back({{&Keys,FName("Shared")},1,EReditusQuestItemKind::Usable});
    Delivery.Rewards.Items.push_back({{&Usable,FName("Shared")},2,EReditusQuestItemKind::KeyItem});Delivery.Rewards.Gold=500;
    S.Deliveries.Value=Delivery;FString Error;
    assert(S.TurnInFetchQuest({},&Player)&&!S.bAllowBuiltInFetchDelivery);S.bApplyingNPCAction=true;assert(!S.TurnInFetchQuest({},&Player));S.bApplyingNPCAction=false;
    // Late preflight errors cannot partially take the letter or give gold.
    Delivery.Rewards.Experience=1;assert(!S.ApplyBuiltInFetchDelivery(&Player,Delivery,Error));Delivery.Rewards.Experience=0;
    assert(Count(K,"Shared")==1&&Count(U,"Shared")==9&&Gold.Value==100&&S.Confirmed==0);
    U.ValueProp=&Wrong;assert(!S.ApplyBuiltInFetchDelivery(&Player,Delivery,Error));U.ValueProp=&Quantity;
    Gold.Value=2147483647;assert(!S.ApplyBuiltInFetchDelivery(&Player,Delivery,Error));Gold.Value=100;
    S.RejectConfirm=true;assert(!S.ApplyBuiltInFetchDelivery(&Player,Delivery,Error));S.RejectConfirm=false;
    assert(Count(K,"Shared")==1&&Count(U,"Shared")==9&&Gold.Value==100&&S.Deliveries.Exists&&!S.bApplyingNPCAction);
    assert(S.ApplyBuiltInFetchDelivery(&Player,Delivery,Error));bool Exists=true;
    assert(Count(K,"Shared",&Exists)==0&&!Exists&&Count(U,"Shared")==11&&Gold.Value==600&&S.Confirmed==1);
    assert(!S.ApplyBuiltInFetchDelivery(&Player,Delivery,Error)&&Gold.Value==600&&S.Confirmed==1);
    // Exact rollback of absent reward entries and existing zero entries.
    S.Deliveries.Exists=true;K.Values={{FName("Shared"),1,true}};U.Values.clear();S.RejectConfirm=true;
    assert(!S.ApplyBuiltInFetchDelivery(&Player,Delivery,Error));assert(Count(U,"Shared",&Exists)==0&&!Exists&&Count(K,"Shared")==1);
    U.Values={{FName("Shared"),0,true}};assert(!S.ApplyBuiltInFetchDelivery(&Player,Delivery,Error));Count(U,"Shared",&Exists);assert(Exists);
    Player.Local=false;assert(!S.ApplyBuiltInFetchDelivery(&Player,Delivery,Error));Player.Local=true;
    S.bImporting=true;assert(!S.ApplyBuiltInFetchDelivery(&Player,Delivery,Error));S.bImporting=false;
    assert(!S.ApplyBuiltInFetchDelivery(nullptr,Delivery,Error));
    // Authored hunt dialogue uses the same exchange after a confirmed defeat.
    UReditusQuestSubsystem Hunt;
    D.Stages[0].Tasks[0].Type=ReditusQuestRuntime::Kind::DefeatEnemy;
    D.Stages[0].Tasks[0].Target="DeathBoar";
    assert(Hunt.Runtime.AddDefinition(D,E)&&Hunt.Runtime.Start("fetch"));
    FReditusQuestDelivery HuntDelivery;HuntDelivery.Rewards.Gold=500;
    Hunt.Deliveries.Value=HuntDelivery;Gold.Value=100;
    assert(!Hunt.ApplyBuiltInFetchDelivery(&Player,HuntDelivery,Error)&&Gold.Value==100);
    assert(Hunt.Runtime.Signal(ReditusQuestRuntime::Kind::DefeatEnemy,"DeathBoar",1,"kill1"));
    assert(Hunt.TurnInDialogueQuest({},&Player)&&!Hunt.bAllowBuiltInFetchDelivery);
    assert(Hunt.ApplyBuiltInFetchDelivery(&Player,HuntDelivery,Error)&&Gold.Value==600&&Hunt.Confirmed==1);
    assert(!Hunt.ApplyBuiltInFetchDelivery(&Player,HuntDelivery,Error)&&Gold.Value==600);
}
