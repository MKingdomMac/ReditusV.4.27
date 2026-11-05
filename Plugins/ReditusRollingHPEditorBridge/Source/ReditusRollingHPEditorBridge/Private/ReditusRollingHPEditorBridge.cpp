#include "Modules/ModuleManager.h"
#include "HAL/IConsoleManager.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Self.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompiler.h"
#include "ScopedTransaction.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogReditusHPBridge,Log,All);

namespace
{
    struct FHPRead
    {
        UK2Node_VariableGet* Getter=nullptr;
        UEdGraphPin* Output=nullptr;
        TArray<UEdGraphPin*> Consumers;
    };

    TArray<UEdGraphNode*> ExecutedNodes(UEdGraphNode* Root)
    {
        TArray<UEdGraphNode*> Result,Pending;
        Pending.Add(Root);
        while (Pending.Num()>0)
        {
            UEdGraphNode* Node=Pending.Pop(false);
            if (!Node || Result.Contains(Node)) continue;
            Result.Add(Node);
            for (UEdGraphPin* Pin:Node->Pins)
                if (Pin && Pin->Direction==EGPD_Output && Pin->PinType.PinCategory==UEdGraphSchema_K2::PC_Exec)
                    for (UEdGraphPin* Other:Pin->LinkedTo) Pending.AddUnique(Other->GetOwningNode());
        }
        return Result;
    }

    void CollectHPReads(UEdGraphPin* Input,TMap<UK2Node_VariableGet*,FHPRead>& Reads,TSet<UEdGraphPin*>& Seen)
    {
        if (!Input || Seen.Contains(Input)) return;
        Seen.Add(Input);
        for (UEdGraphPin* Output:Input->LinkedTo)
        {
            UEdGraphNode* Source=Output ? Output->GetOwningNode() : nullptr;
            auto* Getter=Cast<UK2Node_VariableGet>(Source);
            if (Getter && Getter->VariableReference.GetMemberName()==TEXT("currentHP"))
            {
                FHPRead& Read=Reads.FindOrAdd(Getter);
                Read.Getter=Getter; Read.Output=Output; Read.Consumers.AddUnique(Input);
                continue;
            }
            // Only follow value dependencies. Never walk into another event's
            // execution chain, a death check's implementation, or a target selector.
            if (Source)
                for (UEdGraphPin* Pin:Source->Pins)
                    if (Pin && Pin->Direction==EGPD_Input && Pin->PinType.PinCategory!=UEdGraphSchema_K2::PC_Exec)
                        CollectHPReads(Pin,Reads,Seen);
        }
    }

    UK2Node_CallFunction* NewCall(UEdGraph* Graph,UFunction* Function,int32 X,int32 Y)
    {
        auto* Node=NewObject<UK2Node_CallFunction>(Graph);
        Graph->AddNode(Node,false,false);
        Node->CreateNewGuid();
        Node->SetFromFunction(Function);
        Node->AllocateDefaultPins();
        Node->NodePosX=X; Node->NodePosY=Y;
        return Node;
    }

    bool ConnectUnit(const UEdGraphSchema_K2* Schema,UEdGraph* Graph,UEdGraphPin* OriginalTarget,UEdGraphPin* NewTarget)
    {
        if (!NewTarget) return false;
        if (OriginalTarget && OriginalTarget->LinkedTo.Num()==1)
            return Schema->TryCreateConnection(OriginalTarget->LinkedTo[0],NewTarget);
        if (OriginalTarget && OriginalTarget->LinkedTo.Num()>1) return false;
        auto* Self=NewObject<UK2Node_Self>(Graph);
        Graph->AddNode(Self,false,false); Self->CreateNewGuid(); Self->AllocateDefaultPins();
        Self->NodePosX=NewTarget->GetOwningNode()->NodePosX-180;
        Self->NodePosY=NewTarget->GetOwningNode()->NodePosY+70;
        return Schema->TryCreateConnection(Self->FindPin(UEdGraphSchema_K2::PN_Self),NewTarget);
    }

    void InstallBridge()
    {
        if (!GEditor || GEditor->PlayWorld)
        {
            UE_LOG(LogReditusHPBridge,Error,TEXT("Stop Play before installing the HP bridge."));
            return;
        }
        UBlueprint* Blueprint=LoadObject<UBlueprint>(nullptr,
            TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Units/BP_UnitBase.BP_UnitBase"));
        UClass* Library=FindObject<UClass>(nullptr,TEXT("/Script/Chapter1Reditus.ReditusRollingHPLibrary"));
        UFunction* GetHP=Library ? Library->FindFunctionByName(TEXT("GetHPForCalculation")) : nullptr;
        UFunction* ResolveHP=Library ? Library->FindFunctionByName(TEXT("ResolveHPWrite")) : nullptr;
        UFunction* CanHeal=Library ? Library->FindFunctionByName(TEXT("CanReceiveBattleHealing")) : nullptr;
        if (!Blueprint || !GetHP || !ResolveHP || !CanHeal)
        {
            UE_LOG(LogReditusHPBridge,Error,TEXT("BP_UnitBase or the new runtime HP functions are missing. Build Chapter1ReditusEditor first."));
            return;
        }
        // Compilation can reconstruct nodes/pins, so do it before collecting
        // pointers for the edit plan.
        FCompilerResultsLog BeforeCompile;
        FKismetEditorUtilities::CompileBlueprint(Blueprint,EBlueprintCompileOptions::SkipGarbageCollection,&BeforeCompile);
        if (BeforeCompile.NumErrors>0)
        { UE_LOG(LogReditusHPBridge,Error,TEXT("BP_UnitBase already has compile errors. Resolve them before installing the bridge.")); return; }
        TMap<FName,UK2Node_CustomEvent*> Events;
        int32 Installed=0,Calculators=0,HealingGuards=0;
        static const FName Names[]={TEXT("SetHP"),TEXT("TakeDamage"),TEXT("Heal"),TEXT("TakePoisonDamage"),TEXT("Regen")};
        for (UEdGraph* Graph:Blueprint->UbergraphPages)
            for (UEdGraphNode* Node:Graph->Nodes)
            {
                if (auto* Call=Cast<UK2Node_CallFunction>(Node))
                {
                    if (Call->FunctionReference.GetMemberName()==TEXT("ResolveHPWrite")) ++Installed;
                    else if (Call->FunctionReference.GetMemberName()==TEXT("GetHPForCalculation")) ++Calculators;
                    else if (Call->FunctionReference.GetMemberName()==TEXT("CanReceiveBattleHealing")) ++HealingGuards;
                }
                if (auto* Event=Cast<UK2Node_CustomEvent>(Node))
                    for (FName Name:Names)
                        if (Event->CustomFunctionName==Name)
                        {
                            if (Events.Contains(Name))
                            { UE_LOG(LogReditusHPBridge,Error,TEXT("Duplicate HP event %s; no graph changed."),*Name.ToString()); return; }
                            Events.Add(Name,Event);
                        }
            }
        if (Installed>0)
        {
            if (Installed==1 && Calculators>=5 && HealingGuards==1)
            {
                UE_LOG(LogReditusHPBridge,Display,TEXT("Rolling HP bridge already exists. No duplicate nodes added."));
            }
            else
            {
                UE_LOG(LogReditusHPBridge,Error,TEXT("An incomplete or modified HP bridge already exists. No graph changed; inspect the existing bridge before continuing."));
            }
            return;
        }
        for (FName Name:Names)
            if (!Events.Contains(Name))
            { UE_LOG(LogReditusHPBridge,Error,TEXT("Required event %s was not found; no graph changed."),*Name.ToString()); return; }

        UK2Node_CustomEvent* SetHP=Events.FindChecked(TEXT("SetHP"));
        UEdGraph* SetGraph=SetHP->GetGraph();
        UK2Node_VariableSet* Assign=nullptr;
        TMap<UK2Node_VariableGet*,FHPRead> Reads;
        TSet<UEdGraphPin*> Seen;
        for (FName Name:Names)
        {
            int32 Before=0;
            for (const auto& Entry:Reads) Before+=Entry.Value.Consumers.Num();
            for (UEdGraphNode* Node:ExecutedNodes(Events.FindChecked(Name)))
            {
                if (Name==TEXT("SetHP"))
                {
                    if (auto* Setter=Cast<UK2Node_VariableSet>(Node))
                        if (Setter->VariableReference.GetMemberName()==TEXT("currentHP"))
                        {
                            if (Assign && Assign!=Setter)
                            { UE_LOG(LogReditusHPBridge,Error,TEXT("Multiple currentHP assignments; no graph changed.")); return; }
                            Assign=Setter;
                        }
                    // Preserve the original Overdrive formula, but base the hit
                    // delta on destination HP so counter steps never award OD.
                    if (auto* Call=Cast<UK2Node_CallFunction>(Node))
                        if (Call->FunctionReference.GetMemberName()==TEXT("SetOverDrivePoints"))
                            CollectHPReads(Call->FindPin(TEXT("overdrivepoints")),Reads,Seen);
                }
                else if (auto* Call=Cast<UK2Node_CallFunction>(Node))
                    if (Call->FunctionReference.GetMemberName()==TEXT("SetHP"))
                        CollectHPReads(Call->FindPin(TEXT("Hp")),Reads,Seen);
            }
            int32 After=0;
            for (const auto& Entry:Reads) After+=Entry.Value.Consumers.Num();
            if (After<=Before)
            { UE_LOG(LogReditusHPBridge,Error,TEXT("Could not verify the arithmetic in %s; no graph changed."),*Name.ToString()); return; }
        }
        // Read the first SetHP execution branch condition separately. It is the
        // original incoming-Hp < old-HP test, ahead of the OD calculation.
        UEdGraphPin* SetThen=SetHP->FindPin(UEdGraphSchema_K2::PN_Then);
        UEdGraphNode* First=SetThen && SetThen->LinkedTo.Num()==1 ? SetThen->LinkedTo[0]->GetOwningNode() : nullptr;
        if (First) CollectHPReads(First->FindPin(TEXT("Condition")),Reads,Seen);
        UEdGraphPin* AssignExec=Assign ? Assign->FindPin(UEdGraphSchema_K2::PN_Execute) : nullptr;
        UEdGraphPin* AssignValue=Assign ? Assign->FindPin(TEXT("currentHP")) : nullptr;
        UEdGraphPin* Requested=SetHP->FindPin(TEXT("Hp"));
        UK2Node_CustomEvent* HealEvent=Events.FindChecked(TEXT("Heal"));
        UEdGraphPin* HealThen=HealEvent->FindPin(UEdGraphSchema_K2::PN_Then);
        if (!AssignExec || !AssignValue || !Requested || AssignValue->LinkedTo.Num()!=1 ||
            AssignValue->LinkedTo[0]!=Requested || AssignExec->LinkedTo.Num()==0 || !First || !First->FindPin(TEXT("Condition")) ||
            !HealThen || HealThen->LinkedTo.Num()!=1)
        { UE_LOG(LogReditusHPBridge,Error,TEXT("SetHP layout differs from the reviewed graph; no graph changed.")); return; }

        const auto* Schema=Cast<UEdGraphSchema_K2>(SetGraph->GetSchema());
        if (!Schema) return;
        bool bSuccess=true;
        {
            FScopedTransaction Transaction(NSLOCTEXT("ReditusHPBridge","Install","Connect Reditus rolling HP"));
            Blueprint->Modify();
            for (UEdGraph* Graph:Blueprint->UbergraphPages)
            {
                Graph->Modify();
                for (UEdGraphNode* Node:Graph->Nodes) Node->Modify();
            }
            for (const auto& Entry:Reads)
            {
                const FHPRead& Read=Entry.Value;
                UEdGraph* Graph=Read.Getter->GetGraph();
                auto* Call=NewCall(Graph,GetHP,Read.Getter->NodePosX,Read.Getter->NodePosY+85);
                bSuccess=ConnectUnit(Schema,Graph,Read.Getter->FindPin(UEdGraphSchema_K2::PN_Self),Call->FindPin(TEXT("Unit"))) && bSuccess;
                for (UEdGraphPin* Consumer:Read.Consumers)
                {
                    Read.Output->BreakLinkTo(Consumer);
                    bSuccess=Schema->TryCreateConnection(Call->FindPin(UEdGraphSchema_K2::PN_ReturnValue),Consumer) && bSuccess;
                }
                // Keep the old getter if a non-arithmetic consumer still uses it.
                bool bStillUsed=false;
                for (UEdGraphPin* Pin:Read.Getter->Pins)
                    if (Pin && Pin->Direction==EGPD_Output && Pin->LinkedTo.Num()>0) bStillUsed=true;
                if (!bStillUsed) FBlueprintEditorUtils::RemoveNode(Blueprint,Read.Getter,true);
            }
            auto* Resolve=NewCall(SetGraph,ResolveHP,Assign->NodePosX-220,Assign->NodePosY-110);
            bSuccess=ConnectUnit(Schema,SetGraph,nullptr,Resolve->FindPin(TEXT("Unit"))) && bSuccess;
            const auto ExecSources=AssignExec->LinkedTo;
            for (UEdGraphPin* Source:ExecSources)
            {
                Source->BreakLinkTo(AssignExec);
                bSuccess=Schema->TryCreateConnection(Source,Resolve->FindPin(UEdGraphSchema_K2::PN_Execute)) && bSuccess;
            }
            bSuccess=Schema->TryCreateConnection(Resolve->FindPin(UEdGraphSchema_K2::PN_Then),AssignExec) && bSuccess;
            AssignValue->BreakAllPinLinks();
            bSuccess=Schema->TryCreateConnection(Requested,Resolve->FindPin(TEXT("RequestedHP"))) && bSuccess;
            bSuccess=Schema->TryCreateConnection(Resolve->FindPin(UEdGraphSchema_K2::PN_ReturnValue),AssignValue) && bSuccess;
            // Missing the rescue deadline is not a resurrection. Gate only the
            // ordinary Heal event; direct initialization/Phoenix Down SetHP
            // calls and the caller's OnSkillUsed completion remain untouched.
            UEdGraph* HealGraph=HealEvent->GetGraph();
            auto* HealingAllowed=NewCall(HealGraph,CanHeal,HealEvent->NodePosX+180,HealEvent->NodePosY+160);
            bSuccess=ConnectUnit(Schema,HealGraph,nullptr,HealingAllowed->FindPin(TEXT("Unit"))) && bSuccess;
            auto* HealingBranch=NewObject<UK2Node_IfThenElse>(HealGraph);
            HealGraph->AddNode(HealingBranch,false,false); HealingBranch->CreateNewGuid(); HealingBranch->AllocateDefaultPins();
            HealingBranch->NodePosX=HealEvent->NodePosX+250; HealingBranch->NodePosY=HealEvent->NodePosY;
            const auto HealNext=HealThen->LinkedTo;
            HealThen->BreakAllPinLinks();
            bSuccess=Schema->TryCreateConnection(HealThen,HealingBranch->FindPin(UEdGraphSchema_K2::PN_Execute)) && bSuccess;
            for (UEdGraphPin* Next:HealNext)
                bSuccess=Schema->TryCreateConnection(HealingBranch->FindPin(UEdGraphSchema_K2::PN_Then),Next) && bSuccess;
            bSuccess=Schema->TryCreateConnection(HealingAllowed->FindPin(UEdGraphSchema_K2::PN_ReturnValue),HealingBranch->FindPin(TEXT("Condition"))) && bSuccess;
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
            FCompilerResultsLog AfterCompile;
            FKismetEditorUtilities::CompileBlueprint(Blueprint,EBlueprintCompileOptions::SkipGarbageCollection,&AfterCompile);
            bSuccess=bSuccess && AfterCompile.NumErrors==0;
        }
        if (!bSuccess)
        {
            GEditor->UndoTransaction();
            FKismetEditorUtilities::CompileBlueprint(Blueprint);
            UE_LOG(LogReditusHPBridge,Error,TEXT("Could not connect/compile the bridge. Installation was undone."));
            return;
        }
        UE_LOG(LogReditusHPBridge,Display,TEXT("Rolling HP bridge connected and compiled. Save BP_UnitBase. Damage, Heal, poison, Regen and the OD calculation now use destination HP."));
    }
}

class FReditusRollingHPEditorBridgeModule final : public IModuleInterface
{
    IConsoleObject* Command=nullptr;
public:
    virtual void StartupModule() override
    {
        Command=IConsoleManager::Get().RegisterConsoleCommand(TEXT("Reditus.InstallRollingHPBridge"),
            TEXT("Connect rolling HP to the reviewed BP_UnitBase graph. Stop Play first, then save BP_UnitBase."),
            FConsoleCommandDelegate::CreateStatic(&InstallBridge),ECVF_Default);
    }
    virtual void ShutdownModule() override
    { if (Command) IConsoleManager::Get().UnregisterConsoleObject(Command); }
};
IMPLEMENT_MODULE(FReditusRollingHPEditorBridgeModule,ReditusRollingHPEditorBridge)
