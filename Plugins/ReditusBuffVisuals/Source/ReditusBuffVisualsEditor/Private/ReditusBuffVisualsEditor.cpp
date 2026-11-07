#include "ReditusBuffVisualLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/ActorComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_Self.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompiler.h"
#include "Misc/DefaultValueHelper.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogReditusProtectInstall, Log, All);

namespace
{
    const TCHAR* BlueprintPath = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Skills/Buffs/BP_DefenseIncrease.BP_DefenseIncrease");
    const TCHAR* TablePackage = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_BuffVisuals");
    const TCHAR* TablePath = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_BuffVisuals.DT_BuffVisuals");
    const FName ProtectID(TEXT("Protect"));

    TArray<UEdGraphNode*> ExecChain(UEdGraphNode* Root)
    {
        TArray<UEdGraphNode*> Pending{Root}, Visited;
        while (Pending.Num() > 0)
        {
            UEdGraphNode* Node = Pending.Pop(false);
            if (!Node || Visited.Contains(Node))
            {
                continue;
            }
            Visited.Add(Node);
            for (const UEdGraphPin* Pin : Node->Pins)
            {
                if (Pin && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
                {
                    for (const UEdGraphPin* Linked : Pin->LinkedTo)
                    {
                        if (Linked)
                        {
                            Pending.Add(Linked->GetOwningNode());
                        }
                    }
                }
            }
        }
        return Visited;
    }

    UEdGraphPin* OnlyLink(UEdGraphPin* Pin)
    {
        return Pin && Pin->LinkedTo.Num() == 1 ? Pin->LinkedTo[0] : nullptr;
    }

    UEdGraphPin* Literal(UEdGraphNode* Node, FName Name)
    {
        UEdGraphPin* Pin = Node->FindPin(Name);
        if (!Pin || Pin->Direction != EGPD_Input || Pin->LinkedTo.Num() != 0 || Pin->bOrphanedPin)
        {
            return nullptr;
        }
        // A split transform pin is not equivalent to a single literal.
        return Pin->SubPins.Num() == 0 ? Pin : nullptr;
    }

    bool HasLiteral(UEdGraphNode* Node, FName Name, const TCHAR* Value)
    {
        const UEdGraphPin* Pin = Literal(Node, Name);
        return Pin && Pin->DefaultValue.Equals(Value, ESearchCase::IgnoreCase);
    }

    struct FProtectEdit
    {
        UEdGraph* Graph = nullptr;
        UK2Node_CallFunction* Spawn = nullptr;
        UK2Node_CallFunction* Destroy = nullptr;
        UEdGraphPin* ApplyBefore = nullptr;
        UEdGraphPin* ApplyAfter = nullptr;
        UEdGraphPin* EndBefore = nullptr;
        UEdGraphPin* EndAfter = nullptr;
        UEdGraphPin* AnchorSource = nullptr;
        UEdGraphPin* ParticleInput = nullptr;
        FReditusBuffVisualRow Seed;
    };

    bool InspectLegacyNodes(const TArray<UEdGraphNode*>& ApplyChain,
        const TArray<UEdGraphNode*>& EndChain, FProtectEdit& Edit)
    {
        for (UEdGraphNode* Node : ApplyChain)
        {
            auto* Call = Cast<UK2Node_CallFunction>(Node);
            UFunction* Function = Call ? Call->GetTargetFunction() : nullptr;
            if (Function && Function->GetOuter() == UNiagaraFunctionLibrary::StaticClass() &&
                Function->GetFName() == TEXT("SpawnSystemAttached"))
            {
                if (Edit.Spawn)
                {
                    return false;
                }
                Edit.Spawn = Call;
            }
        }

        if (!Edit.Spawn || EndChain.Contains(Edit.Spawn))
        {
            return false;
        }
        Edit.Graph = Edit.Spawn->GetGraph();
        Edit.ApplyBefore = OnlyLink(Edit.Spawn->FindPin(UEdGraphSchema_K2::PN_Execute));
        Edit.ApplyAfter = OnlyLink(Edit.Spawn->FindPin(UEdGraphSchema_K2::PN_Then));
        Edit.AnchorSource = OnlyLink(Edit.Spawn->FindPin(TEXT("AttachToComponent")));
        UEdGraphPin* SpawnOutput = Edit.Spawn->FindPin(UEdGraphSchema_K2::PN_ReturnValue);
        if (!Edit.ApplyBefore || !Edit.ApplyAfter || !Edit.AnchorSource ||
            !SpawnOutput || SpawnOutput->LinkedTo.Num() != 2)
        {
            return false;
        }

        for (UEdGraphPin* Target : SpawnOutput->LinkedTo)
        {
            if (!Target || Target->Direction != EGPD_Input ||
                Target->GetOwningNode()->GetGraph() != Edit.Graph)
            {
                return false;
            }
            UEdGraphNode* Owner = Target->GetOwningNode();
            if (auto* Set = Cast<UK2Node_VariableSet>(Owner))
            {
                if (Target->PinName == TEXT("particle") &&
                    Set->VariableReference.GetMemberName() == TEXT("particle") &&
                    Set == Edit.ApplyAfter->GetOwningNode())
                {
                    Edit.ParticleInput = Target;
                }
            }
            if (auto* Call = Cast<UK2Node_CallFunction>(Owner))
            {
                const UFunction* Function = Call->GetTargetFunction();
                const FName Name = Call->FunctionReference.GetMemberName();
                if (Target->PinName == UEdGraphSchema_K2::PN_Self && Function &&
                    Function->GetOuter() == UActorComponent::StaticClass() &&
                    (Name == TEXT("K2_DestroyComponent") || Name == TEXT("DestroyComponent")) &&
                    EndChain.Contains(Call) && !ApplyChain.Contains(Call) &&
                    Literal(Call, TEXT("Object")))
                {
                    Edit.Destroy = Call;
                }
            }
        }
        if (!Edit.ParticleInput || !Edit.Destroy)
        {
            return false;
        }

        Edit.EndBefore = OnlyLink(Edit.Destroy->FindPin(UEdGraphSchema_K2::PN_Execute));
        Edit.EndAfter = OnlyLink(Edit.Destroy->FindPin(UEdGraphSchema_K2::PN_Then));
        const UEdGraphPin* Asset = Literal(Edit.Spawn, TEXT("SystemTemplate"));
        const UEdGraphPin* Socket = Literal(Edit.Spawn, TEXT("AttachPointName"));
        const UEdGraphPin* Offset = Literal(Edit.Spawn, TEXT("Location"));
        const UEdGraphPin* Rotation = Literal(Edit.Spawn, TEXT("Rotation"));
        if (!Edit.EndBefore || !Edit.EndAfter || !Asset || !Socket || !Offset || !Rotation ||
            !FDefaultValueHelper::ParseVector(Offset->DefaultValue, Edit.Seed.WhileActive.Offset) ||
            !FDefaultValueHelper::ParseRotator(Rotation->DefaultValue, Edit.Seed.WhileActive.Rotation) ||
            !HasLiteral(Edit.Spawn, TEXT("LocationType"), TEXT("KeepRelativeOffset")) ||
            !HasLiteral(Edit.Spawn, TEXT("bAutoDestroy"), TEXT("false")) ||
            !HasLiteral(Edit.Spawn, TEXT("bAutoActivate"), TEXT("true")) ||
            !HasLiteral(Edit.Spawn, TEXT("PoolingMethod"), TEXT("None")) ||
            !Literal(Edit.Spawn, TEXT("bPreCullCheck")))
        {
            return false;
        }
        Edit.Seed.WhileActive.Niagara = Cast<UNiagaraSystem>(Asset->DefaultObject);
        Edit.Seed.WhileActive.Socket = FName(*Socket->DefaultValue);
        return IsValid(Edit.Seed.WhileActive.Niagara);
    }

    UK2Node_CallFunction* AddCall(UEdGraph* Graph, UFunction* Function, const UEdGraphNode* Old)
    {
        auto* Node = NewObject<UK2Node_CallFunction>(Graph, NAME_None, RF_Transactional);
        Graph->AddNode(Node, false, false);
        Node->CreateNewGuid();
        Node->SetFromFunction(Function);
        Node->AllocateDefaultPins();
        Node->NodePosX = Old->NodePosX;
        Node->NodePosY = Old->NodePosY;
        return Node;
    }

    bool Connect(const UEdGraphSchema_K2* Schema, UEdGraphPin* Out, UEdGraphPin* In)
    {
        return Out && In && Schema->TryCreateConnection(Out, In);
    }

    bool ConnectBuffOwner(const UEdGraphSchema_K2* Schema, UEdGraph* Graph, UK2Node_CallFunction* Call)
    {
        auto* Node = NewObject<UK2Node_Self>(Graph, NAME_None, RF_Transactional);
        Graph->AddNode(Node, false, false);
        Node->CreateNewGuid();
        Node->AllocateDefaultPins();
        Node->NodePosX = Call->NodePosX - 176;
        Node->NodePosY = Call->NodePosY + 136;
        return Connect(Schema, Node->FindPin(UEdGraphSchema_K2::PN_Self), Call->FindPin(TEXT("BuffOwner")));
    }

    void InstallProtect()
    {
        if (!GEditor || GEditor->PlayWorld)
        {
            UE_LOG(LogReditusProtectInstall, Error, TEXT("Stop Play before running Reditus.InstallProtectVisuals."));
            return;
        }

        auto* BP = LoadObject<UBlueprint>(nullptr, BlueprintPath);
        UClass* Library = UReditusBuffVisualLibrary::StaticClass();
        UFunction* StartFunction = Library->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReditusBuffVisualLibrary, StartBuffVisuals));
        UFunction* StopFunction = Library->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReditusBuffVisualLibrary, StopBuffVisuals));
        if (!BP || !BP->ParentClass || !BP->ParentClass->IsChildOf(AActor::StaticClass()) || !StartFunction || !StopFunction)
        {
            UE_LOG(LogReditusProtectInstall, Error, TEXT("Could not find BP_DefenseIncrease or the buff visual functions. No graph was rewired."));
            return;
        }

        FCompilerResultsLog Before;
        FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::SkipGarbageCollection, &Before);
        if (Before.NumErrors != 0)
        {
            UE_LOG(LogReditusProtectInstall, Error, TEXT("BP_DefenseIncrease already has compile errors. No graph was rewired."));
            return;
        }

        TArray<UK2Node_Event*> ApplyEvents, EndEvents;
        TArray<UK2Node_CallFunction*> Starts, Stops;
        for (UEdGraph* Graph : BP->UbergraphPages)
        {
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (auto* Event = Cast<UK2Node_Event>(Node))
                {
                    const FName Name = Event->EventReference.GetMemberName();
                    if (Name == TEXT("ApplyBuff")) ApplyEvents.Add(Event);
                    if (Name == TEXT("OnBuffEnded")) EndEvents.Add(Event);
                }
                if (auto* Call = Cast<UK2Node_CallFunction>(Node))
                {
                    UFunction* Function = Call->GetTargetFunction();
                    if (Function == StartFunction) Starts.Add(Call);
                    if (Function == StopFunction) Stops.Add(Call);
                }
            }
        }
        if (ApplyEvents.Num() != 1 || EndEvents.Num() != 1)
        {
            UE_LOG(LogReditusProtectInstall, Error, TEXT("Expected one Apply Buff event and one On Buff Ended event. No graph was rewired."));
            return;
        }

        const auto ApplyChain = ExecChain(ApplyEvents[0]);
        const auto EndChain = ExecChain(EndEvents[0]);
        UObject* Occupant = LoadObject<UObject>(nullptr, TablePath, nullptr, LOAD_NoWarn | LOAD_Quiet);
        UDataTable* Table = Cast<UDataTable>(Occupant);
        if (Occupant && (!Table || Table->GetRowStruct() != FReditusBuffVisualRow::StaticStruct()))
        {
            UE_LOG(LogReditusProtectInstall, Error, TEXT("The DT_BuffVisuals path is occupied by an incompatible asset. No graph was rewired."));
            return;
        }

        if (Starts.Num() != 0 || Stops.Num() != 0)
        {
            const UEdGraphPin* TablePin = Starts.Num() == 1 ? Literal(Starts[0], TEXT("VisualTable")) : nullptr;
            const UEdGraphPin* IDPin = Starts.Num() == 1 ? Literal(Starts[0], TEXT("BuffID")) : nullptr;
            if (Starts.Num() == 1 && Stops.Num() == 1 && ApplyChain.Contains(Starts[0]) && EndChain.Contains(Stops[0]) &&
                Table && TablePin && TablePin->DefaultObject == Table && IDPin &&
                FName(*IDPin->DefaultValue) == ProtectID && Table->GetRowMap().Contains(ProtectID))
            {
                UE_LOG(LogReditusProtectInstall, Display, TEXT("Protect visuals are already installed. Your table settings were kept."));
            }
            else
            {
                UE_LOG(LogReditusProtectInstall, Error, TEXT("Found an existing or partial Protect visual setup. No graph was rewired."));
            }
            return;
        }

        FProtectEdit Edit;
        if (!InspectLegacyNodes(ApplyChain, EndChain, Edit))
        {
            UE_LOG(LogReditusProtectInstall, Error, TEXT("The Protect particle wiring differs from the reviewed Blueprint. No graph was rewired."));
            return;
        }
        if (Table && !Table->GetRowMap().Contains(ProtectID))
        {
            UE_LOG(LogReditusProtectInstall, Error, TEXT("Existing DT_BuffVisuals has no Protect row. No graph was rewired."));
            return;
        }
        if (!Table && FPackageName::DoesPackageExist(TablePackage))
        {
            UE_LOG(LogReditusProtectInstall, Error, TEXT("A package already occupies the DT_BuffVisuals path. No graph was rewired."));
            return;
        }

        const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(Edit.Graph->GetSchema());
        if (!Schema)
        {
            UE_LOG(LogReditusProtectInstall, Error, TEXT("The Protect graph is not a K2 graph. No graph was rewired."));
            return;
        }

        bool bCreatedTable = false;
        bool bSuccess = false;
        {
            const FScopedTransaction Transaction(NSLOCTEXT("ReditusBuffVisuals", "InstallProtect", "Install Reditus Protect Visuals"));
            if (!Transaction.IsOutstanding())
            {
                UE_LOG(LogReditusProtectInstall, Error, TEXT("Could not start a Blueprint undo transaction. No graph was rewired."));
                return;
            }

            if (!Table)
            {
                UPackage* Package = CreatePackage(nullptr, TablePackage);
                Table = NewObject<UDataTable>(Package, TEXT("DT_BuffVisuals"), RF_Public | RF_Standalone | RF_Transactional);
                Table->RowStruct = FReditusBuffVisualRow::StaticStruct();
                Table->AddRow(ProtectID, Edit.Seed);
                FAssetRegistryModule::AssetCreated(Table);
                Table->MarkPackageDirty();
                bCreatedTable = true;
            }

            BP->Modify();
            Edit.Graph->Modify();
            Edit.Spawn->Modify();
            Edit.Destroy->Modify();
            for (UEdGraphPin* Pin : {Edit.ApplyBefore, Edit.ApplyAfter, Edit.EndBefore,
                Edit.EndAfter, Edit.AnchorSource, Edit.ParticleInput})
            {
                Pin->GetOwningNode()->Modify();
            }

            auto* Start = AddCall(Edit.Graph, StartFunction, Edit.Spawn);
            auto* Stop = AddCall(Edit.Graph, StopFunction, Edit.Destroy);
            UEdGraphPin* TablePin = Start->FindPin(TEXT("VisualTable"));
            UEdGraphPin* IDPin = Start->FindPin(TEXT("BuffID"));
            if (TablePin && IDPin)
            {
                TablePin->DefaultObject = Table;
                IDPin->DefaultValue = ProtectID.ToString();

                Edit.Spawn->BreakAllNodeLinks();
                Edit.Destroy->BreakAllNodeLinks();
                bSuccess =
                    ConnectBuffOwner(Schema, Edit.Graph, Start) &&
                    ConnectBuffOwner(Schema, Edit.Graph, Stop) &&
                    Connect(Schema, Edit.AnchorSource, Start->FindPin(TEXT("AttachToComponent"))) &&
                    Connect(Schema, Edit.ApplyBefore, Start->FindPin(UEdGraphSchema_K2::PN_Execute)) &&
                    Connect(Schema, Start->FindPin(UEdGraphSchema_K2::PN_Then), Edit.ApplyAfter) &&
                    Connect(Schema, Start->FindPin(UEdGraphSchema_K2::PN_ReturnValue), Edit.ParticleInput) &&
                    Connect(Schema, Edit.EndBefore, Stop->FindPin(UEdGraphSchema_K2::PN_Execute)) &&
                    Connect(Schema, Stop->FindPin(UEdGraphSchema_K2::PN_Then), Edit.EndAfter);
                if (bSuccess)
                {
                    FBlueprintEditorUtils::RemoveNode(BP, Edit.Spawn, true);
                    FBlueprintEditorUtils::RemoveNode(BP, Edit.Destroy, true);
                    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
                    FCompilerResultsLog After;
                    FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::SkipGarbageCollection, &After);
                    bSuccess = After.NumErrors == 0 && BP->Status != BS_Error;
                }
            }
        }

        if (!bSuccess)
        {
            const bool bUndone = GEditor->UndoTransaction(false);
            if (bUndone)
            {
                FKismetEditorUtilities::CompileBlueprint(BP);
            }
            UE_LOG(LogReditusProtectInstall, Error, TEXT("Protect install could not connect or compile. Blueprint undo: %s. %s"),
                bUndone ? TEXT("succeeded") : TEXT("failed; do not save the modified Blueprint"),
                bCreatedTable ? TEXT("A new unsaved DT_BuffVisuals may still exist.") : TEXT("The table was not changed."));
            return;
        }

        UE_LOG(LogReditusProtectInstall, Display,
            TEXT("Protect visuals connected and compiled. Save BP_DefenseIncrease and DT_BuffVisuals."));
    }
}

class FReditusBuffVisualsEditorModule final : public IModuleInterface
{
    IConsoleObject* Command = nullptr;

public:
    virtual void StartupModule() override
    {
        Command = IConsoleManager::Get().RegisterConsoleCommand(TEXT("Reditus.InstallProtectVisuals"),
            TEXT("Install the DT_BuffVisuals Protect row and wire BP_DefenseIncrease. Stop Play first."),
            FConsoleCommandDelegate::CreateStatic(&InstallProtect), ECVF_Default);
    }

    virtual void ShutdownModule() override
    {
        if (Command)
        {
            IConsoleManager::Get().UnregisterConsoleObject(Command);
        }
    }
};

IMPLEMENT_MODULE(FReditusBuffVisualsEditorModule, ReditusBuffVisualsEditor)
