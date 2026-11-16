#pragma once
// Small typed reflection stand-ins for compiling the ACTUAL bridge .cpp offline.
// These verify our call/guard logic, not Unreal ABI, UHT, rendering or Blueprint execution.
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#define TEXT(x) x
using uint8 = std::uint8_t;
using int32 = std::int32_t;
using FString = std::string;
constexpr unsigned CPF_Parm=1, CPF_OutParm=2, CPF_ReturnParm=4;

struct FName
{
    int32 Id=0;
    static std::vector<std::string>& Names() { static std::vector<std::string> N{"None"}; return N; }
    FName() = default;
    FName(const char* Name)
    {
        auto& N=Names(); auto I=std::find(N.begin(),N.end(),Name);
        if(I==N.end()) { N.push_back(Name); Id=static_cast<int32>(N.size()-1); }
        else Id=static_cast<int32>(I-N.begin());
    }
    bool IsNone() const {return Id==0;}
    bool operator==(FName B) const {return Id==B.Id;}
    bool operator!=(FName B) const {return Id!=B.Id;}
    bool operator<(FName B) const {return Id<B.Id;}
};
const FName NAME_None;
template<class T> struct TArray : std::vector<T>
{
    using std::vector<T>::vector;
    bool Contains(const T& V) const {return std::find(this->begin(),this->end(),V)!=this->end();}
    int32 Num() const {return static_cast<int32>(this->size());}
};
template<class T> using TUniquePtr=std::unique_ptr<T>;
template<class T,class... A> std::unique_ptr<T> MakeUnique(A&&... Args)
{return std::make_unique<T>(std::forward<A>(Args)...);}

struct UClass;
struct UFunction;
struct APlayerController;
struct UObject
{
    std::array<uint8,256> Bytes{};
    virtual ~UObject()=default;
    virtual UClass* GetClass(){return nullptr;}
    virtual UFunction* FindFunction(FName){return nullptr;}
    virtual void ProcessEvent(UFunction*,void*){}
    bool IsA(UClass* Expected);
};
struct FProperty
{
    FName Name;
    unsigned Flags;
    std::size_t Offset;
    int32 ArrayDim=1;
    FProperty(FName N,unsigned F,std::size_t O):Name(N),Flags(F),Offset(O){}
    virtual ~FProperty()=default;
    bool HasAnyPropertyFlags(unsigned F) const {return (Flags&F)!=0;}
    FName GetFName() const {return Name;}
    int32 GetOffset_ForInternal() const {return static_cast<int32>(Offset);}
};
struct UStruct:UObject
{
    std::vector<std::unique_ptr<FProperty>> Props;
    template<class T,class... A> T* Add(A&&... Args)
    {auto P=std::make_unique<T>(std::forward<A>(Args)...);auto* R=P.get();Props.push_back(std::move(P));return R;}
};
template<class T> struct ValueProperty:FProperty
{
    using FProperty::FProperty;
    void SetPropertyValue_InContainer(void* Mem,T V) {std::memcpy(static_cast<uint8*>(Mem)+Offset,&V,sizeof(T));}
    void SetPropertyValue_InContainer(UObject* Obj,T V) {SetPropertyValue_InContainer(Obj->Bytes.data(),V);}
    T GetPropertyValue_InContainer(const void* Mem) const {T V;std::memcpy(&V,static_cast<const uint8*>(Mem)+Offset,sizeof(T));return V;}
    T GetPropertyValue_InContainer(UObject* Obj) const {return GetPropertyValue_InContainer(Obj->Bytes.data());}
    T GetPropertyValue(const void* Mem) const {T V;std::memcpy(&V,Mem,sizeof(T));return V;}
};
struct FIntProperty:ValueProperty<int32> {using ValueProperty::ValueProperty;};
struct FBoolProperty:ValueProperty<bool> {using ValueProperty::ValueProperty;};
struct FNameProperty:ValueProperty<FName> {using ValueProperty::ValueProperty;};
struct UEnum
{
    FString Path="/Game/TurnBasedJRPGTemplate/Blueprints/Enums/E_QuestStatus.E_QuestStatus";
    std::map<int,FString> Names;
    FString GetPathName() const{return Path;}
    FString GetNameStringByValue(uint8 Value) const
    {auto I=Names.find(Value);return I==Names.end()?FString():I->second;}
};
struct FByteProperty:ValueProperty<uint8>
{
    using ValueProperty::ValueProperty;
    UEnum* Enum=nullptr;
};
struct FClassProperty:ValueProperty<UClass*>
{
    using ValueProperty::ValueProperty;
    UClass* MetaClass=nullptr;
    void SetObjectPropertyValue_InContainer(void* M,UClass* V){SetPropertyValue_InContainer(M,V);}
    UClass* GetObjectPropertyValue(const void* M) const{return GetPropertyValue(M);}
};
struct FScriptDelegate
{
    UObject* Listener=nullptr;
    FName Method;
    void BindUFunction(UObject* L,FName M){Listener=L;Method=M;}
    bool operator==(const FScriptDelegate& Other)const{return Listener==Other.Listener && Method==Other.Method;}
};
struct FMulticastScriptDelegate
{
    std::vector<FScriptDelegate> Callbacks;
    void AddUnique(const FScriptDelegate& C)
    {if(std::find(Callbacks.begin(),Callbacks.end(),C)==Callbacks.end())Callbacks.push_back(C);}
    void Remove(const FScriptDelegate& C)
    {Callbacks.erase(std::remove(Callbacks.begin(),Callbacks.end(),C),Callbacks.end());}
};
struct FMulticastInlineDelegateProperty:FProperty
{
    using FProperty::FProperty;
    UFunction* SignatureFunction=nullptr;
    FMulticastScriptDelegate Value;
    template<class T>T* ContainerPtrToValuePtr(UObject*){return reinterpret_cast<T*>(&Value);}
};
struct FObjectPropertyBase:FProperty
{
    using FProperty::FProperty;
    UClass* PropertyClass=nullptr;
    UObject* Value=nullptr;
    UObject* GetObjectPropertyValue_InContainer(UObject*)const{return Value;}
    void SetObjectPropertyValue_InContainer(UObject*,UObject* V){Value=V;}
};
struct Entry {UClass* Key;int32 Value;bool Valid=true;};
struct FMapProperty:FProperty
{
    using FProperty::FProperty;
    FProperty* KeyProp=nullptr;
    FProperty* ValueProp=nullptr;
    std::vector<Entry>* Entries=nullptr;
    template<class T> T* ContainerPtrToValuePtr(UObject*) const{return reinterpret_cast<T*>(Entries);}
};
struct FScriptMapHelper
{
    std::vector<Entry>& Entries;
    FScriptMapHelper(FMapProperty*,void* M):Entries(*static_cast<std::vector<Entry>*>(M)){}
    int32 GetMaxIndex() const{return static_cast<int32>(Entries.size());}
    bool IsValidIndex(int32 I) const{return Entries.at(I).Valid;}
    void* GetKeyPtr(int32 I){return &Entries.at(I).Key;}
    void* GetValuePtr(int32 I){return &Entries.at(I).Value;}
};
struct UClass:UStruct
{
    FString Path;
    UClass* Super=nullptr;
    UObject Defaults;
    FString GetPathName() const{return Path;}
    UClass* GetSuperClass() const{return Super;}
    UObject* GetDefaultObject(){return &Defaults;}
    bool IsChildOf(UClass* Parent) const
    {for(auto* C=this;C;C=C->Super) if(C==Parent)return true;return false;}
};
struct UFunction:UStruct
{
    int32 NumParms=0;
    int32 ParmsSize=0;
    std::function<void(APlayerController&,void*)> Body;
    std::string Label;
};
inline bool UObject::IsA(UClass* Expected){return GetClass() && GetClass()->IsChildOf(Expected);}
struct AActor:UObject {};
template<class T> struct TSubclassOf
{
    UClass* Ptr=nullptr;
    TSubclassOf()=default;
    TSubclassOf(UClass* P):Ptr(P){}
    UClass* Get() const{return Ptr;}
    UClass* operator->() const{return Ptr;}
    explicit operator bool() const{return Ptr!=nullptr;}
};
struct APlayerController:UObject
{
    UClass Class;
    std::map<FName,std::unique_ptr<UFunction>> Functions;
    std::vector<std::string> Calls;
    UClass* GetClass(){return &Class;}
    UFunction* FindFunction(FName N){auto I=Functions.find(N);return I==Functions.end()?nullptr:I->second.get();}
    void ProcessEvent(UFunction* F,void* M){Calls.push_back(F->Label);F->Body(*this,M);}
};
template<class T> bool IsValid(T* P){return P!=nullptr;}
template<class T> T* CastField(FProperty* P){return dynamic_cast<T*>(P);}
template<class T> T* FindFProperty(UStruct* Owner,FName N)
{
    if(!Owner)return nullptr;
    for(auto& P:Owner->Props)if(P->Name==N)return dynamic_cast<T*>(P.get());
    return nullptr;
}
template<class T> struct TFieldIterator
{
    UStruct* Struct;
    std::size_t I=0;
    explicit TFieldIterator(UStruct* S):Struct(S){}
    explicit operator bool()const{return Struct && I<Struct->Props.size();}
    TFieldIterator& operator++(){++I;return *this;}
    T* operator*()const{return static_cast<T*>(Struct->Props[I].get());}
    T* operator->()const{return **this;}
};
struct FStructOnScope
{
    std::array<uint8,256> Memory{};
    explicit FStructOnScope(UFunction*){}
    void* GetStructMemory(){return Memory.data();}
};
enum class EReditusQuestState:uint8 {Unknown,Unavailable,Available,Active,ReadyToDeliver,Completed};
enum class EReditusQuestAction:uint8 {StartQuest,AdvanceQuest,TurnInQuest};
struct FReditusQuestSnapshot
{
    EReditusQuestState State=EReditusQuestState::Unknown;
    int32 Progress=0,Goal=0;
    bool IsMainQuest=false;
};
struct UReditusQuestBridge
{
    static bool ReadQuest(APlayerController*,TSubclassOf<AActor>,FReditusQuestSnapshot&);
    static bool ApplyQuestAction(APlayerController*,TSubclassOf<AActor>,EReditusQuestAction,int32=1,bool=true);
};
