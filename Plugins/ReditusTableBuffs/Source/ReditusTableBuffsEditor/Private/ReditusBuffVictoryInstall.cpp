#include "ReditusTableBuffLibrary.h"
#include "Components/ActorComponent.h"
#include "Editor.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Self.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompiler.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogReditusBuffVictoryInstall, Log, All);

// Named namespace: do not collide with other editor files in UE unity builds.
namespace ReditusBuffVictoryEditor
{
    UEdGraphNode* NodeByID(UBlueprint* BP, const TCHAR* ID)
    {
        FGuid Guid;
        if (!BP || !FGuid::Parse(FString(ID), Guid)) return nullptr;
        for (UEdGraph* Graph : BP->UbergraphPages)
            for (UEdGraphNode* Node : Graph->Nodes)
                if (Node && Node->NodeGuid == Guid) return Node;
        return nullptr;
    }

    UEdGraphPin* Pin(UEdGraphNode* Node, FName Name)
    {
        UEdGraphPin* P = Node ? Node->FindPin(Name) : nullptr;
        return P && !P->bOrphanedPin ? P : nullptr;
    }

    bool OutputTo(UEdGraphPin* Out, UEdGraphPin* In)
    {
        return Out && In && Out->Direction == EGPD_Output && In->Direction == EGPD_Input &&
            Out->GetOwningNode()->GetGraph() == In->GetOwningNode()->GetGraph() &&
            Out->LinkedTo.Num() == 1 && Out->LinkedTo[0] == In && In->LinkedTo.Contains(Out);
    }

    bool Pair(UEdGraphPin* Out, UEdGraphPin* In)
    { return OutputTo(Out, In) && In->LinkedTo.Num() == 1; }

    bool Compile(UBlueprint* BP)
    {
        FCompilerResultsLog Log;
        if (!BP) return false;
        FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::SkipGarbageCollection, &Log);
        return Log.NumErrors == 0 && BP->Status != BS_Error;
    }

    struct FExpiryPlan
    {
        UEdGraphPin* Turn = nullptr;
        UEdGraphPin* Time = nullptr;
        UEdGraphPin* EarlyIn = nullptr;
        UEdGraphPin* EarlyOut = nullptr;
        UEdGraphPin* Cleanup = nullptr;
        bool Done() const
        {
            return OutputTo(Turn, Cleanup) && OutputTo(Time, Cleanup) && Cleanup->LinkedTo.Num() == 2 &&
                EarlyIn && EarlyIn->LinkedTo.Num() == 0 && EarlyOut && EarlyOut->LinkedTo.Num() == 0;
        }
        bool Legacy() const
        {
            return OutputTo(Turn, EarlyIn) && OutputTo(Time, EarlyIn) && EarlyIn->LinkedTo.Num() == 2 &&
                Pair(EarlyOut, Cleanup);
        }
    };

    bool PlanExpiry(UBlueprint* BP, FExpiryPlan& P)
    {
        auto* Turn = Cast<UK2Node_IfThenElse>(NodeByID(BP, TEXT("66BDB7E848211A90412DF0B97E615037")));
        auto* Time = Cast<UK2Node_IfThenElse>(NodeByID(BP, TEXT("E465C6FB4332624CECC827AF91FBDAD8")));
        auto* Early = Cast<UK2Node_CallFunction>(NodeByID(BP, TEXT("043CA15444AD15FEA832F9B76A56B53E")));
        auto* Cleanup = Cast<UK2Node_MacroInstance>(NodeByID(BP, TEXT("7AB7A0B046F833E9843A17B03FC94DC6")));
        P.Turn = Pin(Turn, TEXT("then")); P.Time = Pin(Time, TEXT("then"));
        P.EarlyIn = Pin(Early, TEXT("execute")); P.EarlyOut = Pin(Early, TEXT("then"));
        P.Cleanup = Pin(Cleanup, TEXT("exec"));
        return Early && Early->FunctionReference.GetMemberName() == TEXT("K2_DestroyActor") && Cleanup &&
            (P.Done() || P.Legacy());
    }

    bool ApplyExpiry(const FExpiryPlan& P)
    {
        if (P.Done()) return true;
        if (!P.Legacy()) return false;
        for (UEdGraphPin* Item : {P.Turn, P.Time, P.EarlyIn, P.EarlyOut, P.Cleanup}) Item->GetOwningNode()->Modify();
        P.EarlyOut->BreakLinkTo(P.Cleanup);
        P.Turn->BreakLinkTo(P.EarlyIn); P.Time->BreakLinkTo(P.EarlyIn);
        const auto* Schema = Cast<UEdGraphSchema_K2>(P.Cleanup->GetOwningNode()->GetGraph()->GetSchema());
        // Keep the existing OnBuffEnded -> activeBuffs.Remove -> DestroyActor.
        return Schema && Schema->TryCreateConnection(P.Turn, P.Cleanup) && Schema->TryCreateConnection(P.Time, P.Cleanup);
    }

    bool GateInstalled(UEdGraphPin* Before, UEdGraphPin* After, UFunction* Function)
    {
        if (!Before || Before->LinkedTo.Num() != 1) return false;
        auto* Check = Cast<UK2Node_CallFunction>(Before->LinkedTo[0]->GetOwningNode());
        UEdGraphPin* Out = Pin(Check, TEXT("then"));
        if (!Check || Check->GetTargetFunction() != Function || !Pair(Before, Pin(Check, TEXT("execute"))) ||
            !Out || Out->LinkedTo.Num() != 1) return false;
        auto* Branch = Cast<UK2Node_IfThenElse>(Out->LinkedTo[0]->GetOwningNode());
        UEdGraphPin* Input = Pin(Check, TEXT("BattleController"));
        return Branch && Pair(Out, Pin(Branch, TEXT("execute"))) && Pair(Pin(Branch, TEXT("then")), After) &&
            Pair(Pin(Check, TEXT("ReturnValue")), Pin(Branch, TEXT("Condition"))) &&
            Pin(Branch, TEXT("else")) && Pin(Branch, TEXT("else"))->LinkedTo.Num() == 0 &&
            Input && Input->LinkedTo.Num() == 1 && Cast<UK2Node_Self>(Input->LinkedTo[0]->GetOwningNode());
    }

    bool RestoreGate(UEdGraphPin* Before, UEdGraphPin* After, UFunction* Function)
    {
        if (Pair(Before,After)) return true;
        if (!GateInstalled(Before,After,Function)) return false;
        auto* Check=Cast<UK2Node_CallFunction>(Before->LinkedTo[0]->GetOwningNode());
        auto* Branch=Cast<UK2Node_IfThenElse>(Pin(Check,TEXT("then"))->LinkedTo[0]->GetOwningNode());
        auto* Self=Cast<UK2Node_Self>(Pin(Check,TEXT("BattleController"))->LinkedTo[0]->GetOwningNode());
        UEdGraph* Graph=Before->GetOwningNode()->GetGraph();
        const auto* Schema=Cast<UEdGraphSchema_K2>(Graph->GetSchema());
        for (UEdGraphNode* Node : {Before->GetOwningNode(),After->GetOwningNode(),
            static_cast<UEdGraphNode*>(Check),static_cast<UEdGraphNode*>(Branch),static_cast<UEdGraphNode*>(Self)})
            if (Node) Node->Modify();
        Before->BreakLinkTo(Pin(Check,TEXT("execute")));
        Pin(Branch,TEXT("then"))->BreakLinkTo(After);
        if (!Schema || !Schema->TryCreateConnection(Before,After)) return false;
        // Disconnect the old gate completely. These pure/orphaned nodes are
        // removed only when their exact installed wiring was recognized.
        Check->BreakAllNodeLinks(); Branch->BreakAllNodeLinks();
        Graph->RemoveNode(Check); Graph->RemoveNode(Branch);
        if (Self && Pin(Self,TEXT("self")) && Pin(Self,TEXT("self"))->LinkedTo.Num()==0)
            Graph->RemoveNode(Self);
        return Pair(Before,After);
    }

    bool DurationHookInstalled(UEdGraphPin* Before, UEdGraphPin* After, UFunction* Function)
    {
        if (!Before || Before->LinkedTo.Num()!=1) return false;
        auto* Call=Cast<UK2Node_CallFunction>(Before->LinkedTo[0]->GetOwningNode());
        auto* Input=Pin(Call,TEXT("Buff"));
        return Call && Call->GetTargetFunction()==Function && Pair(Before,Pin(Call,TEXT("execute"))) &&
            Pair(Pin(Call,TEXT("then")),After) && Input && Input->LinkedTo.Num()==1 &&
            Cast<UK2Node_Self>(Input->LinkedTo[0]->GetOwningNode());
    }

    bool ApplyDurationHook(UEdGraphPin* Before, UEdGraphPin* After, UFunction* Function)
    {
        if (DurationHookInstalled(Before,After,Function)) return true;
        if (!Pair(Before,After)) return false;
        UEdGraph* Graph=Before->GetOwningNode()->GetGraph();
        auto* Call=NewObject<UK2Node_CallFunction>(Graph,NAME_None,RF_Transactional);
        auto* Self=NewObject<UK2Node_Self>(Graph,NAME_None,RF_Transactional);
        for (UEdGraphNode* Node : {static_cast<UEdGraphNode*>(Call),static_cast<UEdGraphNode*>(Self)})
        {
            Graph->AddNode(Node,false,false); Node->CreateNewGuid();
            Node->NodePosX=Before->GetOwningNode()->NodePosX+250;
            Node->NodePosY=Before->GetOwningNode()->NodePosY;
        }
        Self->NodePosY+=150;
        Call->SetFromFunction(Function); Call->AllocateDefaultPins(); Self->AllocateDefaultPins();
        Before->GetOwningNode()->Modify(); After->GetOwningNode()->Modify();
        Before->BreakLinkTo(After);
        const auto* Schema=Cast<UEdGraphSchema_K2>(Graph->GetSchema());
        return Schema && Schema->TryCreateConnection(Before,Pin(Call,TEXT("execute"))) &&
            Schema->TryCreateConnection(Pin(Call,TEXT("then")),After) &&
            Schema->TryCreateConnection(Pin(Self,TEXT("self")),Pin(Call,TEXT("Buff")));
    }

    void RestoreVictoryFlow()
    {
        if (!GEditor || GEditor->PlayWorld)
        {
            UE_LOG(LogReditusBuffVictoryInstall,Error,TEXT("Stop Play before Reditus.RestoreVictoryFlow."));
            return;
        }
        auto* Battle=LoadObject<UBlueprint>(nullptr,TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Battle/BP_BattleController.BP_BattleController"));
        if (!Battle || !Compile(Battle))
        {
            UE_LOG(LogReditusBuffVictoryInstall,Error,TEXT("BP_BattleController must load and compile before restoration. No graph changes made."));
            return;
        }
        auto* PresenterClass=FindObject<UClass>(nullptr,TEXT("/Script/Chapter1Reditus.ReditusBattleHUDPresenterComponent"));
        UFunction* Gate=PresenterClass ? PresenterClass->FindFunctionByName(TEXT("TryBeginRecordedBattleResult")) : nullptr;
        auto* Event=Cast<UK2Node_CustomEvent>(NodeByID(Battle,TEXT("2B2C742E4B072AAB88118FB367418FD9")));
        auto* First=Cast<UK2Node_CallFunction>(NodeByID(Battle,TEXT("C11A31E0437DC5B6577671A01E345DBA")));
        auto* Before=Pin(Event,TEXT("then")); auto* After=Pin(First,TEXT("execute"));
        if (!Gate || !Event || Event->CustomFunctionName!=TEXT("OnBattleOver") ||
            !First || First->FunctionReference.GetMemberName()!=TEXT("FadeOut") ||
            (!Pair(Before,After) && !GateInstalled(Before,After,Gate)))
        {
            UE_LOG(LogReditusBuffVictoryInstall,Error,TEXT("Victory restore preflight could not recognize the original or installed event path. No graph changes made; the rebuilt native gate is already a pass-through."));
            return;
        }
        bool Success=false;
        {
            const FScopedTransaction Transaction(NSLOCTEXT("Reditus","RestoreVictoryFlow","Restore original victory flow"));
            if (!Transaction.IsOutstanding()) return;
            Battle->Modify(); Before->GetOwningNode()->GetGraph()->Modify();
            if (RestoreGate(Before,After,Gate))
            {
                if (auto* Enabled=FindFProperty<FBoolProperty>(PresenterClass,TEXT("bCompleteRecordedVictory")))
                    if (Battle->SimpleConstructionScript)
                        for (USCS_Node* Node : Battle->SimpleConstructionScript->GetAllNodes())
                            if (Node && Node->ComponentTemplate && Node->ComponentTemplate->IsA(PresenterClass))
                            {
                                Node->ComponentTemplate->Modify();
                                Enabled->SetPropertyValue_InContainer(Node->ComponentTemplate,false);
                            }
                FBlueprintEditorUtils::MarkBlueprintAsModified(Battle);
                Success=Compile(Battle);
            }
        }
        if (!Success)
        {
            const bool Undone=GEditor->UndoTransaction(false);
            if (Undone) Compile(Battle);
            UE_LOG(LogReditusBuffVictoryInstall,Error,TEXT("Victory restore failed. Undo %s."),Undone ? TEXT("succeeded") : TEXT("failed; do not save modified assets"));
            return;
        }
        UE_LOG(LogReditusBuffVictoryInstall,Display,TEXT("Restored original OnBattleOver execution. Save BP_BattleController and start a fresh battle. Victory cleanup now waits for Explore and does not stop ATB or start rewards."));
    }

    void InstallBuffExpiryFixes()
    {
        if (!GEditor || GEditor->PlayWorld)
        {
            UE_LOG(LogReditusBuffVictoryInstall,Error,TEXT("Stop Play before Reditus.InstallBuffExpiryFixes."));
            return;
        }
        auto* Buff=LoadObject<UBlueprint>(nullptr,TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Skills/Buffs/BP_BuffBase.BP_BuffBase"));
        if (!Buff || !Compile(Buff))
        {
            UE_LOG(LogReditusBuffVictoryInstall,Error,TEXT("BP_BuffBase must load and compile before installation. No graph changes made."));
            return;
        }
        UFunction* Function=UReditusTableBuffLibrary::StaticClass()->FindFunctionByName(
            GET_FUNCTION_NAME_CHECKED(UReditusTableBuffLibrary,SynchronizeLegacyBuffDuration));
        auto* Turns=Cast<UK2Node_VariableSet>(NodeByID(Buff,TEXT("CED50682452A1D66E048828A6A526A06")));
        auto* Print=Cast<UK2Node_CallFunction>(NodeByID(Buff,TEXT("2AD2FEAD49BB80CE963DCEBED9CF6976")));
        auto* Before=Pin(Turns,TEXT("then")); auto* After=Pin(Print,TEXT("execute"));
        FExpiryPlan Expiry;
        if (!Function || !Turns || Turns->VariableReference.GetMemberName()!=TEXT("turnLeft") ||
            !PlanExpiry(Buff,Expiry) || (!Pair(Before,After) && !DurationHookInstalled(Before,After,Function)))
        {
            UE_LOG(LogReditusBuffVictoryInstall,Error,TEXT("Buff expiry preflight failed. No graph changes made."));
            return;
        }
        bool Success=false;
        {
            const FScopedTransaction Transaction(NSLOCTEXT("Reditus","InstallBuffExpiryFixes","Fix buff expiry"));
            if (!Transaction.IsOutstanding()) return;
            Buff->Modify(); Before->GetOwningNode()->GetGraph()->Modify();
            if (ApplyExpiry(Expiry) && ApplyDurationHook(Before,After,Function))
            {
                FBlueprintEditorUtils::MarkBlueprintAsModified(Buff);
                Success=Compile(Buff);
            }
        }
        if (!Success)
        {
            const bool Undone=GEditor->UndoTransaction(false);
            if (Undone) Compile(Buff);
            UE_LOG(LogReditusBuffVictoryInstall,Error,TEXT("Buff expiry install failed. Undo %s."),Undone ? TEXT("succeeded") : TEXT("failed; do not save modified assets"));
            return;
        }
        UE_LOG(LogReditusBuffVictoryInstall,Display,TEXT("Buff expiry hooks installed. Save BP_BuffBase. No victory flow was added."));
    }

    void InstallBuffAndVictoryFixes()
    {
        // Backward-compatible command name: it can no longer reinstall the
        // withdrawn victory gate or opt into native victory continuation.
        InstallBuffExpiryFixes();
        RestoreVictoryFlow();
    }

    FAutoConsoleCommand GRestoreVictoryFlow(
        TEXT("Reditus.RestoreVictoryFlow"),
        TEXT("Remove the added result gate and restore the original BP victory flow. Stop Play first."),
        FConsoleCommandDelegate::CreateStatic(&RestoreVictoryFlow));
    FAutoConsoleCommand GInstallBuffExpiryFixes(
        TEXT("Reditus.InstallBuffExpiryFixes"),
        TEXT("Install legacy buff cleanup/timer hooks only. Stop Play first."),
        FConsoleCommandDelegate::CreateStatic(&InstallBuffExpiryFixes));
    FAutoConsoleCommand GInstallBuffAndVictoryFixes(
        TEXT("Reditus.InstallBuffAndVictoryFixes"),
        TEXT("Install buff expiry hooks and RESTORE original victory flow. Stop Play first."),
        FConsoleCommandDelegate::CreateStatic(&InstallBuffAndVictoryFixes));
}
