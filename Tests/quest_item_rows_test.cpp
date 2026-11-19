// Only the actual inventory lookup is compiled here, not an Unreal simulation.
#include <algorithm>
#include <cassert>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <cctype>
#define TEXT(V) V
using int32=int;
using TCHAR=char;
template<class T>using TArray=std::vector<T>;
struct FName
{
    std::string Value;
    FName()=default;FName(const char* V):Value(V){std::transform(Value.begin(),Value.end(),Value.begin(),[](unsigned char C){return char(std::tolower(C));});}
    bool IsNone()const{return Value.empty()||Value=="None";}
    bool operator==(const FName& R)const{return Value==R.Value;}
    bool operator<(const FName& R)const{return Value<R.Value;}
};
struct Rows {std::set<FName> Names;bool Contains(FName N)const{return Names.count(N)>0;}};
struct ObjectPath {std::string V;const char* operator*()const{return V.c_str();}};
struct UDataTable {Rows Values;std::string Path;const Rows& GetRowMap()const{return Values;}ObjectPath GetPathName()const{return {Path};}};
struct FDataTableRowHandle {UDataTable* DataTable=nullptr;FName RowName;};
struct FProperty{virtual ~FProperty()=default;};
struct FNameProperty:FProperty{FName GetPropertyValue(const void* P)const{return *static_cast<const FName*>(P);}};
struct FIntProperty:FProperty{int32 GetPropertyValue(const void* P)const{return *static_cast<const int32*>(P);}};
struct Entry{FName Item;int32 Amount;bool Valid;};
struct FMapProperty:FProperty
{
    FProperty* KeyProp=nullptr;FProperty* ValueProp=nullptr;std::vector<Entry> Entries;
    template<class T> T* ContainerPtrToValuePtr(void*){return reinterpret_cast<T*>(this);}
};
struct FakeClass{std::map<FName,FProperty*> Properties;};
struct APlayerController{FakeClass Class;FakeClass* GetClass(){return &Class;}};
template<class T>T* FindFProperty(FakeClass* C,FName N)
{auto I=C->Properties.find(N);return I==C->Properties.end()?nullptr:dynamic_cast<T*>(I->second);}
template<class T>T* CastField(FProperty* P){return dynamic_cast<T*>(P);}
bool IsValid(APlayerController* P){return P!=nullptr;}
struct FScriptMapHelper
{
    FMapProperty* Map;
    FScriptMapHelper(FMapProperty* P,void*):Map(P){}
    int32 GetMaxIndex()const{return int32(Map->Entries.size());}
    bool IsValidIndex(int32 I)const{return Map->Entries[unsigned(I)].Valid;}
    const void* GetKeyPtr(int32 I)const{return &Map->Entries[unsigned(I)].Item;}
    const void* GetValuePtr(int32 I)const{return &Map->Entries[unsigned(I)].Amount;}
};
namespace FMath{int32 Max(int32 A,int32 B){return std::max(A,B);}}
enum class EReditusQuestItemKind{Usable,Misc,KeyItem,Equipment};
struct FReditusQuestItemAmount {FDataTableRowHandle ItemRow;EReditusQuestItemKind Kind=EReditusQuestItemKind::Misc;int Amount=1;};
struct UReditusQuestSubsystem
{bool GetQuestItemCount(APlayerController*,FDataTableRowHandle,EReditusQuestItemKind,int32&)const;};
// ACTUAL_ITEM_ROUTING
// ACTUAL_ITEM_ROW_VALIDATION
// ACTUAL_INVENTORY_LOOKUP
int main()
{
    UDataTable Usable,Equipment,Keys,Misc;for(auto* T:{&Usable,&Equipment,&Keys,&Misc})T->Values.Names.insert(FName("Shared"));
    const std::string Prefix="/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/";
    Usable.Path=Prefix+"DT_UsableItems.DT_UsableItems";Equipment.Path=Prefix+"DT_Equipment.DT_Equipment";
    Keys.Path=Prefix+"DT_KeyItems.DT_KeyItems";Misc.Path=Prefix+"DT_MiscItems.DT_MiscItems";
    FNameProperty Name;FIntProperty Int;FProperty Wrong;
    FMapProperty U,E,K,M;
    for(auto* Map:{&U,&E,&K,&M}){Map->KeyProp=&Name;Map->ValueProp=&Int;}
    U.Entries={{FName("Shared"),3,true}};
    E.Entries={{FName("Shared"),999,false},{FName("Shared"),7,true}};
    K.Entries={{FName("Shared"),1,true}};M.Entries={{FName("Shared"),-5,true}};
    APlayerController P;P.Class.Properties={{FName("usableItems"),&U},{FName("equipment"),&E},
        {FName("KeyItems"),&K},{FName("miscItems"),&M}};
    UReditusQuestSubsystem S;int32 Count=0;
    assert(S.GetQuestItemCount(&P,{&Usable,FName("Shared")},EReditusQuestItemKind::Usable,Count)&&Count==3);
    assert(S.GetQuestItemCount(&P,{&Equipment,FName("Shared")},EReditusQuestItemKind::Equipment,Count)&&Count==7);
    assert(S.GetQuestItemCount(&P,{&Keys,FName("Shared")},EReditusQuestItemKind::KeyItem,Count)&&Count==1);
    assert(S.GetQuestItemCount(&P,{&Misc,FName("Shared")},EReditusQuestItemKind::Misc,Count)&&Count==0);
    // Every stale/invalid legacy selection is ignored. Same row names stay in their table's bag.
    for(int Legacy=0;Legacy<256;++Legacy){
        const auto WrongKind=static_cast<EReditusQuestItemKind>(Legacy);
        assert(S.GetQuestItemCount(&P,{&Usable,FName("Shared")},WrongKind,Count)&&Count==3);
        assert(S.GetQuestItemCount(&P,{&Equipment,FName("Shared")},WrongKind,Count)&&Count==7);
        assert(S.GetQuestItemCount(&P,{&Keys,FName("Shared")},WrongKind,Count)&&Count==1);
        assert(S.GetQuestItemCount(&P,{&Misc,FName("Shared")},WrongKind,Count)&&Count==0);
    }
    TArray<FReditusQuestItemAmount> Delivery;
    for(auto* Table:{&Usable,&Equipment,&Keys,&Misc})Delivery.push_back({{Table,FName("Shared")},EReditusQuestItemKind::Misc,1});
    assert(ReditusQuestItemRouting::NormalizeItems(Delivery));
    assert(Delivery[0].Kind==EReditusQuestItemKind::Usable&&Delivery[1].Kind==EReditusQuestItemKind::Equipment);
    assert(Delivery[2].Kind==EReditusQuestItemKind::KeyItem&&Delivery[3].Kind==EReditusQuestItemKind::Misc);
    UDataTable Unrelated;Unrelated.Path="/Game/Other/DT_UsableItems.DT_UsableItems";Unrelated.Values.Names.insert("Shared");
    Count=99;assert(!S.GetQuestItemCount(&P,{&Unrelated,FName("Shared")},EReditusQuestItemKind::Usable,Count)&&Count==0);
    Delivery[0].Kind=EReditusQuestItemKind::Equipment;Delivery.push_back({{&Unrelated,FName("Shared")},EReditusQuestItemKind::Usable,1});
    assert(!ReditusQuestItemRouting::NormalizeItems(Delivery)&&Delivery[0].Kind==EReditusQuestItemKind::Equipment);Delivery.pop_back();
    Delivery[0].Amount=0;assert(!ReditusQuestItemRouting::NormalizeItems(Delivery));Delivery[0].Amount=1;
    // Case differences in an Unreal object path do not change catalog identity.
    std::transform(Usable.Path.begin(),Usable.Path.end(),Usable.Path.begin(),[](unsigned char C){return char(std::tolower(C));});
    assert(S.GetQuestItemCount(&P,{&Usable,FName("Shared")},EReditusQuestItemKind::KeyItem,Count)&&Count==3);
    assert(!S.GetQuestItemCount(&P,{&Equipment,FName("Missing")},EReditusQuestItemKind::Equipment,Count));
    assert(!S.GetQuestItemCount(&P,{nullptr,FName("Shared")},EReditusQuestItemKind::Equipment,Count));
    assert(!S.GetQuestItemCount(nullptr,{&Equipment,FName("Shared")},EReditusQuestItemKind::Equipment,Count));
    E.KeyProp=&Wrong;assert(!S.GetQuestItemCount(&P,{&Equipment,FName("Shared")},EReditusQuestItemKind::Equipment,Count));
    E.KeyProp=&Name;E.ValueProp=&Wrong;
    assert(!S.GetQuestItemCount(&P,{&Equipment,FName("Shared")},EReditusQuestItemKind::Equipment,Count));
    E.ValueProp=&Int;P.Class.Properties.erase(FName("equipment"));
    assert(!S.GetQuestItemCount(&P,{&Equipment,FName("Shared")},EReditusQuestItemKind::Equipment,Count));
}
