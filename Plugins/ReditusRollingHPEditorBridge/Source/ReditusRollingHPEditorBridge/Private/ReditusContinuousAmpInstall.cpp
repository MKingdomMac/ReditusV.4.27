#include "Editor.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "HAL/IConsoleManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompiler.h"
#include "Misc/Guid.h"
#include "ScopedTransaction.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogReditusContinuousAmp, Log, All);

namespace
{
    UEdGraphNode* FindAmpNode(UBlueprint* BP, const TCHAR* NodeId)
    {
        FGuid Guid;
        if (!FGuid::Parse(FString(NodeId), Guid)) return nullptr;
        for (UEdGraph* Graph : BP->UbergraphPages)
            for (UEdGraphNode* Node : Graph->Nodes)
                if (Node && Node->NodeGuid == Guid) return Node;
        return nullptr;
    }

    UEdGraphPin* AmpPin(UEdGraphNode* Node, FName Name)
    {
        UEdGraphPin* Pin = Node ? Node->FindPin(Name) : nullptr;
        return Pin && !Pin->bOrphanedPin ? Pin : nullptr;
    }

    bool AmpPair(UEdGraphPin* Out, UEdGraphPin* In)
    {
        return Out && In && Out->Direction == EGPD_Output && In->Direction == EGPD_Input &&
            Out->GetOwningNode()->GetGraph() == In->GetOwningNode()->GetGraph() &&
            Out->LinkedTo.Num() == 1 && Out->LinkedTo[0] == In &&
            In->LinkedTo.Num() == 1 && In->LinkedTo[0] == Out;
    }

    bool AmpCompile(UBlueprint* BP)
    {
        FCompilerResultsLog Log;
        FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::SkipGarbageCollection, &Log);
        return Log.NumErrors == 0 && BP->Status != BS_Error;
    }

    struct FAmpWirePlan
    {
        UEdGraphPin* Before = nullptr;
        UEdGraphPin* OldIn = nullptr;
        UEdGraphPin* OldOut = nullptr;
        UEdGraphPin* After = nullptr;

        bool IsLegacy() const { return AmpPair(Before, OldIn) && AmpPair(OldOut, After); }
        bool IsDirect() const { return AmpPair(Before, After); }

        bool Bypass() const
        {
            if (!IsLegacy()) return false;
            for (UEdGraphPin* Pin : {Before, OldIn, OldOut, After}) Pin->GetOwningNode()->Modify();
            Before->BreakLinkTo(OldIn);
            OldOut->BreakLinkTo(After);
            return CastChecked<UEdGraphSchema_K2>(Before->GetOwningNode()->GetGraph()->GetSchema())->TryCreateConnection(Before, After);
        }
    };

    bool AmpIsBool(const UEdGraphPin* Pin)
    {
        return Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean;
    }

    bool AmpIsInt(const UEdGraphPin* Pin)
    {
        return Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Int;
    }

    bool AmpLiteralFalse(const UEdGraphPin* Pin)
    {
        return AmpIsBool(Pin) && Pin->Direction == EGPD_Input && Pin->LinkedTo.Num() == 0 &&
            Pin->DefaultValue.Equals(TEXT("false"), ESearchCase::IgnoreCase);
    }

    bool IsAmpCapInput(const UEdGraphPin* Pin)
    {
        if (!AmpIsInt(Pin) || Pin->LinkedTo.Num() != 1) return false;
        UEdGraphPin* Out = Pin->LinkedTo[0];
        const auto* Get = Out ? Cast<UK2Node_VariableGet>(Out->GetOwningNode()) : nullptr;
        return Get && Get->VariableReference.GetMemberName() == TEXT("MaxExhaustionPoints");
    }

    bool InstallAmpCap(UEdGraph* Graph, UEdGraphPin* ClampMax, UEdGraphPin* OldCap, const UK2Node_VariableGet* CapTemplate)
    {
        ClampMax->GetOwningNode()->Modify();
        OldCap->GetOwningNode()->Modify();
        auto* GetCap = NewObject<UK2Node_VariableGet>(Graph, NAME_None, RF_Transactional);
        Graph->AddNode(GetCap, false, false);
        GetCap->CreateNewGuid();
        GetCap->VariableReference = CapTemplate->VariableReference;
        GetCap->NodePosX = OldCap->GetOwningNode()->NodePosX;
        GetCap->NodePosY = OldCap->GetOwningNode()->NodePosY;
        GetCap->AllocateDefaultPins();
        UEdGraphPin* Out = AmpPin(GetCap, TEXT("MaxExhaustionPoints"));
        if (!AmpIsInt(Out)) return false;
        OldCap->BreakLinkTo(ClampMax);
        return CastChecked<UEdGraphSchema_K2>(Graph->GetSchema())->TryCreateConnection(Out, ClampMax);
    }

    void InstallContinuousAmp()
    {
        if (!GEditor || GEditor->PlayWorld)
        {
            UE_LOG(LogReditusContinuousAmp, Error, TEXT("Stop Play before installing continuous AMP."));
            return;
        }

        auto* Base = LoadObject<UBlueprint>(nullptr,
            TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Units/BP_UnitBase.BP_UnitBase"));
        auto* Player = LoadObject<UBlueprint>(nullptr,
            TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Units/PlayerUnits/BP_PlayerUnitBase.BP_PlayerUnitBase"));
        if (!Base || !Player || !AmpCompile(Base) || !AmpCompile(Player))
        {
            UE_LOG(LogReditusContinuousAmp, Error, TEXT("Both unit Blueprints must load and compile before installing AMP. No wires changed."));
            return;
        }

        // The GUIDs identify the exact AMP paths from the project's graph export.
        auto* AddEvent = Cast<UK2Node_CustomEvent>(FindAmpNode(Base, TEXT("89C5795645AF10E230273EA9A330C7EC")));
        auto* GainGate = Cast<UK2Node_IfThenElse>(FindAmpNode(Base, TEXT("A30B44E946D4881F182E289878F566FB")));
        auto* GainExhausted = Cast<UK2Node_VariableGet>(FindAmpNode(Base, TEXT("A9FE574942582CFE0B727A9DD2B8D1A4")));
        auto* SetGain = Cast<UK2Node_VariableSet>(FindAmpNode(Base, TEXT("862DBE4548C879C2C942AD92609F6653")));
        auto* Clamp = Cast<UK2Node_CallFunction>(FindAmpNode(Base, TEXT("E1D757E149CFE5B8A31E69A272B18E85")));
        auto* OldCap = Cast<UK2Node_VariableGet>(FindAmpNode(Base, TEXT("B0AC305D46A4C810490242A39F873C6C")));
        auto* CapTemplate = Cast<UK2Node_VariableGet>(FindAmpNode(Base, TEXT("2A03D0FF4A84E99BBB510B9BCC856CF7")));
        auto* SpendEvent = Cast<UK2Node_CustomEvent>(FindAmpNode(Player, TEXT("ED2FA3D94570B4E84D79F786488EA5A9")));
        auto* Spend = Cast<UK2Node_VariableSet>(FindAmpNode(Player, TEXT("5286D625498D20731EAC46A88A585444")));
        auto* ClearBoost = Cast<UK2Node_VariableSet>(FindAmpNode(Player, TEXT("65E67E1445227261947489AD3C41FA6F")));
        auto* ChargeExhaustion = Cast<UK2Node_CallFunction>(FindAmpNode(Player, TEXT("9783F8284F0389E07C9832B572ED2D0A")));
        auto* MinusGate = Cast<UK2Node_CallFunction>(FindAmpNode(Player, TEXT("0D2D5EF44EF7F19DA2F8BC93C282D053")));
        auto* PlusGate = Cast<UK2Node_CallFunction>(FindAmpNode(Player, TEXT("A85CFEDB466E060F972A79990531AED2")));
        auto* MinusExhausted = Cast<UK2Node_VariableGet>(FindAmpNode(Player, TEXT("B8816EAB47D1B4EB6FD7DBBE0C49CD7C")));
        auto* PlusExhausted = Cast<UK2Node_VariableGet>(FindAmpNode(Player, TEXT("5D6011A44265B436838EEAADBA735F6E")));

        const FAmpWirePlan Gain{AmpPin(AddEvent, UEdGraphSchema_K2::PN_Then),
            AmpPin(GainGate, UEdGraphSchema_K2::PN_Execute), AmpPin(GainGate, TEXT("else")),
            AmpPin(SetGain, UEdGraphSchema_K2::PN_Execute)};
        const FAmpWirePlan Pay{AmpPin(Spend, UEdGraphSchema_K2::PN_Then),
            AmpPin(ChargeExhaustion, UEdGraphSchema_K2::PN_Execute), AmpPin(ChargeExhaustion, UEdGraphSchema_K2::PN_Then),
            AmpPin(ClearBoost, UEdGraphSchema_K2::PN_Execute)};
        UEdGraphPin* GainWhenExhausted = AmpPin(GainGate, UEdGraphSchema_K2::PN_Then);
        UEdGraphPin* Max = AmpPin(Clamp, TEXT("Max"));
        UEdGraphPin* OldMax = AmpPin(OldCap, TEXT("Current Exhaust Points"));
        UEdGraphPin* MinusB = AmpPin(MinusGate, TEXT("B"));
        UEdGraphPin* PlusB = AmpPin(PlusGate, TEXT("B"));
        UEdGraphPin* MinusFrom = AmpPin(MinusExhausted, TEXT("IsExhausted"));
        UEdGraphPin* PlusFrom = AmpPin(PlusExhausted, TEXT("IsExhausted"));

        if (Gain.IsDirect() && Pay.IsDirect() && IsAmpCapInput(Max) && AmpLiteralFalse(MinusB) && AmpLiteralFalse(PlusB))
        {
            UE_LOG(LogReditusContinuousAmp, Display, TEXT("Continuous AMP is already wired."));
            return;
        }

        if (!AddEvent || AddEvent->CustomFunctionName != TEXT("AddAmpPoints") ||
            !SpendEvent || SpendEvent->CustomFunctionName != TEXT("SubtractAmpPoints") ||
            !SetGain || !Spend || !ClearBoost || !ChargeExhaustion || !CapTemplate ||
            !GainWhenExhausted || GainWhenExhausted->LinkedTo.Num() != 0 ||
            !AmpPair(AmpPin(GainExhausted, TEXT("IsExhausted")), AmpPin(GainGate, TEXT("Condition"))) ||
            !AmpPair(AmpPin(SpendEvent, UEdGraphSchema_K2::PN_Then), AmpPin(Spend, UEdGraphSchema_K2::PN_Execute)) ||
            SetGain->VariableReference.GetMemberName() != TEXT("TotalAmpPoints") ||
            Spend->VariableReference.GetMemberName() != TEXT("TotalAmpPoints") ||
            ClearBoost->VariableReference.GetMemberName() != TEXT("IsBoostedState") ||
            ChargeExhaustion->FunctionReference.GetMemberName() != TEXT("SetExhaustionPoints") ||
            CapTemplate->VariableReference.GetMemberName() != TEXT("MaxExhaustionPoints") ||
            !Gain.IsLegacy() || !Pay.IsLegacy() || !AmpPair(OldMax, Max) ||
            !AmpPair(AmpPin(Clamp, UEdGraphSchema_K2::PN_ReturnValue), AmpPin(SetGain, TEXT("TotalAmpPoints"))) ||
            !AmpPair(MinusFrom, MinusB) || !AmpPair(PlusFrom, PlusB) ||
            !AmpIsBool(MinusB) || !AmpIsBool(PlusB) ||
            !AmpIsInt(Max) || !AmpIsInt(AmpPin(CapTemplate, TEXT("MaxExhaustionPoints"))) ||
            !Cast<UEdGraphSchema_K2>(AddEvent->GetGraph()->GetSchema()) ||
            !Cast<UEdGraphSchema_K2>(Spend->GetGraph()->GetSchema()))
        {
            UE_LOG(LogReditusContinuousAmp, Error, TEXT("AMP paths differ from the reviewed unit graphs. No wires changed."));
            return;
        }

        bool bApplied = false;
        {
            const FScopedTransaction Transaction(NSLOCTEXT("Reditus", "InstallContinuousAmp", "Install Continuous AMP"));
            if (!Transaction.IsOutstanding())
            {
                UE_LOG(LogReditusContinuousAmp, Error, TEXT("Could not open an undo transaction. No wires changed."));
                return;
            }
            Base->Modify();
            Player->Modify();
            AddEvent->GetGraph()->Modify();
            Spend->GetGraph()->Modify();
            bApplied = InstallAmpCap(AddEvent->GetGraph(), Max, OldMax, CapTemplate) && Gain.Bypass() && Pay.Bypass();
            if (bApplied)
            {
                for (UEdGraphPin* Pin : {MinusFrom, MinusB, PlusFrom, PlusB})
                    Pin->GetOwningNode()->Modify();
                MinusFrom->BreakLinkTo(MinusB);
                PlusFrom->BreakLinkTo(PlusB);
                MinusB->DefaultValue = TEXT("false");
                PlusB->DefaultValue = TEXT("false");
                GainExhausted->Modify();
                FBlueprintEditorUtils::RemoveNode(Base, GainGate, true);
                FBlueprintEditorUtils::RemoveNode(Base, GainExhausted, true);
                FBlueprintEditorUtils::RemoveNode(Base, OldCap, true);
                FBlueprintEditorUtils::RemoveNode(Player, ChargeExhaustion, true);
                FBlueprintEditorUtils::RemoveNode(Player, MinusExhausted, true);
                FBlueprintEditorUtils::RemoveNode(Player, PlusExhausted, true);
                FBlueprintEditorUtils::MarkBlueprintAsModified(Base);
                FBlueprintEditorUtils::MarkBlueprintAsModified(Player);
                bApplied = AmpCompile(Base) && AmpCompile(Player);
            }
        }

        if (!bApplied)
        {
            const bool bUndone = GEditor->UndoTransaction(false);
            if (bUndone) { AmpCompile(Base); AmpCompile(Player); }
            UE_LOG(LogReditusContinuousAmp, Error, TEXT("Continuous AMP install did not complete. Blueprint undo: %s."),
                bUndone ? TEXT("succeeded") : TEXT("failed; do not save the modified Blueprints"));
            return;
        }
        UE_LOG(LogReditusContinuousAmp, Display,
            TEXT("Continuous AMP installed. Save BP_UnitBase and BP_PlayerUnitBase."));
    }

    FAutoConsoleCommand GInstallContinuousAmpCommand(
        TEXT("Reditus.InstallContinuousAmp"),
        TEXT("Remove AMP exhaustion costs and restore a fixed cap. Stop Play first."),
        FConsoleCommandDelegate::CreateStatic(&InstallContinuousAmp));
}
