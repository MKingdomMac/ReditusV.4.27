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
#include "K2Node_Self.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompiler.h"
#include "ScopedTransaction.h"
#include "UObject/EnumProperty.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogReditusAmpInstall, Log, All);

// Unreal's unity build can include this file beside ReditusTableBuffsEditor.cpp.
// Keep these helpers out of its unnamed namespace (which also defines Pin/Compile).
namespace ReditusAmpRefillEditor
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
        UEdGraphPin* Result = Node ? Node->FindPin(Name) : nullptr;
        return Result && !Result->bOrphanedPin ? Result : nullptr;
    }

    bool Pair(UEdGraphPin* Out, UEdGraphPin* In)
    {
        return Out && In && Out->Direction == EGPD_Output && In->Direction == EGPD_Input &&
            Out->GetOwningNode()->GetGraph() == In->GetOwningNode()->GetGraph() &&
            Out->LinkedTo.Num() == 1 && Out->LinkedTo[0] == In &&
            In->LinkedTo.Num() == 1 && In->LinkedTo[0] == Out;
    }

    bool Compile(UBlueprint* BP)
    {
        if (!BP) return false;
        FCompilerResultsLog Log;
        FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::SkipGarbageCollection, &Log);
        return Log.NumErrors == 0 && BP->Status != BS_Error;
    }

    struct FBypass
    {
        UEdGraphPin* Before = nullptr;
        UEdGraphPin* OldIn = nullptr;
        UEdGraphPin* OldOut = nullptr;
        UEdGraphPin* After = nullptr;
        bool Done() const { return Pair(Before, After); }
        bool Legacy() const { return Pair(Before, OldIn) && Pair(OldOut, After); }
        bool Apply() const
        {
            if (Done()) return true;
            if (!Legacy()) return false;
            for (UEdGraphPin* P : {Before, OldIn, OldOut, After}) P->GetOwningNode()->Modify();
            Before->BreakLinkTo(OldIn);
            OldOut->BreakLinkTo(After);
            const auto* Schema = Cast<UEdGraphSchema_K2>(Before->GetOwningNode()->GetGraph()->GetSchema());
            return Schema && Schema->TryCreateConnection(Before, After);
        }
    };

    bool IntPin(const UEdGraphPin* P)
    { return P && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Int; }

    bool ExhaustionGateDone(const UEdGraphPin* P)
    {
        return P && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean && P->LinkedTo.Num() == 0 &&
            (P->DefaultValue.Equals(TEXT("false"), ESearchCase::IgnoreCase) || P->DefaultValue == TEXT("0"));
    }

    bool FixedCap(const UEdGraphPin* Max)
    {
        if (!IntPin(Max) || Max->LinkedTo.Num() != 1) return false;
        const auto* Get = Cast<UK2Node_VariableGet>(Max->LinkedTo[0]->GetOwningNode());
        return Get && Get->VariableReference.GetMemberName() == TEXT("MaxExhaustionPoints");
    }

    struct FUnitPlan
    {
        FBypass Gain, Spend;
        UEdGraphPin* Max = nullptr;
        UEdGraphPin* OldMax = nullptr;
        UK2Node_VariableGet* CapTemplate = nullptr;
        UEdGraphPin* Minus = nullptr;
        UEdGraphPin* MinusOld = nullptr;
        UEdGraphPin* Plus = nullptr;
        UEdGraphPin* PlusOld = nullptr;
        bool bGrantGateInstalled = false;
    };

    bool GrantGateInstalled(UEdGraphPin* Before, UEdGraphPin* After)
    {
        if (!Before || Before->LinkedTo.Num() != 1) return false;
        auto* Branch = Cast<UK2Node_IfThenElse>(Before->LinkedTo[0]->GetOwningNode());
        UEdGraphPin* Condition = Pin(Branch, TEXT("Condition"));
        if (!Branch || !Pair(Before, Pin(Branch, TEXT("execute"))) || !Pair(Pin(Branch, TEXT("then")), After) ||
            !Pin(Branch, TEXT("else")) || Pin(Branch, TEXT("else"))->LinkedTo.Num() || !Condition || Condition->LinkedTo.Num() != 1) return false;
        auto* Check = Cast<UK2Node_CallFunction>(Condition->LinkedTo[0]->GetOwningNode());
        UEdGraphPin* Unit = Pin(Check, TEXT("Unit"));
        return Check && Check->GetTargetFunction() == UReditusTableBuffLibrary::StaticClass()->FindFunctionByName(
            GET_FUNCTION_NAME_CHECKED(UReditusTableBuffLibrary, CanReceiveAmp)) &&
            Unit && Unit->LinkedTo.Num() == 1 && Cast<UK2Node_Self>(Unit->LinkedTo[0]->GetOwningNode());
    }

    bool PlanUnits(UBlueprint* Base, UBlueprint* Player, FUnitPlan& Plan)
    {
        // Reviewed unit exports. Each independent change accepts its original
        // or already-patched form, including mixed installs from the older kit.
        auto* Add = Cast<UK2Node_CustomEvent>(NodeByID(Base, TEXT("89C5795645AF10E230273EA9A330C7EC")));
        auto* Gate = Cast<UK2Node_IfThenElse>(NodeByID(Base, TEXT("A30B44E946D4881F182E289878F566FB")));
        auto* Gain = Cast<UK2Node_VariableSet>(NodeByID(Base, TEXT("862DBE4548C879C2C942AD92609F6653")));
        auto* Clamp = Cast<UK2Node_CallFunction>(NodeByID(Base, TEXT("E1D757E149CFE5B8A31E69A272B18E85")));
        auto* OldCap = Cast<UK2Node_VariableGet>(NodeByID(Base, TEXT("B0AC305D46A4C810490242A39F873C6C")));
        Plan.CapTemplate = Cast<UK2Node_VariableGet>(NodeByID(Base, TEXT("2A03D0FF4A84E99BBB510B9BCC856CF7")));
        auto* Subtract = Cast<UK2Node_CustomEvent>(NodeByID(Player, TEXT("ED2FA3D94570B4E84D79F786488EA5A9")));
        auto* Spend = Cast<UK2Node_VariableSet>(NodeByID(Player, TEXT("5286D625498D20731EAC46A88A585444")));
        auto* ClearBoost = Cast<UK2Node_VariableSet>(NodeByID(Player, TEXT("65E67E1445227261947489AD3C41FA6F")));
        auto* Exhaust = Cast<UK2Node_CallFunction>(NodeByID(Player, TEXT("9783F8284F0389E07C9832B572ED2D0A")));
        Plan.Gain = {Pin(Add, TEXT("then")), Pin(Gate, TEXT("execute")), Pin(Gate, TEXT("else")), Pin(Gain, TEXT("execute"))};
        Plan.bGrantGateInstalled = GrantGateInstalled(Plan.Gain.Before, Plan.Gain.After);
        Plan.Spend = {Pin(Spend, TEXT("then")), Pin(Exhaust, TEXT("execute")), Pin(Exhaust, TEXT("then")), Pin(ClearBoost, TEXT("execute"))};
        Plan.Max = Pin(Clamp, TEXT("Max"));
        Plan.OldMax = Pin(OldCap, TEXT("Current Exhaust Points"));
        Plan.Minus = Pin(NodeByID(Player, TEXT("0D2D5EF44EF7F19DA2F8BC93C282D053")), TEXT("B"));
        Plan.Plus = Pin(NodeByID(Player, TEXT("A85CFEDB466E060F972A79990531AED2")), TEXT("B"));
        Plan.MinusOld = Pin(NodeByID(Player, TEXT("B8816EAB47D1B4EB6FD7DBBE0C49CD7C")), TEXT("IsExhausted"));
        Plan.PlusOld = Pin(NodeByID(Player, TEXT("5D6011A44265B436838EEAADBA735F6E")), TEXT("IsExhausted"));
        const bool CapOK = FixedCap(Plan.Max) || (Plan.CapTemplate &&
            Plan.CapTemplate->VariableReference.GetMemberName() == TEXT("MaxExhaustionPoints") &&
            IntPin(Pin(Plan.CapTemplate, TEXT("MaxExhaustionPoints"))) && Pair(Plan.OldMax, Plan.Max));
        return Add && Add->CustomFunctionName == TEXT("AddAmpPoints") && Subtract &&
            Subtract->CustomFunctionName == TEXT("SubtractAmpPoints") && Gain && Spend && ClearBoost &&
            Gain->VariableReference.GetMemberName() == TEXT("TotalAmpPoints") &&
            Spend->VariableReference.GetMemberName() == TEXT("TotalAmpPoints") &&
            ClearBoost->VariableReference.GetMemberName() == TEXT("IsBoostedState") &&
            Pair(Pin(Subtract, TEXT("then")), Pin(Spend, TEXT("execute"))) &&
            Pair(Pin(Clamp, TEXT("ReturnValue")), Pin(Gain, TEXT("TotalAmpPoints"))) && CapOK &&
            (Plan.bGrantGateInstalled || Plan.Gain.Done() || (Plan.Gain.Legacy() && Pin(Gate, TEXT("then")) &&
                Pin(Gate, TEXT("then"))->LinkedTo.Num() == 0)) &&
            (Plan.Spend.Done() || (Plan.Spend.Legacy() && Exhaust &&
                Exhaust->FunctionReference.GetMemberName() == TEXT("SetExhaustionPoints"))) &&
            (ExhaustionGateDone(Plan.Minus) || Pair(Plan.MinusOld, Plan.Minus)) &&
            (ExhaustionGateDone(Plan.Plus) || Pair(Plan.PlusOld, Plan.Plus));
    }

    bool ApplyUnits(const FUnitPlan& Plan)
    {
        if ((!Plan.bGrantGateInstalled && !Plan.Gain.Apply()) || !Plan.Spend.Apply()) return false;
        if (!Plan.bGrantGateInstalled)
        {
            UEdGraph* Graph = Plan.Gain.Before->GetOwningNode()->GetGraph();
            const auto* Schema = Cast<UEdGraphSchema_K2>(Graph->GetSchema());
            auto* Branch = NewObject<UK2Node_IfThenElse>(Graph, NAME_None, RF_Transactional);
            auto* Check = NewObject<UK2Node_CallFunction>(Graph, NAME_None, RF_Transactional);
            auto* Self = NewObject<UK2Node_Self>(Graph, NAME_None, RF_Transactional);
            for (UEdGraphNode* Node : {static_cast<UEdGraphNode*>(Branch), static_cast<UEdGraphNode*>(Check), static_cast<UEdGraphNode*>(Self)})
            {
                Graph->AddNode(Node, false, false);
                Node->CreateNewGuid();
                Node->NodePosX = Plan.Gain.Before->GetOwningNode()->NodePosX + 280;
                Node->NodePosY = Plan.Gain.Before->GetOwningNode()->NodePosY;
            }
            Check->SetFromFunction(UReditusTableBuffLibrary::StaticClass()->FindFunctionByName(
                GET_FUNCTION_NAME_CHECKED(UReditusTableBuffLibrary, CanReceiveAmp)));
            Branch->AllocateDefaultPins(); Check->AllocateDefaultPins(); Self->AllocateDefaultPins();
            Check->NodePosY += 170; Self->NodePosY += 280;
            Plan.Gain.Before->BreakLinkTo(Plan.Gain.After);
            if (!Schema || !Schema->TryCreateConnection(Plan.Gain.Before, Pin(Branch, TEXT("execute"))) ||
                !Schema->TryCreateConnection(Pin(Branch, TEXT("then")), Plan.Gain.After) ||
                !Schema->TryCreateConnection(Pin(Check, TEXT("ReturnValue")), Pin(Branch, TEXT("Condition"))) ||
                !Schema->TryCreateConnection(Pin(Self, TEXT("self")), Pin(Check, TEXT("Unit")))) return false;
        }
        if (!FixedCap(Plan.Max))
        {
            UEdGraph* Graph = Plan.Max->GetOwningNode()->GetGraph();
            Graph->Modify();
            auto* Get = NewObject<UK2Node_VariableGet>(Graph, NAME_None, RF_Transactional);
            Graph->AddNode(Get, false, false);
            Get->CreateNewGuid();
            Get->VariableReference = Plan.CapTemplate->VariableReference;
            Get->NodePosX = Plan.OldMax->GetOwningNode()->NodePosX;
            Get->NodePosY = Plan.OldMax->GetOwningNode()->NodePosY - 80;
            Get->AllocateDefaultPins();
            Plan.Max->GetOwningNode()->Modify();
            Plan.OldMax->GetOwningNode()->Modify();
            Plan.OldMax->BreakLinkTo(Plan.Max);
            const auto* Schema = Cast<UEdGraphSchema_K2>(Graph->GetSchema());
            if (!Schema || !Schema->TryCreateConnection(Pin(Get, TEXT("MaxExhaustionPoints")), Plan.Max)) return false;
        }
        for (UEdGraphPin* P : {Plan.Minus, Plan.Plus})
        {
            P->GetOwningNode()->Modify();
            for (UEdGraphPin* Old : P->LinkedTo) Old->GetOwningNode()->Modify();
            P->BreakAllPinLinks();
            P->DefaultValue = TEXT("false");
        }
        return true;
    }

    struct FClockPlan
    {
        UActorComponent* Clock = nullptr;
        FStructProperty* Tuning = nullptr;
        FEnumProperty* Timing = nullptr;
        FIntProperty* Gain = nullptr;
        FBoolProperty* First = nullptr;
        FBoolProperty* Skipped = nullptr;
        int64 PersonalCycle = INDEX_NONE;
    };

    bool PlanClocks(UBlueprint* Battle, TArray<FClockPlan>& Plans)
    {
        if (!Battle || !Battle->SimpleConstructionScript) return false;
        for (USCS_Node* Node : Battle->SimpleConstructionScript->GetAllNodes())
        {
            UActorComponent* Clock = Node ? Node->ComponentTemplate : nullptr;
            if (!Clock || Clock->GetClass()->GetPathName() != TEXT("/Script/Chapter1Reditus.ReditusATBComponent")) continue;
            FClockPlan P;
            P.Clock = Clock;
            P.Tuning = FindFProperty<FStructProperty>(Clock->GetClass(), TEXT("Tuning"));
            if (!P.Tuning) return false;
            P.Timing = FindFProperty<FEnumProperty>(P.Tuning->Struct, TEXT("AmpTiming"));
            P.Gain = FindFProperty<FIntProperty>(P.Tuning->Struct, TEXT("AmpGain"));
            P.First = FindFProperty<FBoolProperty>(P.Tuning->Struct, TEXT("bAmpOnFirstCycle"));
            P.Skipped = FindFProperty<FBoolProperty>(P.Tuning->Struct, TEXT("bAmpOnSkippedCycles"));
            if (!P.Timing || !P.Timing->GetEnum() || !P.Gain || !P.First || !P.Skipped) return false;
            P.PersonalCycle = P.Timing->GetEnum()->GetValueByNameString(TEXT("PersonalCycle"));
            if (P.PersonalCycle == INDEX_NONE) return false;
            Plans.Add(P);
        }
        return Plans.Num() == 1;
    }

    void InstallAmpRefillAndBlock()
    {
        if (!GEditor || GEditor->PlayWorld)
        {
            UE_LOG(LogReditusAmpInstall, Error, TEXT("Stop Play before running Reditus.InstallAmpRefillAndBlock."));
            return;
        }
        auto* Base = LoadObject<UBlueprint>(nullptr, TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Units/BP_UnitBase.BP_UnitBase"));
        auto* Player = LoadObject<UBlueprint>(nullptr, TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Units/PlayerUnits/BP_PlayerUnitBase.BP_PlayerUnitBase"));
        auto* Battle = LoadObject<UBlueprint>(nullptr, TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Battle/BP_BattleController.BP_BattleController"));
        auto* Table = LoadObject<UDataTable>(nullptr, TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_Buffs.DT_Buffs"));
        if (!Base || !Player || !Battle || !Table || Table->GetRowStruct() != FReditusTableBuffRow::StaticStruct() ||
            !Compile(Base) || !Compile(Player) || !Compile(Battle))
        {
            UE_LOG(LogReditusAmpInstall, Error, TEXT("Unit/controller Blueprints must compile and DT_Buffs must load. No changes made."));
            return;
        }
        FUnitPlan UnitPlan;
        TArray<FClockPlan> Clocks;
        const bool UnitsOK = PlanUnits(Base, Player, UnitPlan);
        const bool ClockOK = PlanClocks(Battle, Clocks);
        const auto* Existing = Table->FindRow<FReditusTableBuffRow>(TEXT("AmpBlock"), TEXT("Install AMP"), false);
        if (!UnitsOK || !ClockOK || (Existing && Existing->RuntimeEffect != EReditusTableBuffEffect::BlockAmpGain))
        {
            UE_LOG(LogReditusAmpInstall, Error, TEXT("AMP preflight failed: unit wires=%s, ATB template=%s, AmpBlock row=%s. No changes made."),
                UnitsOK ? TEXT("OK") : TEXT("unrecognized"), ClockOK ? TEXT("OK") : TEXT("unrecognized"),
                Existing && Existing->RuntimeEffect != EReditusTableBuffEffect::BlockAmpGain ? TEXT("occupied") : TEXT("OK"));
            return;
        }
        bool bSucceeded = false;
        {
            const FScopedTransaction Transaction(NSLOCTEXT("Reditus", "InstallAmpRefillAndBlock", "Install AMP refill and AMP Block"));
            if (!Transaction.IsOutstanding()) return;
            Base->Modify(); Player->Modify(); Battle->Modify();
            UnitPlan.Gain.Before->GetOwningNode()->GetGraph()->Modify();
            UnitPlan.Spend.Before->GetOwningNode()->GetGraph()->Modify();
            if (ApplyUnits(UnitPlan))
            {
                for (const FClockPlan& P : Clocks)
                {
                    P.Clock->Modify();
                    void* Tuning = P.Tuning->ContainerPtrToValuePtr<void>(P.Clock);
                    P.Timing->GetUnderlyingProperty()->SetIntPropertyValue(P.Timing->ContainerPtrToValuePtr<void>(Tuning), P.PersonalCycle);
                    P.Gain->SetPropertyValue_InContainer(Tuning, 1);
                    P.First->SetPropertyValue_InContainer(Tuning, true);
                    P.Skipped->SetPropertyValue_InContainer(Tuning, true);
                }
                FBlueprintEditorUtils::MarkBlueprintAsModified(Base);
                FBlueprintEditorUtils::MarkBlueprintAsModified(Player);
                FBlueprintEditorUtils::MarkBlueprintAsModified(Battle);
                bSucceeded = Compile(Base) && Compile(Player) && Compile(Battle);
                if (bSucceeded && !Existing)
                {
                    FReditusTableBuffRow Row;
                    Row.DisplayName = NSLOCTEXT("Reditus", "AmpBlockName", "AMP Block");
                    Row.EffectSummary = NSLOCTEXT("Reditus", "AmpBlockEffect", "Prevents AMP gains for the next two personal turns. Existing AMP can still be spent.");
                    Row.RuntimeEffect = EReditusTableBuffEffect::BlockAmpGain;
                    Row.PhysicalDefenseMultiplier = 1.f;
                    Row.OwnerCycles = 2;
                    Row.ApplicationChancePercent = 100.f;
                    Row.Catalogue.Category = TEXT("Harmful");
                    Row.Catalogue.DurationMode = TEXT("OwnerCycles");
                    Row.Catalogue.Duration = 2.f;
                    Row.Catalogue.ReapplyPolicy = TEXT("Refresh");
                    Row.Catalogue.AllowedSources = TEXT("Both");
                    Row.Catalogue.AllowedTargets = TEXT("Both");
                    Row.Catalogue.Symbol = FText::FromString(TEXT("AMP-"));
                    Row.Catalogue.ProposedBehavior = Row.EffectSummary;
                    Row.Catalogue.ImplementationStatus = TEXT("Runtime configured");
                    Table->Modify();
                    Table->AddRow(TEXT("AmpBlock"), Row);
                    Table->PostEditChange();
                    Table->MarkPackageDirty();
                }
            }
        }
        if (!bSucceeded)
        {
            const bool bUndone = GEditor->UndoTransaction(false);
            if (bUndone) { Compile(Base); Compile(Player); Compile(Battle); }
            UE_LOG(LogReditusAmpInstall, Error, TEXT("AMP install failed. Undo: %s."),
                bUndone ? TEXT("succeeded") : TEXT("failed; do not save the modified assets"));
            return;
        }
        UE_LOG(LogReditusAmpInstall, Display, TEXT("AMP refill and AmpBlock installed. Save BP_UnitBase, BP_PlayerUnitBase, BP_BattleController and DT_Buffs. +1 at each personal Cycle Start, including skipped turns; fixed capacity; AmpBlock defaults to 2 awards."));
    }

    FAutoConsoleCommand GInstallAmpRefillAndBlock(
        TEXT("Reditus.InstallAmpRefillAndBlock"),
        TEXT("Install +1 AMP per personal turn, remove exhaustion lockout, and add DT_Buffs > AmpBlock. Stop Play first."),
        FConsoleCommandDelegate::CreateStatic(&InstallAmpRefillAndBlock));
}
