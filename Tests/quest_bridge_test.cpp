#include "quest_bridge_standins.h"
#include "ReditusQuestNPCPolicy.h"

// The runner inserts the actual ReditusQuestBridge.cpp here, excluding only includes.
// ACTUAL_BRIDGE

struct Fixture
{
    APlayerController Player;
    UClass Base,Quest,ItemBase,ItemQuest;
    UEnum StatusEnum;
    std::vector<Entry> Entries;
    uint8 Status=1;
    int Inventory=5;
    int Rewards=0;
    FClassProperty MapKey{FName("Key"),0,0};
    FIntProperty MapValue{FName("Value"),0,0};

    UFunction* Function(const char* Name)
    {
        auto F=std::make_unique<UFunction>();F->Label=Name;
        auto* R=F.get();Player.Functions[FName(Name)]=std::move(F);return R;
    }
    void QuestParameter(UFunction* F)
    {auto* Q=F->Add<FClassProperty>(FName("quest"),CPF_Parm,0);Q->MetaClass=&Base;}
    Fixture()
    {
        Base.Path="QuestBase";
        Quest.Super=&Base;
        ItemBase.Super=&Base;
        ItemBase.Path="/Game/TurnBasedJRPGTemplate/Blueprints/Quests/ItemQuests/BP_ItemQuestBase.BP_ItemQuestBase_C";
        ItemQuest.Super=&ItemBase;
        for(auto* C:{&Quest,&ItemQuest})
        {
            auto* S=C->Add<FIntProperty>(FName("steps"),0,0);S->SetPropertyValue_InContainer(C->GetDefaultObject(),3);
            auto* M=C->Add<FBoolProperty>(FName("IsMainQuest"),0,8);M->SetPropertyValue_InContainer(C->GetDefaultObject(),true);
        }
        ItemQuest.Add<FNameProperty>(FName("itemToCollect"),0,16)->SetPropertyValue_InContainer(ItemQuest.GetDefaultObject(),FName("Stones"));
        Entries={{&Quest,2,false},{&Quest,2,true},{&ItemQuest,3,true}};
        auto* Map=Player.Class.Add<FMapProperty>(FName("activeQuests"),0,0);
        Map->KeyProp=&MapKey;Map->ValueProp=&MapValue;Map->Entries=&Entries;
        // Intentionally reorder enum VALUES: the bridge must use enumerator names.
        StatusEnum.Names={{1,"NewEnumerator0"},{7,"NewEnumerator4"},{9,"NewEnumerator1"},{11,"NewEnumerator2"},{13,"NewEnumerator3"}};
        auto* Get=Function("GetQuestStatus");Get->NumParms=2;QuestParameter(Get);
        auto* Out=Get->Add<FByteProperty>(FName("questStatus"),CPF_Parm|CPF_OutParm,16);Out->Enum=&StatusEnum;
        Get->Body=[this,Out](APlayerController&,void* M){Out->SetPropertyValue_InContainer(M,Status);};
        auto* Start=Function("AddQuest");Start->NumParms=1;QuestParameter(Start);
        Start->Body=[this](APlayerController&,void*){Status=9;};
        auto* Advance=Function("UpdateQuest");Advance->NumParms=5;QuestParameter(Advance);
        auto* Steps=Advance->Add<FIntProperty>(FName("stepsProgress"),CPF_Parm,16);
        auto* Override=Advance->Add<FBoolProperty>(FName("overrideProgress"),CPF_Parm,24);
        auto* Quiet=Advance->Add<FBoolProperty>(FName("ignoreNotification"),CPF_Parm,25);
        auto* NoDeliver=Advance->Add<FBoolProperty>(FName("IsNotDeliverable"),CPF_Parm,26);
        Advance->Body=[this,Steps,Override,Quiet,NoDeliver](APlayerController&,void* M)
        {
            assert(Steps->GetPropertyValue_InContainer(M)>0);
            assert(!Override->GetPropertyValue_InContainer(M));
            assert(!Quiet->GetPropertyValue_InContainer(M));
            assert(NoDeliver->GetPropertyValue_InContainer(M));
            Status=11;
        };
        auto* Deliver=Function("DeliverQuest");Deliver->NumParms=2;QuestParameter(Deliver);
        Deliver->Add<FBoolProperty>(FName("Show Quest Rewards"),CPF_Parm,16);
        Deliver->Body=[this](APlayerController&,void*){Status=13;++Rewards;};
        auto* GetAmount=Function("GetItemAmount");GetAmount->NumParms=2;
        auto* Item=GetAmount->Add<FNameProperty>(FName("item"),CPF_Parm,0);
        auto* Amount=GetAmount->Add<FIntProperty>(FName("amount"),CPF_Parm|CPF_OutParm,16);
        GetAmount->Body=[this,Item,Amount](APlayerController&,void* M)
        {assert(Item->GetPropertyValue_InContainer(M)==FName("Stones"));Amount->SetPropertyValue_InContainer(M,Inventory);};
        auto* Remove=Function("RemoveItemFromInventory");Remove->NumParms=2;
        auto* RemoveItem=Remove->Add<FNameProperty>(FName("item"),CPF_Parm,0);
        auto* RemoveAmount=Remove->Add<FIntProperty>(FName("amount"),CPF_Parm,16);
        Remove->Body=[this,RemoveItem,RemoveAmount](APlayerController&,void* M)
        {assert(RemoveItem->GetPropertyValue_InContainer(M)==FName("Stones"));Inventory-=RemoveAmount->GetPropertyValue_InContainer(M);};
    }
};

int main()
{
    using namespace ReditusQuestNPCPolicy;
    for(int Actual=0;Actual<6;++Actual)
        for(int Required=0;Required<6;++Required)
            for(int Progress=0;Progress<7;++Progress)
            {
                const auto A=static_cast<State>(Actual),R=static_cast<State>(Required);
                assert(RuleMatches(A,R,false,Progress,2,4)==(Actual!=0 && Actual==Required && Progress>=2 && Progress<4));
                assert(ObjectiveVisible(A,Progress,0,3)==(A==State::Active && Progress<3));
            }
    assert(!NPCMarkerVisible(State::Completed,true,true,true));
    assert(NPCMarkerVisible(State::Available,true,false,true));
    assert(!NPCMarkerVisible(State::Active,true,false,true));
    assert(NPCMarkerVisible(State::ReadyToDeliver,true,false,true));
    assert(!ActionAllowed(State::Completed,Action::TurnIn));
    assert(InRange(999,0,-1));

    Fixture F;
    FReditusQuestSnapshot S;
    assert(UReditusQuestBridge::ReadQuest(&F.Player,&F.Quest,S));
    assert(S.State==EReditusQuestState::Available && S.Progress==2 && S.Goal==3 && S.IsMainQuest);
    assert(UReditusQuestBridge::ApplyQuestAction(&F.Player,&F.Quest,EReditusQuestAction::StartQuest));
    assert(!UReditusQuestBridge::ApplyQuestAction(&F.Player,&F.Quest,EReditusQuestAction::StartQuest));
    assert(!UReditusQuestBridge::ApplyQuestAction(&F.Player,&F.Quest,EReditusQuestAction::AdvanceQuest,0));
    assert(UReditusQuestBridge::ApplyQuestAction(&F.Player,&F.Quest,EReditusQuestAction::AdvanceQuest));
    assert(UReditusQuestBridge::ApplyQuestAction(&F.Player,&F.Quest,EReditusQuestAction::TurnInQuest));
    assert(F.Rewards==1 && F.Inventory==5);
    assert(!UReditusQuestBridge::ApplyQuestAction(&F.Player,&F.Quest,EReditusQuestAction::TurnInQuest));
    assert(F.Rewards==1);

    Fixture Items;Items.Status=11;
    assert(UReditusQuestBridge::ApplyQuestAction(&Items.Player,&Items.ItemQuest,EReditusQuestAction::TurnInQuest));
    assert(Items.Inventory==2 && Items.Rewards==1);
    assert(Items.Player.Calls[Items.Player.Calls.size()-2]=="DeliverQuest");
    assert(Items.Player.Calls.back()=="RemoveItemFromInventory");
    assert(!UReditusQuestBridge::ApplyQuestAction(&Items.Player,&Items.ItemQuest,EReditusQuestAction::TurnInQuest));
    assert(Items.Inventory==2 && Items.Rewards==1);

    Fixture Insufficient;Insufficient.Status=11;Insufficient.Inventory=2;
    assert(!UReditusQuestBridge::ApplyQuestAction(&Insufficient.Player,&Insufficient.ItemQuest,EReditusQuestAction::TurnInQuest));
    assert(Insufficient.Rewards==0 && Insufficient.Inventory==2);
    Fixture Missing;Missing.Status=11;Missing.Player.Functions.erase(FName("RemoveItemFromInventory"));
    assert(!UReditusQuestBridge::ApplyQuestAction(&Missing.Player,&Missing.ItemQuest,EReditusQuestAction::TurnInQuest));
    assert(Missing.Rewards==0 && Missing.Inventory==5);
    Fixture Wrong;Wrong.StatusEnum.Path="UnrelatedEnum";
    assert(!UReditusQuestBridge::ReadQuest(&Wrong.Player,&Wrong.Quest,S));
    assert(S.State==EReditusQuestState::Unknown);
    assert(!UReditusQuestBridge::ApplyQuestAction(nullptr,&Wrong.Quest,EReditusQuestAction::StartQuest));
    Fixture Extra;
    Extra.Player.FindFunction(FName("AddQuest"))->Add<FBoolProperty>(FName("Unexpected"),CPF_Parm,32);
    assert(!UReditusQuestBridge::ApplyQuestAction(&Extra.Player,&Extra.Quest,EReditusQuestAction::StartQuest));
    assert(Extra.Status==1);
    return 0;
}
