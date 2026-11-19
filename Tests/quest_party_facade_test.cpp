#include "reflection_standins.h"
struct FArrayProperty:FProperty
{
    using FProperty::FProperty;
    FProperty* Inner=nullptr;
    TArray<FName> Values;
    template<class T>T* ContainerPtrToValuePtr(UObject*){return reinterpret_cast<T*>(&Values);}
};
struct FScriptArrayHelper
{
    TArray<FName>& Values;
    FScriptArrayHelper(FArrayProperty*,void* P):Values(*static_cast<TArray<FName>*>(P)){}
    int32 Num()const{return Values.Num();}
    void* GetRawPtr(int32 I){return &Values.at(I);}
};
// ACTUAL_PARTY_FACADE
struct Fixture
{
    APlayerController PC;
    FNameProperty Element{"Element",0,0};
    FIntProperty Wrong{"Wrong",0,0};
    FArrayProperty* Roster;FArrayProperty* Party;
    int LastLevel=0;
    Fixture()
    {
        Roster=PC.Class.Add<FArrayProperty>("UnlockedPartyCharacterIDs",0,0);Roster->Inner=&Element;
        Party=PC.Class.Add<FArrayProperty>("ActivePartyCharacterIDs",0,0);Party->Inner=&Element;
        auto Unlock=std::make_unique<UFunction>();Unlock->Label="Unlock";Unlock->ParmsSize=16;
        Unlock->Add<FNameProperty>("CharacterID",CPF_Parm,0);Unlock->Add<FIntProperty>("StartingLevel",CPF_Parm,8);
        Unlock->Body=[this](APlayerController&,void* P){FName ID;std::memcpy(&ID,P,sizeof(ID));std::memcpy(&LastLevel,static_cast<uint8*>(P)+8,sizeof(LastLevel));Roster->Values.push_back(ID);};
        PC.Functions.emplace("UnlockCharacterAtLevel",std::move(Unlock));
        auto Add=std::make_unique<UFunction>();Add->Label="Party";Add->ParmsSize=8;Add->Add<FNameProperty>("CharacterID",CPF_Parm,0);
        Add->Body=[this](APlayerController&,void* P){FName ID;std::memcpy(&ID,P,sizeof(ID));Party->Values.push_back(ID);};
        PC.Functions.emplace("AddCharacterIDToActiveParty",std::move(Add));
    }
};
int main()
{
    using namespace ReditusQuestPartyFacade;
    Fixture F;assert(Recruit(&F.PC,"Rover",8,true,4));assert(Owned(&F.PC,"Rover"));assert(F.LastLevel==8);
    assert(F.Party->Values.Contains("Rover"));assert(F.PC.Calls.size()==2);
    assert(!Recruit(&F.PC,"Rover",1,true,4));assert(F.PC.Calls.size()==2 && F.LastLevel==8);
    Fixture Full;Full.Party->Values={"A","B","C","D"};assert(Recruit(&Full.PC,"Rover",10,true,4));
    assert(Owned(&Full.PC,"Rover") && !Full.Party->Values.Contains("Rover") && Full.PC.Calls.size()==1);
    Fixture Reserve;assert(Recruit(&Reserve.PC,"Rover",1,false,4));assert(Reserve.Party->Values.empty());
    Fixture Bad;Bad.Roster->Inner=&Bad.Wrong;assert(!Recruit(&Bad.PC,"Rover",1,true,4));assert(Bad.PC.Calls.empty());
    Fixture Missing;Missing.PC.Functions.erase("UnlockCharacterAtLevel");assert(!Recruit(&Missing.PC,"Rover",1,true,4));
    assert(!Owned(nullptr,"Rover"));assert(!Recruit(nullptr,"Rover",1,true,4));
    Fixture Wrong;Wrong.PC.Functions.at("UnlockCharacterAtLevel")->Props[1]->Name="Experience";
    assert(!Recruit(&Wrong.PC,"Rover",1,true,4));assert(Wrong.PC.Calls.empty());
    Fixture Ref;Ref.PC.Functions.at("UnlockCharacterAtLevel")->Props[1]->Flags|=CPF_OutParm;
    assert(!Recruit(&Ref.PC,"Rover",1,true,4));assert(Ref.PC.Calls.empty());
    Fixture Failed;Failed.PC.Functions.at("UnlockCharacterAtLevel")->Body=[](APlayerController&,void*){};
    assert(!Recruit(&Failed.PC,"Rover",1,true,4));assert(Failed.Party->Values.empty());
    Fixture Listener;Listener.PC.Functions.at("UnlockCharacterAtLevel")->Body=[&Listener](APlayerController&,void*){Listener.Roster->Values.push_back("Rover");Listener.Party->Values.push_back("Rover");};
    assert(Recruit(&Listener.PC,"Rover",1,true,4));assert(Listener.Party->Values.size()==1 && Listener.PC.Calls.size()==1);
    Fixture Invalid;assert(!Recruit(&Invalid.PC,NAME_None,1,true,4));assert(!Recruit(&Invalid.PC,"Rover",0,true,4));
    assert(Invalid.PC.Calls.empty());
}
