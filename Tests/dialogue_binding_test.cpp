#include "quest_bridge_standins.h"

// The runner inserts the actual private reflection helper, excluding includes.
// ACTUAL_REFLECTION

struct BindFixture
{
    APlayerController Widget, Listener, Other;
    UFunction Signature;
    UEnum RecruitmentEnum;
    FMulticastInlineDelegateProperty* Removed;

    BindFixture()
    {
        Removed=Widget.Class.Add<FMulticastInlineDelegateProperty>(FName("Removed"),0,0);
        Removed->SignatureFunction=&Signature;
        auto Zero=std::make_unique<UFunction>();
        Listener.Functions[FName("HandleDialogueRemoved")]=std::move(Zero);
        auto WithResult=std::make_unique<UFunction>();
        WithResult->NumParms=1;WithResult->ParmsSize=1;
        WithResult->Add<FByteProperty>(FName("RecruitmentEvents"),CPF_Parm,0);
        Listener.Functions[FName("HandleDialogueRemovedWithResult")]=std::move(WithResult);
        FScriptDelegate Original;
        Original.BindUFunction(&Other,FName("ExistingRecruitmentListener"));
        Removed->Value.AddUnique(Original);
    }
    FByteProperty* AddRecruitmentResult()
    {
        Signature.NumParms=1;Signature.ParmsSize=1;
        auto* P=Signature.Add<FByteProperty>(FName("RecruitmentEvents"),CPF_Parm,0);
        P->Enum=&RecruitmentEnum;return P;
    }
};

int main()
{
    using namespace ReditusQuestNPCReflection;
    BindFixture Legacy;
    assert(BindDialogueRemoved(&Legacy.Widget,&Legacy.Listener,true));
    assert(Legacy.Removed->Value.Callbacks.size()==2);
    assert(Legacy.Removed->Value.Callbacks.back().Method==FName("HandleDialogueRemoved"));
    assert(BindDialogueRemoved(&Legacy.Widget,&Legacy.Listener,true));
    assert(Legacy.Removed->Value.Callbacks.size()==2);
    assert(BindDialogueRemoved(&Legacy.Widget,&Legacy.Listener,false));
    assert(Legacy.Removed->Value.Callbacks.size()==1);
    assert(Legacy.Removed->Value.Callbacks[0].Listener==&Legacy.Other);

    BindFixture Current;Current.AddRecruitmentResult();
    assert(BindDialogueRemoved(&Current.Widget,&Current.Listener,true));
    assert(Current.Removed->Value.Callbacks.size()==2);
    assert(Current.Removed->Value.Callbacks.back().Method==FName("HandleDialogueRemovedWithResult"));
    assert(BindDialogueRemoved(&Current.Widget,&Current.Listener,true));
    assert(Current.Removed->Value.Callbacks.size()==2);
    assert(BindDialogueRemoved(&Current.Widget,&Current.Listener,false));
    assert(Current.Removed->Value.Callbacks.size()==1);
    assert(Current.Removed->Value.Callbacks[0].Listener==&Current.Other);

    BindFixture Unexpected;Unexpected.Signature.Add<FBoolProperty>(FName("Other"),CPF_Parm,0);
    assert(!BindDialogueRemoved(&Unexpected.Widget,&Unexpected.Listener,true));
    assert(Unexpected.Removed->Value.Callbacks.size()==1);
    BindFixture More;More.AddRecruitmentResult();
    More.Signature.Add<FBoolProperty>(FName("Extra"),CPF_Parm,1);
    assert(!BindDialogueRemoved(&More.Widget,&More.Listener,true));
    BindFixture DifferentLayout;DifferentLayout.AddRecruitmentResult();
    DifferentLayout.Signature.ParmsSize=2;
    assert(!BindDialogueRemoved(&DifferentLayout.Widget,&DifferentLayout.Listener,true));
    BindFixture DifferentOffset;DifferentOffset.AddRecruitmentResult()->Offset=1;
    assert(!BindDialogueRemoved(&DifferentOffset.Widget,&DifferentOffset.Listener,true));
    BindFixture Output;Output.AddRecruitmentResult()->Flags|=CPF_OutParm;
    assert(!BindDialogueRemoved(&Output.Widget,&Output.Listener,true));
    BindFixture Missing;Missing.AddRecruitmentResult();
    Missing.Listener.Functions.erase(FName("HandleDialogueRemovedWithResult"));
    assert(!BindDialogueRemoved(&Missing.Widget,&Missing.Listener,true));
    assert(!BindDialogueRemoved(nullptr,&Missing.Listener,true));
    assert(!BindDialogueRemoved(&Missing.Widget,nullptr,true));
    return 0;
}
