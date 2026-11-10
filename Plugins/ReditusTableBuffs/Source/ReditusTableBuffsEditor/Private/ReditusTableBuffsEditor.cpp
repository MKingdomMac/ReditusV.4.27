#include "ReditusTableBuffLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetToolsModule.h"
#include "Components/ActorComponent.h"
#include "DataTableEditorUtils.h"
#include "Editor.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "IAssetTools.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ClassDynamicCast.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_GetArrayItem.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Knot.h"
#include "K2Node_MakeArray.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_Self.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompiler.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogReditusGuardUpInstall, Log, All);

namespace
{
    const FName TestSkillID(TEXT("GuardUp_Test"));
    const FName BuffID(TEXT("Protect"));
    const FName SourceSkillID(TEXT("ProtectSelfBuff"));
    const TCHAR* LegacySkillPath = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Skills/BuffSkills/BP_ProtectSelfBuff.BP_ProtectSelfBuff");
    const TCHAR* BattlePath = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Battle/BP_BattleController.BP_BattleController");
    const TCHAR* SkillsPath = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_Skills.DT_Skills");
    const TCHAR* VisualPath = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_BuffVisuals.DT_BuffVisuals");
    const TCHAR* BuffPackage = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_Buffs");
    const TCHAR* BuffPath = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_Buffs.DT_Buffs");
    const TCHAR* LegacyBuffBasePath = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Skills/Buffs/BP_BuffBase.BP_BuffBase_C");
    const TCHAR* LegacyBlindPath = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Skills/Buffs/BP_Blind.BP_Blind_C");
    const TCHAR* LegacyPoisonPath = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Skills/Buffs/BP_Poison.BP_Poison_C");
    const TCHAR* SkillFolder = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Skills/BuffSkills");
    const TCHAR* NewSkillName = TEXT("BP_GuardUpTableSkill");
    const TCHAR* NewSkillPackage = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Skills/BuffSkills/BP_GuardUpTableSkill");
    const TCHAR* NewSkillPath = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Skills/BuffSkills/BP_GuardUpTableSkill.BP_GuardUpTableSkill");

    UEdGraphPin* Pin(UEdGraphNode* Node, FName Name)
    {
        UEdGraphPin* Result = Node ? Node->FindPin(Name) : nullptr;
        return Result && !Result->bOrphanedPin ? Result : nullptr;
    }

    UEdGraphPin* OneLink(UEdGraphPin* P)
    {
        return P && P->LinkedTo.Num() == 1 ? P->LinkedTo[0] : nullptr;
    }

    // Reroutes have no gameplay behavior. Inspect the original value but
    // preserve the immediate link at the destination when inserting a node.
    UEdGraphPin* TraceSource(UEdGraphPin* Input)
    {
        TSet<UEdGraphNode*> Visited;
        UEdGraphPin* Output = OneLink(Input);
        while (Output)
        {
            auto* Knot = Cast<UK2Node_Knot>(Output->GetOwningNode());
            if (!Knot) return Output->Direction == EGPD_Output ? Output : nullptr;
            if (Visited.Contains(Knot)) return nullptr;
            Visited.Add(Knot);
            Output = OneLink(Pin(Knot, TEXT("InputPin")));
        }
        return nullptr;
    }

    void FindConsumers(UEdGraphPin* Output, TArray<UEdGraphPin*>& Consumers)
    {
        if (!Output) return;
        TArray<UEdGraphPin*> Pending;
        TSet<UEdGraphPin*> Seen;
        Pending.Add(Output);
        while (Pending.Num())
        {
            UEdGraphPin* Current = Pending.Pop(false);
            if (!Current || Seen.Contains(Current)) continue;
            Seen.Add(Current);
            for (UEdGraphPin* Input : Current->LinkedTo)
            {
                if (!Input || Input->Direction != EGPD_Input || Input->bOrphanedPin) continue;
                auto* Knot = Cast<UK2Node_Knot>(Input->GetOwningNode());
                if (Knot)
                {
                    Pending.Add(Pin(Knot, TEXT("OutputPin")));
                }
                else
                {
                    Consumers.AddUnique(Input);
                }
            }
        }
    }

    bool Link(UEdGraph* Graph, UEdGraphPin* From, UEdGraphPin* To)
    {
        const auto* Schema = Cast<UEdGraphSchema_K2>(Graph->GetSchema());
        return Schema && From && To && Schema->TryCreateConnection(From, To);
    }

    bool Compile(UBlueprint* BP)
    {
        FCompilerResultsLog Log;
        FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::SkipGarbageCollection, &Log);
        return Log.NumErrors == 0 && BP->Status != BS_Error;
    }

    UK2Node_CallFunction* AddCall(UEdGraph* Graph, UFunction* Function, int32 X, int32 Y)
    {
        auto* Node = NewObject<UK2Node_CallFunction>(Graph, NAME_None, RF_Transactional);
        Graph->AddNode(Node, false, false);
        Node->CreateNewGuid();
        Node->SetFromFunction(Function);
        Node->AllocateDefaultPins();
        Node->NodePosX = X;
        Node->NodePosY = Y;
        return Node;
    }

    const FName AggroFocusID(TEXT("AggroFocus"));
    const TCHAR* EnemyTargetingPath = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Units/EnemyUnits/BP_EnemyUnitBase.BP_EnemyUnitBase");
    const TCHAR* SkillTargetingPath = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Skills/BP_BattleTechsBase.BP_BattleTechsBase");
    const TCHAR* AggroFocusPackage = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Skills/Buffs/BP_AggroFocusBuff");
    const TCHAR* AggroFocusPath = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Skills/Buffs/BP_AggroFocusBuff.BP_AggroFocusBuff");

    struct FAggroIndexSite
    {
        UEdGraphPin* Index = nullptr;
        UEdGraphPin* Previous = nullptr;
        UEdGraphPin* Controller = nullptr;
        bool bInstalled = false;
    };

    bool IsVariableOutput(const UEdGraphPin* Output, FName Name)
    {
        const auto* Get = Output ? Cast<UK2Node_VariableGet>(Output->GetOwningNode()) : nullptr;
        return Get && Get->VariableReference.GetMemberName() == Name && Output->PinName == Name;
    }

    bool FindAggroIndexSites(UBlueprint* BP, UFunction* Resolver, TArray<FAggroIndexSite>& Sites)
    {
        if (!BP || !Resolver) return false;
        const FName SelectorName(TEXT("Target Random Player Unit Based On Aggro"));
        UEdGraph* Graph = nullptr;
        for (auto* Candidate : BP->FunctionGraphs)
            if (Candidate && Candidate->GetFName() == SelectorName)
            {
                if (Graph) return false;
                Graph = Candidate;
            }
        if (!Graph) return false;

        int32 ReturnCount = 0;
        for (auto* Node : Graph->Nodes)
        {
            auto* Return = Cast<UK2Node_FunctionResult>(Node);
            if (!Return) continue;
            ++ReturnCount;
            auto* ArrayOutput = TraceSource(Pin(Return, TEXT("Output")));
            auto* ArrayItem = ArrayOutput ? Cast<UK2Node_GetArrayItem>(ArrayOutput->GetOwningNode()) : nullptr;
            auto* Index = Pin(ArrayItem, TEXT("Dimension 1"));
            auto* Previous = OneLink(Index);
            auto* Source = TraceSource(Index);
            auto* Players = TraceSource(Pin(ArrayItem, TEXT("Array")));
            auto* CurrentBattle = IsVariableOutput(Players, TEXT("playerUnits"))
                ? TraceSource(Pin(Players->GetOwningNode(), UEdGraphSchema_K2::PN_Self)) : nullptr;
            auto* Controller = IsVariableOutput(CurrentBattle, TEXT("currentBattle"))
                ? TraceSource(Pin(CurrentBattle->GetOwningNode(), UEdGraphSchema_K2::PN_Self)) : nullptr;
            if (!Index || !Previous || !Source || Index->PinType.PinCategory != UEdGraphSchema_K2::PC_Int ||
                !IsVariableOutput(Controller, TEXT("battleController"))) return false;

            auto* Existing = Cast<UK2Node_CallFunction>(Source->GetOwningNode());
            const bool bInstalled = Existing && Existing->FunctionReference.GetMemberName() == Resolver->GetFName();
            if (bInstalled)
            {
                auto* Context = TraceSource(Pin(Existing, TEXT("Selector")));
                if (Source->PinName != UEdGraphSchema_K2::PN_ReturnValue ||
                    !TraceSource(Pin(Existing, TEXT("LegacyIndex"))) ||
                    TraceSource(Pin(Existing, TEXT("BattleController"))) != Controller ||
                    !Context || !Context->GetOwningNode()->IsA<UK2Node_Self>()) return false;
            }
            bool bAlreadyFound = false;
            for (const auto& Site : Sites) bAlreadyFound |= Site.Index == Index;
            if (!bAlreadyFound) Sites.Add(FAggroIndexSite{Index, Previous, Controller, bInstalled});
        }
        // The exported selectors have separate weighted and zero-aggro returns.
        return ReturnCount >= 2 && Sites.Num() >= 2;
    }

    bool WireAggroIndexSites(UBlueprint* BP, UFunction* Resolver, const TArray<FAggroIndexSite>& Sites, bool& bChanged)
    {
        bChanged = false;
        for (const auto& Site : Sites)
        {
            if (Site.bInstalled) continue;
            auto* Item = Site.Index->GetOwningNode();
            auto* Graph = Item->GetGraph();
            BP->Modify();
            Graph->Modify();
            Item->Modify();
            Site.Previous->GetOwningNode()->Modify();
            Site.Controller->GetOwningNode()->Modify();
            auto* Call = AddCall(Graph, Resolver, Item->NodePosX - 240, Item->NodePosY + 150);
            auto* Self = NewObject<UK2Node_Self>(Graph, NAME_None, RF_Transactional);
            Graph->AddNode(Self, false, false);
            Self->CreateNewGuid();
            Self->AllocateDefaultPins();
            Self->NodePosX = Call->NodePosX - 160;
            Self->NodePosY = Call->NodePosY + 86;
            if (!Link(Graph, Site.Controller, Pin(Call, TEXT("BattleController"))) ||
                !Link(Graph, Pin(Self, UEdGraphSchema_K2::PN_Self), Pin(Call, TEXT("Selector"))) ||
                !Link(Graph, Site.Previous, Pin(Call, TEXT("LegacyIndex")))) return false;
            Site.Previous->BreakLinkTo(Site.Index);
            if (!Link(Graph, Pin(Call, UEdGraphSchema_K2::PN_ReturnValue), Site.Index)) return false;
            bChanged = true;
        }
        if (bChanged) FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
        return true;
    }

    void InstallAggroFocusBuff()
    {
        if (!GEditor || GEditor->PlayWorld)
        {
            UE_LOG(LogReditusGuardUpInstall, Error, TEXT("Stop Play before running Reditus.InstallAggroFocusBuff."));
            return;
        }
        auto* Table = LoadObject<UDataTable>(nullptr, BuffPath);
        auto* Enemy = LoadObject<UBlueprint>(nullptr, EnemyTargetingPath);
        auto* Skills = LoadObject<UBlueprint>(nullptr, SkillTargetingPath);
        auto* Library = UReditusTableBuffLibrary::StaticClass();
        auto* Resolver = Library->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReditusTableBuffLibrary, ResolveAggroFocusIndex));
        auto* Parent = FindObject<UClass>(nullptr, TEXT("/Script/ReditusTableBuffs.ReditusTableBuffState"));
        TArray<FAggroIndexSite> EnemySites, SkillSites;
        if (!Table || Table->GetRowStruct() != FReditusTableBuffRow::StaticStruct() || !Parent || !Resolver)
        {
            UE_LOG(LogReditusGuardUpInstall, Error, TEXT("Aggro Focus requires the existing DT_Buffs and the rebuilt Table Buffs plugin. No assets were changed."));
            return;
        }
        const bool bEnemyOK = FindAggroIndexSites(Enemy, Resolver, EnemySites);
        const bool bSkillsOK = FindAggroIndexSites(Skills, Resolver, SkillSites);
        if (!bEnemyOK || !bSkillsOK)
        {
            UE_LOG(LogReditusGuardUpInstall, Error,
                TEXT("Could not recognize the player-array aggro returns: BP_EnemyUnitBase=%s, BP_BattleTechsBase=%s. No assets were changed."),
                bEnemyOK ? TEXT("OK") : TEXT("unrecognized"), bSkillsOK ? TEXT("OK") : TEXT("unrecognized"));
            return;
        }

        UObject* Occupant = LoadObject<UObject>(nullptr, AggroFocusPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
        auto* BuffBP = Cast<UBlueprint>(Occupant);
        if ((Occupant && (!BuffBP || BuffBP->ParentClass != Parent || !BuffBP->GeneratedClass)) ||
            (!Occupant && FPackageName::DoesPackageExist(AggroFocusPackage)))
        {
            UE_LOG(LogReditusGuardUpInstall, Error, TEXT("The BP_AggroFocusBuff asset path is already occupied. No assets were changed."));
            return;
        }
        const auto* Existing = Table->FindRow<FReditusTableBuffRow>(AggroFocusID, TEXT("Install Aggro Focus"), false);
        if (Existing && (Existing->RuntimeEffect != EReditusTableBuffEffect::AggroFocus ||
            (!Existing->AggroFocusBuffClass.IsNull() &&
                (!BuffBP || Existing->AggroFocusBuffClass.LoadSynchronous() != BuffBP->GeneratedClass))))
        {
            UE_LOG(LogReditusGuardUpInstall, Error, TEXT("An unrelated AggroFocus row already exists. No assets were changed."));
            return;
        }
        if (!BuffBP)
        {
            auto* Package = CreatePackage(AggroFocusPackage);
            BuffBP = FKismetEditorUtilities::CreateBlueprint(Parent, Package, FName(TEXT("BP_AggroFocusBuff")),
                BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), FName(TEXT("ReditusAggroFocus")));
            if (!BuffBP || !Compile(BuffBP) || !BuffBP->GeneratedClass)
            {
                UE_LOG(LogReditusGuardUpInstall, Error, TEXT("Could not compile BP_AggroFocusBuff. Targeting graphs and DT_Buffs were not changed."));
                return;
            }
            FAssetRegistryModule::AssetCreated(BuffBP);
            BuffBP->MarkPackageDirty();
        }

        bool bSucceeded = false;
        {
            const FScopedTransaction Transaction(NSLOCTEXT("ReditusBuffs", "InstallAggroFocus", "Install Aggro Focus buff"));
            if (!Transaction.IsOutstanding())
            {
                UE_LOG(LogReditusGuardUpInstall, Error, TEXT("Could not open undo transaction. Targeting graphs and DT_Buffs were not changed."));
                return;
            }
            bool bEnemyChanged = false, bSkillsChanged = false;
            if (WireAggroIndexSites(Enemy, Resolver, EnemySites, bEnemyChanged) &&
                WireAggroIndexSites(Skills, Resolver, SkillSites, bSkillsChanged) &&
                (!bEnemyChanged || Compile(Enemy)) && (!bSkillsChanged || Compile(Skills)))
            {
                if (!Existing || Existing->AggroFocusBuffClass.IsNull())
                {
                    FReditusTableBuffRow Row;
                    if (Existing) Row = *Existing;
                    else
                    {
                        Row.DisplayName = NSLOCTEXT("ReditusBuffs", "AggroFocus", "Aggro Focus");
                        Row.EffectSummary = NSLOCTEXT("ReditusBuffs", "AggroFocusSummary", "Draws ordinary single-target enemy attacks while active.");
                        Row.RuntimeEffect = EReditusTableBuffEffect::AggroFocus;
                        Row.OwnerCycles = 2;
                        Row.Catalogue.Category = TEXT("Beneficial");
                        Row.Catalogue.AllowedSources = TEXT("Player");
                        Row.Catalogue.AllowedTargets = TEXT("Player");
                        Row.Catalogue.ImplementationStatus = TEXT("Runtime configured");
                    }
                    Row.AggroFocusBuffClass = TSoftClassPtr<UActorComponent>(FSoftObjectPath(BuffBP->GeneratedClass->GetPathName()));
                    Table->Modify();
                    Table->AddRow(AggroFocusID, Row);
                    Table->PostEditChange();
                    Table->MarkPackageDirty();
                }
                bSucceeded = true;
            }
        }
        if (!bSucceeded)
        {
            const bool bUndone = GEditor->UndoTransaction(false);
            if (bUndone) { Compile(Enemy); Compile(Skills); }
            UE_LOG(LogReditusGuardUpInstall, Error, TEXT("Could not wire or compile the aggro selectors. Undo: %s. The new unsaved buff Blueprint may remain."),
                bUndone ? TEXT("succeeded") : TEXT("failed; do not save the targeting graphs"));
            return;
        }
        UE_LOG(LogReditusGuardUpInstall, Display, TEXT("Aggro Focus installed. Save DT_Buffs, BP_AggroFocusBuff, BP_EnemyUnitBase, and BP_BattleTechsBase. Buff ID: AggroFocus."));
    }

    FAutoConsoleCommand GInstallAggroFocus(
        TEXT("Reditus.InstallAggroFocusBuff"),
        TEXT("Create the Aggro Focus buff Blueprint and row and connect both normal enemy aggro selectors. Stop Play first."),
        FConsoleCommandDelegate::CreateStatic(&InstallAggroFocusBuff));

    struct FSkillSite
    {
        UK2Node_CallFunction* Apply = nullptr;
        UEdGraphPin* Start = nullptr;
        UEdGraphPin* Complete = nullptr;
        UEdGraphPin* Caster = nullptr;
    };

    bool InspectSkill(UBlueprint* BP, FSkillSite& Site, FString& Problem)
    {
        for (UEdGraph* Graph : BP->UbergraphPages)
            for (UEdGraphNode* Node : Graph->Nodes)
                if (auto* Call = Cast<UK2Node_CallFunction>(Node))
                    if (Call->FunctionReference.GetMemberName() == TEXT("ApplyBuffs"))
                    {
                        if (Site.Apply)
                        {
                            Problem = TEXT("More than one Apply Buffs call in the Protect template.");
                            return false;
                        }
                        Site.Apply = Call;
                    }
        if (!Site.Apply)
        {
            Problem = TEXT("No Apply Buffs call in the Protect template.");
            return false;
        }
        auto* InArray = TraceSource(Pin(Site.Apply, TEXT("targetUnits")));
        auto* MakeArray = InArray ? Cast<UK2Node_MakeArray>(InArray->GetOwningNode()) : nullptr;
        Site.Caster = OneLink(Pin(MakeArray, TEXT("[0]")));
        auto* CasterSource = TraceSource(Pin(MakeArray, TEXT("[0]")));
        auto* GetCaster = CasterSource ? Cast<UK2Node_VariableGet>(CasterSource->GetOwningNode()) : nullptr;
        if (!GetCaster || GetCaster->VariableReference.GetMemberName() != TEXT("currentAttackingUnit") ||
            Pin(MakeArray, TEXT("[1]")))
        {
            Problem = TEXT("Protect's Apply Buffs must use a single currentAttackingUnit entry.");
            return false;
        }

        auto* Then = Pin(Site.Apply, UEdGraphSchema_K2::PN_Then);
        Site.Start = OneLink(Pin(Site.Apply, UEdGraphSchema_K2::PN_Execute));
        Site.Complete = OneLink(Then);
        TArray<UEdGraphPin*> Completion;
        FindConsumers(Then, Completion);
        auto* Done = Completion.Num() == 1 ? Cast<UK2Node_CallDelegate>(Completion[0]->GetOwningNode()) : nullptr;
        if (!Site.Start || !Site.Complete || !Done || Done->DelegateReference.GetMemberName() != TEXT("OnSkillUsed"))
        {
            Problem = TEXT("Protect's Apply Buffs execution must continue to OnSkillUsed.");
            return false;
        }
        return true;
    }

    bool WireNewSkill(UBlueprint* BP, UFunction* ApplyBuff, UDataTable* Table)
    {
        FSkillSite Site;
        FString Problem;
        if (!InspectSkill(BP, Site, Problem))
        {
            UE_LOG(LogReditusGuardUpInstall, Error, TEXT("New skill copy: %s"), *Problem);
            return false;
        }
        UEdGraph* Graph = Site.Apply->GetGraph();
        BP->Modify();
        Graph->Modify();
        auto* Call = AddCall(Graph, ApplyBuff, Site.Apply->NodePosX, Site.Apply->NodePosY);
        auto* TablePin = Pin(Call, TEXT("BuffTable"));
        auto* BuffPin = Pin(Call, TEXT("BuffID"));
        if (!TablePin || !BuffPin) return false;
        TablePin->DefaultObject = Table;
        BuffPin->DefaultValue = BuffID.ToString();
        Site.Apply->BreakAllNodeLinks();
        if (!Link(Graph, Site.Start, Pin(Call, UEdGraphSchema_K2::PN_Execute)) ||
            !Link(Graph, Pin(Call, UEdGraphSchema_K2::PN_Then), Site.Complete) ||
            !Link(Graph, Site.Caster, Pin(Call, TEXT("Caster")))) return false;
        FBlueprintEditorUtils::RemoveNode(BP, Site.Apply, true);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
        return Compile(BP);
    }

    struct FDefenseRead
    {
        UEdGraphPin* Base = nullptr;
        UEdGraphPin* Target = nullptr;
        UEdGraphPin* Destination = nullptr;
        bool bAlreadyWired = false;
    };

    bool IsDamageRequestType(const UEdGraphPin* P)
    {
        if (!P || P->PinType.PinCategory != UEdGraphSchema_K2::PC_Struct) return false;
        auto* Struct = P->PinType.PinSubCategoryObject.Get();
        return Struct && Struct->GetPathName() == TEXT("/Script/Chapter1Reditus.ReditusDamageRequest");
    }

    bool IsLiveNativeCalculator(UK2Node_CallFunction* Call)
    {
        if (!Call || !IsDamageRequestType(Pin(Call, TEXT("Request")))) return false;
        const FName Name = Call->FunctionReference.GetMemberName();
        if (Name != TEXT("CalculateDamageFromLegacyType") && Name != TEXT("CalculateDamage")) return false;
        const auto* Exec = Pin(Call, UEdGraphSchema_K2::PN_Execute);
        const auto* Result = Pin(Call, UEdGraphSchema_K2::PN_ReturnValue);
        return (!Exec || Exec->LinkedTo.Num()) && Result && Result->LinkedTo.Num();
    }

    // Only accept a TargetDefense input on the known native request struct
    // that flows to a connected damage calculator. Ignore unused prototypes.
    bool IsNativeDamageRequest(UEdGraphNode* Node)
    {
        if (!Cast<UK2Node_MakeStruct>(Node)) return false;
        for (UEdGraphPin* Output : Node->Pins)
        {
            if (!Output || Output->Direction != EGPD_Output || !IsDamageRequestType(Output)) continue;
            TArray<UEdGraphPin*> Consumers;
            FindConsumers(Output, Consumers);
            for (UEdGraphPin* Consumer : Consumers)
                if (Consumer->PinName == TEXT("Request") &&
                    IsLiveNativeCalculator(Cast<UK2Node_CallFunction>(Consumer->GetOwningNode()))) return true;
        }
        return false;
    }

    bool IsIntDefensePin(UEdGraphPin* Input)
    {
        return Input && Input->Direction == EGPD_Input &&
            Input->PinType.PinCategory == UEdGraphSchema_K2::PC_Int;
    }

    bool InspectDefense(UBlueprint* BP, UFunction* Function, TArray<FDefenseRead>& Reads, FString& Problem)
    {
        UEdGraph* Damage = nullptr;
        for (UEdGraph* Graph : BP->FunctionGraphs)
            if (Graph->GetFName() == TEXT("CalculateDamage")) Damage = Graph;
        if (!Damage || !Cast<UEdGraphSchema_K2>(Damage->GetSchema()))
        {
            Problem = TEXT("BP_BattleController.CalculateDamage was not found.");
            return false;
        }

        for (UEdGraphNode* Node : Damage->Nodes)
        {
            auto* Call = Cast<UK2Node_CallFunction>(Node);
            const bool bSubtract = Call && Call->FunctionReference.GetMemberName() == TEXT("Subtract_IntInt");
            const bool bNativeMake = IsNativeDamageRequest(Node);
            auto* Request = IsLiveNativeCalculator(Call) ? Pin(Call, TEXT("Request")) : nullptr;
            const bool bNativeSplit = Request && Request->SubPins.Num() > 0;
            const bool bNative = bNativeMake || bNativeSplit;
            if (!bSubtract && !bNative) continue;
            auto* Destination = Pin(Node, bNativeSplit ? TEXT("Request_TargetDefense") :
                bNativeMake ? TEXT("TargetDefense") : TEXT("B"));
            if (bNativeSplit && Destination && Destination->ParentPin != Request) Destination = nullptr;
            if (!IsIntDefensePin(Destination))
            {
                if (bNative)
                {
                    Problem = FString::Printf(TEXT("Native damage request %s needs a visible Integer TargetDefense pin."), *Node->GetName());
                    return false;
                }
                continue;
            }

            auto* OriginalSource = TraceSource(Destination);
            auto* Existing = OriginalSource ? Cast<UK2Node_CallFunction>(OriginalSource->GetOwningNode()) : nullptr;
            const bool bAlready = Existing && Existing->GetTargetFunction() == Function &&
                OriginalSource == Pin(Existing, UEdGraphSchema_K2::PN_ReturnValue);
            auto* BaseSource = bAlready ? TraceSource(Pin(Existing, TEXT("BaseDefense"))) : OriginalSource;
            auto* Get = BaseSource ? Cast<UK2Node_VariableGet>(BaseSource->GetOwningNode()) : nullptr;
            if (!Get || Get->VariableReference.GetMemberName() != TEXT("defense"))
            {
                if (bNative || bAlready)
                {
                    Problem = FString::Printf(TEXT("TargetDefense on %s does not trace to the target's physical defense getter."), *Node->GetName());
                    return false;
                }
                // Other arithmetic, including magical defense, stays as-is.
                continue;
            }

            auto* GetterTarget = Pin(Get, UEdGraphSchema_K2::PN_Self);
            auto* Target = OneLink(GetterTarget);
            auto* TargetSource = TraceSource(GetterTarget);
            auto* TargetGetter = TargetSource ? Cast<UK2Node_VariableGet>(TargetSource->GetOwningNode()) : nullptr;
            if (!Target || !TargetGetter || TargetGetter->VariableReference.GetMemberName() != TEXT("currentTargetUnit") ||
                (bAlready && TraceSource(Pin(Existing, TEXT("Target"))) != TargetSource))
            {
                Problem = FString::Printf(TEXT("Physical defense input on %s does not read currentTargetUnit."), *Node->GetName());
                return false;
            }
            Reads.Add(FDefenseRead{OneLink(Destination), Target, Destination, bAlready});
        }
        if (!Reads.Num())
        {
            Problem = TEXT("No supported physical defense input found in BP_BattleController.CalculateDamage.");
            return false;
        }
        return true;
    }

    bool AllDefenseReadsWired(const TArray<FDefenseRead>& Reads)
    {
        for (const auto& Read : Reads)
            if (!Read.bAlreadyWired) return false;
        return Reads.Num() > 0;
    }

    bool WireDefense(UBlueprint* BP, UFunction* Function, const TArray<FDefenseRead>& Reads)
    {
        if (AllDefenseReadsWired(Reads)) return true;
        BP->Modify();
        struct FInsertedRead
        {
            UEdGraphPin* Base;
            UEdGraphPin* Target;
            UK2Node_CallFunction* Call;
        };
        TArray<FInsertedRead> Inserted;
        for (const auto& Read : Reads)
        {
            if (Read.bAlreadyWired) continue;
            auto* Source = Read.Base->GetOwningNode();
            UEdGraph* Graph = Source->GetGraph();
            Graph->Modify();
            Source->Modify();
            Read.Target->GetOwningNode()->Modify();
            Read.Destination->GetOwningNode()->Modify();
            UK2Node_CallFunction* Call = nullptr;
            for (const auto& Previous : Inserted)
                if (Previous.Base == Read.Base && Previous.Target == Read.Target)
                {
                    Call = Previous.Call;
                    break;
                }
            if (!Call)
            {
                Call = AddCall(Graph, Function, Source->NodePosX + 20, Source->NodePosY + 110);
                if (!Link(Graph, Read.Base, Pin(Call, TEXT("BaseDefense"))) ||
                    !Link(Graph, Read.Target, Pin(Call, TEXT("Target")))) return false;
                Inserted.Add(FInsertedRead{Read.Base, Read.Target, Call});
            }
            Read.Base->BreakLinkTo(Read.Destination);
            if (!Link(Graph, Pin(Call, UEdGraphSchema_K2::PN_ReturnValue), Read.Destination)) return false;
        }
        FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
        return Compile(BP);
    }

    template<typename PropType>
    PropType* SkillField(const UScriptStruct* Struct, const FString& Stem)
    {
        PropType* Result = nullptr;
        const FString Prefix = Stem + TEXT("_");
        for (TFieldIterator<FProperty> It(Struct); It; ++It)
        {
            const FString Name = It->GetName();
            if (Name != Stem && !Name.StartsWith(Prefix)) continue;
            auto* Field = CastField<PropType>(*It);
            if (!Field || Result) return nullptr;
            Result = Field;
        }
        return Result;
    }

    struct FBuffRowLoop
    {
        UEdGraphPin* Body = nullptr;
        UEdGraphPin* Recipient = nullptr;
        UEdGraphPin* Legacy = nullptr;
        bool bInstalled = false;
    };

    UK2Node_Self* AddSkillSelf(UEdGraph* Graph, int32 X, int32 Y)
    {
        auto* Self = NewObject<UK2Node_Self>(Graph, NAME_None, RF_Transactional);
        Graph->AddNode(Self, false, false);
        Self->CreateNewGuid();
        Self->AllocateDefaultPins();
        Self->NodePosX = X;
        Self->NodePosY = Y;
        return Self;
    }

    bool FindBuffRowLoop(UBlueprint* Tech, UFunction* Apply, FBuffRowLoop& Site)
    {
        if (!Tech || !Apply) return false;
        for (auto* Graph : Tech->UbergraphPages)
            for (auto* Node : Graph->Nodes)
                if (auto* Event = Cast<UK2Node_CustomEvent>(Node))
                    if (Event->CustomFunctionName == TEXT("ApplyBuffs"))
                    {
                        if (Site.Body) return false;
                        auto* Entry = OneLink(Pin(Event, UEdGraphSchema_K2::PN_Then));
                        auto* Loop = Entry ? Entry->GetOwningNode() : nullptr;
                        if (!Entry || Entry->PinName != TEXT("Exec") ||
                            TraceSource(Pin(Loop, TEXT("Array"))) != Pin(Event, TEXT("targetUnits"))) return false;
                        Site.Body = Pin(Loop, TEXT("LoopBody"));
                        Site.Recipient = Pin(Loop, TEXT("Array Element"));
                        Site.Legacy = OneLink(Site.Body);
                        if (!Site.Recipient || !Site.Legacy) return false;
                        const auto* Call = Cast<UK2Node_CallFunction>(Site.Legacy->GetOwningNode());
                        Site.bInstalled = Call && Call->FunctionReference.GetMemberName() == Apply->GetFName();
                        if (Site.bInstalled && TraceSource(Pin(Site.Legacy->GetOwningNode(), TEXT("EligibleTarget"))) != Site.Recipient)
                            return false;
                    }
        return Site.Body && Site.Recipient && Site.Legacy;
    }

    bool WireBuffRowLoop(UBlueprint* Tech, UFunction* Apply, UDataTable* Skills, const FBuffRowLoop& Site)
    {
        if (Site.bInstalled) return true;
        auto* Loop = Site.Body->GetOwningNode();
        auto* Graph = Loop->GetGraph();
        Tech->Modify();
        Graph->Modify();
        Loop->Modify();
        Site.Legacy->GetOwningNode()->Modify();
        auto* Call = AddCall(Graph, Apply, Loop->NodePosX + 208, Loop->NodePosY + 120);
        auto* Self = AddSkillSelf(Graph, Call->NodePosX - 150, Call->NodePosY + 165);
        auto* Branch = NewObject<UK2Node_IfThenElse>(Graph, NAME_None, RF_Transactional);
        Graph->AddNode(Branch, false, false);
        Branch->CreateNewGuid();
        Branch->AllocateDefaultPins();
        Branch->NodePosX = Call->NodePosX + 244;
        Branch->NodePosY = Call->NodePosY;
        auto* Table = Pin(Call, TEXT("SkillTable"));
        if (!Table) return false;
        Table->DefaultObject = Skills;
        if (!Link(Graph, Pin(Self, UEdGraphSchema_K2::PN_Self), Pin(Call, TEXT("Skill"))) ||
            !Link(Graph, Site.Recipient, Pin(Call, TEXT("EligibleTarget"))) ||
            !Link(Graph, Pin(Call, UEdGraphSchema_K2::PN_ReturnValue), Pin(Branch, TEXT("Condition"))) ||
            !Link(Graph, Pin(Call, UEdGraphSchema_K2::PN_Then), Pin(Branch, UEdGraphSchema_K2::PN_Execute))) return false;
        Site.Body->BreakLinkTo(Site.Legacy);
        if (!Link(Graph, Site.Body, Pin(Call, UEdGraphSchema_K2::PN_Execute)) ||
            !Link(Graph, Pin(Branch, UEdGraphSchema_K2::PN_Else), Site.Legacy)) return false;
        FBlueprintEditorUtils::MarkBlueprintAsModified(Tech);
        return true;
    }

    struct FSkillSpawnID
    {
        UEdGraphPin* AfterSpawn;
        UEdGraphPin* SpawnedSkill;
        UEdGraphPin* Continue;
        UEdGraphPin* SelectedID;
        bool bInstalled;
    };

    bool FindPlayerSkillSpawns(UBlueprint* Controller, UFunction* Setter, TArray<FSkillSpawnID>& Sites)
    {
        if (!Controller || !Setter) return false;
        for (auto* Graph : Controller->UbergraphPages)
            for (auto* Node : Graph->Nodes)
            {
                auto* Spawn = Cast<UK2Node_SpawnActorFromClass>(Node);
                if (!Spawn) continue;
                auto* Before = TraceSource(Pin(Spawn, UEdGraphSchema_K2::PN_Execute));
                auto* CastNode = Before ? Cast<UK2Node_ClassDynamicCast>(Before->GetOwningNode()) : nullptr;
                auto* Class = TraceSource(Pin(CastNode, TEXT("Class")));
                auto* Break = Class ? Cast<UK2Node_BreakStruct>(Class->GetOwningNode()) : nullptr;
                if (!Break || !Class->PinName.ToString().StartsWith(TEXT("LegacySkill_"))) continue;
                auto* Definition = TraceSource(Pin(Break, TEXT("S_SkillDefinition")));
                auto* Lookup = Definition ? Cast<UK2Node_CallFunction>(Definition->GetOwningNode()) : nullptr;
                if (!Lookup || Lookup->FunctionReference.GetMemberName() != TEXT("GetSkillDefinition")) continue;
                auto* ID = TraceSource(Pin(Lookup, TEXT("Skill ID")));
                auto* Event = ID ? Cast<UK2Node_CustomEvent>(ID->GetOwningNode()) : nullptr;
                if (!Event || Event->CustomFunctionName != TEXT("OnSkillSelectedHandler") ||
                    ID->PinName != TEXT("selectedSkill") || ID->PinType.PinCategory != UEdGraphSchema_K2::PC_Name) continue;
                auto* After = Pin(Spawn, UEdGraphSchema_K2::PN_Then);
                auto* Spawned = Pin(Spawn, UEdGraphSchema_K2::PN_ReturnValue);
                auto* Next = OneLink(After);
                if (!Spawned || !Next) return false;
                auto* Call = Cast<UK2Node_CallFunction>(Next->GetOwningNode());
                const bool bInstalled = Call && Call->FunctionReference.GetMemberName() == Setter->GetFName();
                if (bInstalled && (TraceSource(Pin(Call, TEXT("Skill"))) != Spawned ||
                    TraceSource(Pin(Call, TEXT("SkillID"))) != ID)) return false;
                auto* StoreExec = bInstalled ? OneLink(Pin(Call, UEdGraphSchema_K2::PN_Then)) : Next;
                auto* Store = StoreExec ? Cast<UK2Node_VariableSet>(StoreExec->GetOwningNode()) : nullptr;
                if (!Store || Store->VariableReference.GetMemberName() != TEXT("currentSkill") ||
                    TraceSource(Pin(Store, TEXT("currentSkill"))) != Spawned) return false;
                Sites.Add(FSkillSpawnID{After, Spawned, Next, ID, bInstalled});
            }
        return Sites.Num() == 2;
    }

    bool WirePlayerSkillSpawns(UBlueprint* Controller, UFunction* Setter, const TArray<FSkillSpawnID>& Sites)
    {
        bool bChanged = false;
        for (const auto& Site : Sites)
        {
            if (Site.bInstalled) continue;
            auto* Spawn = Site.AfterSpawn->GetOwningNode();
            auto* Graph = Spawn->GetGraph();
            Controller->Modify();
            Graph->Modify();
            Spawn->Modify();
            Site.Continue->GetOwningNode()->Modify();
            Site.SelectedID->GetOwningNode()->Modify();
            auto* Call = AddCall(Graph, Setter, Spawn->NodePosX + 260, Spawn->NodePosY + 150);
            if (!Link(Graph, Site.SpawnedSkill, Pin(Call, TEXT("Skill"))) ||
                !Link(Graph, Site.SelectedID, Pin(Call, TEXT("SkillID")))) return false;
            Site.AfterSpawn->BreakLinkTo(Site.Continue);
            if (!Link(Graph, Site.AfterSpawn, Pin(Call, UEdGraphSchema_K2::PN_Execute)) ||
                !Link(Graph, Pin(Call, UEdGraphSchema_K2::PN_Then), Site.Continue)) return false;
            bChanged = true;
        }
        if (bChanged) FBlueprintEditorUtils::MarkBlueprintAsModified(Controller);
        return true;
    }

    bool PrepareTableBuffSkill(UBlueprint* Skill)
    {
        UK2Node_Event* Use = nullptr;
        UK2Node_CallFunction* Apply = nullptr;
        for (auto* Graph : Skill->UbergraphPages)
            for (auto* Node : Graph->Nodes)
            {
                if (auto* Event = Cast<UK2Node_Event>(Node))
                    if (Event->EventReference.GetMemberName() == TEXT("UseSkill") ||
                        Event->EventReference.GetMemberName() == TEXT("Use Skill"))
                    {
                        if (Use) return false;
                        Use = Event;
                    }
                if (auto* Call = Cast<UK2Node_CallFunction>(Node))
                    if (Call->FunctionReference.GetMemberName() == TEXT("ApplyBuffs"))
                    {
                        if (Apply) return false;
                        Apply = Call;
                    }
            }
        auto* UseExec = Pin(Use, UEdGraphSchema_K2::PN_Then);
        auto* ApplyExec = Pin(Apply, UEdGraphSchema_K2::PN_Execute);
        auto* Finish = OneLink(Pin(Apply, UEdGraphSchema_K2::PN_Then));
        auto* Done = Finish ? Cast<UK2Node_CallDelegate>(Finish->GetOwningNode()) : nullptr;
        auto* Array = TraceSource(Pin(Apply, TEXT("targetUnits")));
        auto* MakeArray = Array ? Cast<UK2Node_MakeArray>(Array->GetOwningNode()) : nullptr;
        auto* Target = TraceSource(Pin(MakeArray, TEXT("[0]")));
        if (!UseExec || !ApplyExec || !Done || Done->DelegateReference.GetMemberName() != TEXT("OnSkillUsed") ||
            !IsVariableOutput(Target, TEXT("currentTargetUnit")) || Use->GetGraph() != Apply->GetGraph()) return false;
        auto* Graph = Use->GetGraph();
        UseExec->BreakAllPinLinks();
        ApplyExec->BreakAllPinLinks();
        if (!Link(Graph, UseExec, ApplyExec)) return false;
        FBlueprintEditorUtils::MarkBlueprintAsModified(Skill);
        if (!Compile(Skill) || !Skill->GeneratedClass) return false;
        auto* Default = Skill->GeneratedClass->GetDefaultObject();
        const auto* Buffs = FindFProperty<FArrayProperty>(Skill->GeneratedClass, TEXT("buffs"));
        const auto* Type = FindFProperty<FByteProperty>(Skill->GeneratedClass, TEXT("skillType"));
        if (!Default || !Buffs || !Type || !Type->Enum ||
            Type->Enum->GetPathName() != TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Skills/E_SkillType.E_SkillType"))
            return false;
        const int64 Defensive = Type->Enum->GetValueByNameString(TEXT("NewEnumerator1"));
        if (Defensive < 0 || Defensive > 255) return false;
        Default->Modify();
        Type->SetPropertyValue_InContainer(Default, static_cast<uint8>(Defensive));
        FScriptArrayHelper Entries(Buffs, Buffs->ContainerPtrToValuePtr<void>(Default));
        Entries.EmptyValues();
        Skill->MarkPackageDirty();
        return true;
    }

    void InstallSkillBuffRows()
    {
        if (!GEditor || GEditor->PlayWorld)
        {
            UE_LOG(LogReditusGuardUpInstall, Error, TEXT("Stop Play before running Reditus.InstallSkillBuffRows."));
            return;
        }
        auto* Skills = LoadObject<UDataTable>(nullptr, SkillsPath);
        if (!Skills || !UReditusTableBuffLibrary::FindSkillBuffRowsField(Skills->GetRowStruct()))
        {
            UE_LOG(LogReditusGuardUpInstall, Error, TEXT("Save Buff/Debuff (Data Table Row Handle) on S_SkillDefinition first. A BuffRows array is also supported. No assets were changed."));
            return;
        }
        auto* Tech = LoadObject<UBlueprint>(nullptr, SkillTargetingPath);
        auto* Controller = LoadObject<UBlueprint>(nullptr, BattlePath);
        auto* Library = UReditusTableBuffLibrary::StaticClass();
        auto* Setter = Library->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReditusTableBuffLibrary, SetTableSkillID));
        auto* Apply = Library->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReditusTableBuffLibrary, ApplySkillBuffRows));
        FBuffRowLoop Loop;
        TArray<FSkillSpawnID> Sites;
        if (!FindBuffRowLoop(Tech, Apply, Loop) || !FindPlayerSkillSpawns(Controller, Setter, Sites))
        {
            UE_LOG(LogReditusGuardUpInstall, Error, TEXT("Skill Buff Rows could not recognize ApplyBuffs or the two player skill spawns. No assets were changed."));
            return;
        }

        const TCHAR* Template = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Skills/BuffSkills/BP_ProtectOtherBuff.BP_ProtectOtherBuff");
        const TCHAR* Package = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Skills/BuffSkills/BP_TableBuffSkill");
        const TCHAR* Path = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Skills/BuffSkills/BP_TableBuffSkill.BP_TableBuffSkill");
        UObject* Occupant = LoadObject<UObject>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
        auto* Skill = Cast<UBlueprint>(Occupant);
        if ((Occupant && (!Skill || !Skill->GeneratedClass || !Tech->GeneratedClass ||
            !Skill->GeneratedClass->IsChildOf(Tech->GeneratedClass))) || (!Occupant && FPackageName::DoesPackageExist(Package)))
        {
            UE_LOG(LogReditusGuardUpInstall, Error, TEXT("The BP_TableBuffSkill path is occupied. No assets were changed."));
            return;
        }
        if (!Skill)
        {
            auto* Source = LoadObject<UBlueprint>(nullptr, Template);
            if (Source)
                Skill = Cast<UBlueprint>(FAssetToolsModule::GetModule().Get().DuplicateAsset(
                    TEXT("BP_TableBuffSkill"), SkillFolder, Source));
            if (!Skill || !PrepareTableBuffSkill(Skill))
            {
                UE_LOG(LogReditusGuardUpInstall, Error, TEXT("Could not prepare the new BP_TableBuffSkill. Existing assets were not changed."));
                return;
            }
        }

        bool bOK = false;
        {
            const FScopedTransaction Transaction(NSLOCTEXT("ReditusBuffs", "InstallSkillBuffRows", "Connect Skill Buff Rows"));
            if (!Transaction.IsOutstanding()) return;
            bool bSpawnChanges = false;
            for (const auto& Site : Sites) bSpawnChanges |= !Site.bInstalled;
            bOK = WireBuffRowLoop(Tech, Apply, Skills, Loop) && WirePlayerSkillSpawns(Controller, Setter, Sites) &&
                (Loop.bInstalled || Compile(Tech)) && (!bSpawnChanges || Compile(Controller));
        }
        if (!bOK)
        {
            const bool bUndone = GEditor->UndoTransaction(false);
            if (bUndone) { Compile(Tech); Compile(Controller); }
            UE_LOG(LogReditusGuardUpInstall, Error, TEXT("Could not wire Skill Buff Rows. Undo: %s. The new unsaved skill Blueprint may remain."),
                bUndone ? TEXT("succeeded") : TEXT("failed; do not save the skill graphs"));
            return;
        }
        UE_LOG(LogReditusGuardUpInstall, Display,
            TEXT("Skill Buff Rows installed. Save BP_BattleTechsBase, BP_BattleController, and BP_TableBuffSkill. Set the skill's Buff/Debuff and Legacy Skill in DT_Skills."));
    }

    FAutoConsoleCommand GInstallSkillBuffRows(
        TEXT("Reditus.InstallSkillBuffRows"),
        TEXT("Connect the BuffRows field on S_SkillDefinition to battle skills. Stop Play first."),
        FConsoleCommandDelegate::CreateStatic(&InstallSkillBuffRows));

    void InstallGuardUpTableTest()
    {
        if (!GEditor || GEditor->PlayWorld)
        {
            UE_LOG(LogReditusGuardUpInstall, Error, TEXT("Stop Play before running Reditus.InstallGuardUpTableTest."));
            return;
        }
        auto* OldSkill = LoadObject<UBlueprint>(nullptr, LegacySkillPath);
        auto* Controller = LoadObject<UBlueprint>(nullptr, BattlePath);
        auto* Skills = LoadObject<UDataTable>(nullptr, SkillsPath);
        auto* Visuals = LoadObject<UDataTable>(nullptr, VisualPath);
        UObject* BuffOccupant = LoadObject<UObject>(nullptr, BuffPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
        auto* Buffs = Cast<UDataTable>(BuffOccupant);
        UObject* SkillOccupant = LoadObject<UObject>(nullptr, NewSkillPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
        auto* NewSkill = Cast<UBlueprint>(SkillOccupant);
        auto* Library = UReditusTableBuffLibrary::StaticClass();
        auto* ApplyBuff = Library->FindFunctionByName(TEXT("ApplyBuffByID"));
        auto* Defense = Library->FindFunctionByName(TEXT("GetBuffedDefense"));
        const auto* SkillStruct = Skills ? Skills->GetRowStruct() : nullptr;
        if (!OldSkill || !OldSkill->GeneratedClass || !Controller || !SkillStruct || !ApplyBuff || !Defense ||
            (BuffOccupant && (!Buffs || Buffs->GetRowStruct() != FReditusTableBuffRow::StaticStruct())) ||
            (!Buffs && FPackageName::DoesPackageExist(BuffPackage)))
        {
            UE_LOG(LogReditusGuardUpInstall, Error, TEXT("Required project assets are missing, or the DT_Buffs path is already used by a different table type."));
            return;
        }

        auto* LegacyClass = SkillField<FClassProperty>(SkillStruct, TEXT("LegacySkill"));
        auto* Display = SkillField<FTextProperty>(SkillStruct, TEXT("DisplayName"));
        auto* Short = SkillField<FTextProperty>(SkillStruct, TEXT("ShortDescription"));
        auto* Long = SkillField<FTextProperty>(SkillStruct, TEXT("LongDescription"));
        auto* MPCost = SkillField<FIntProperty>(SkillStruct, TEXT("MPCost"));
        FString MissingFields;
        const auto RequireField = [&MissingFields](const FProperty* Field, const TCHAR* Name)
        {
            if (!Field)
            {
                if (!MissingFields.IsEmpty()) MissingFields += TEXT(", ");
                MissingFields += Name;
            }
        };
        RequireField(LegacyClass, TEXT("LegacySkill (class)"));
        RequireField(Display, TEXT("DisplayName (text)"));
        RequireField(Short, TEXT("ShortDescription (text)"));
        RequireField(Long, TEXT("LongDescription (text)"));
        RequireField(MPCost, TEXT("MPCost (integer)"));

        // Prefer the original row ID. Also accept a renamed row when it is
        // the only row that points to this exact Protect template class.
        FName TemplateRowID = SourceSkillID;
        const uint8* SourceRow = Skills->GetRowMap().FindRef(SourceSkillID);
        if (LegacyClass && (!SourceRow ||
            LegacyClass->GetObjectPropertyValue_InContainer(SourceRow) != OldSkill->GeneratedClass))
        {
            SourceRow = nullptr;
            for (const auto& Entry : Skills->GetRowMap())
            {
                if (!Entry.Value ||
                    LegacyClass->GetObjectPropertyValue_InContainer(Entry.Value) != OldSkill->GeneratedClass) continue;
                if (SourceRow)
                {
                    SourceRow = nullptr;
                    break;
                }
                SourceRow = Entry.Value;
                TemplateRowID = Entry.Key;
            }
        }

        TArray<FDefenseRead> Reads;
        FSkillSite LegacySite;
        FString SkillProblem;
        FString DefenseProblem;
        const bool bSkillOK = InspectSkill(OldSkill, LegacySite, SkillProblem);
        const bool bDefenseOK = InspectDefense(Controller, Defense, Reads, DefenseProblem);
        if (!MissingFields.IsEmpty())
        {
            UE_LOG(LogReditusGuardUpInstall, Error, TEXT("DT_Skills missing or ambiguous fields: %s."), *MissingFields);
        }
        if (LegacyClass && !SourceRow)
        {
            UE_LOG(LogReditusGuardUpInstall, Error, TEXT("DT_Skills needs ProtectSelfBuff or one unique row with LegacySkill = BP_ProtectSelfBuff."));
        }
        if (!bSkillOK)
        {
            UE_LOG(LogReditusGuardUpInstall, Error, TEXT("BP_ProtectSelfBuff: %s"), *SkillProblem);
        }
        if (!bDefenseOK)
        {
            UE_LOG(LogReditusGuardUpInstall, Error, TEXT("CalculateDamage: %s"), *DefenseProblem);
        }
        if (!MissingFields.IsEmpty() || !SourceRow || !bSkillOK || !bDefenseOK)
        {
            UE_LOG(LogReditusGuardUpInstall, Error, TEXT("Guard Up preflight failed. No changes were made."));
            return;
        }
        UE_LOG(LogReditusGuardUpInstall, Display, TEXT("Guard Up preflight passed: DT_Skills template %s; %d physical defense input(s)."),
            *TemplateRowID.ToString(), Reads.Num());

        const auto* ExistingSkillRow = Skills->GetRowMap().FindRef(TestSkillID);
        if (SkillOccupant || ExistingSkillRow || FPackageName::DoesPackageExist(NewSkillPackage))
        {
            if (NewSkill && NewSkill->GeneratedClass && ExistingSkillRow && Buffs &&
                Buffs->GetRowMap().Contains(BuffID) && AllDefenseReadsWired(Reads) &&
                LegacyClass->GetObjectPropertyValue_InContainer(ExistingSkillRow) == NewSkill->GeneratedClass)
            {
                UE_LOG(LogReditusGuardUpInstall, Display, TEXT("Guard Up table test is already installed. Existing rows were kept."));
            }
            else
            {
                UE_LOG(LogReditusGuardUpInstall, Error, TEXT("The Guard Up skill or test row already exists without a complete install. Existing assets were kept."));
            }
            return;
        }

        const FReditusBuffVisualRow* SeedVisuals = Visuals && Visuals->GetRowStruct() == FReditusBuffVisualRow::StaticStruct() ?
            Visuals->FindRow<FReditusBuffVisualRow>(BuffID, TEXT("Guard Up visual seed"), false) : nullptr;
        const FReditusTableBuffRow* ExistingBuff = Buffs ?
            Buffs->FindRow<FReditusTableBuffRow>(BuffID, TEXT("Guard Up test"), false) : nullptr;
        if (!ExistingBuff && !SeedVisuals)
        {
            UE_LOG(LogReditusGuardUpInstall, Error, TEXT("Missing DT_BuffVisuals -> Protect. Install the working visual pilot first or provide the combined Protect row."));
            return;
        }

        if (!Buffs)
        {
            UPackage* Package = CreatePackage(BuffPackage);
            Buffs = NewObject<UDataTable>(Package, TEXT("DT_Buffs"), RF_Public | RF_Standalone | RF_Transactional);
            Buffs->RowStruct = FReditusTableBuffRow::StaticStruct();
            FAssetRegistryModule::AssetCreated(Buffs);
        }
        if (!ExistingBuff)
        {
            FReditusTableBuffRow Row;
            Row.DisplayName = NSLOCTEXT("ReditusBuffs", "GuardUp", "Guard Up");
            Row.EffectSummary = NSLOCTEXT("ReditusBuffs", "ProtectSummary", "Raises physical defense.");
            Row.RuntimeEffect = EReditusTableBuffEffect::PhysicalDefense;
            Row.Visuals = *SeedVisuals;
            Buffs->AddRow(BuffID, Row);
            Buffs->MarkPackageDirty();
        }

        auto& Tools = FAssetToolsModule::GetModule().Get();
        NewSkill = Cast<UBlueprint>(Tools.DuplicateAsset(NewSkillName, SkillFolder, OldSkill));
        if (!NewSkill || NewSkill == OldSkill || !WireNewSkill(NewSkill, ApplyBuff, Buffs) || !NewSkill->GeneratedClass)
        {
            UE_LOG(LogReditusGuardUpInstall, Error, TEXT("Could not prepare the new skill Blueprint. Existing skill, table rows and battle graphs were not changed. New unsaved assets may exist."));
            return;
        }
        NewSkill->MarkPackageDirty();

        // Compile the new Blueprint before touching either existing asset.
        FStructOnScope Copy(SkillStruct);
        SkillStruct->CopyScriptStruct(Copy.GetStructMemory(), SourceRow);
        auto* Memory = Copy.GetStructMemory();
        LegacyClass->SetObjectPropertyValue_InContainer(Memory, NewSkill->GeneratedClass);
        Display->SetPropertyValue_InContainer(Memory, NSLOCTEXT("ReditusBuffs", "GuardUpTestSkill", "Guard Up (Table Test)"));
        Short->SetPropertyValue_InContainer(Memory, NSLOCTEXT("ReditusBuffs", "GuardUpTestShort", "Increases the caster's physical defense."));
        Long->SetPropertyValue_InContainer(Memory, NSLOCTEXT("ReditusBuffs", "GuardUpTestLong", "Applies Guard Up using the Protect row in DT_Buffs."));
        MPCost->SetPropertyValue_InContainer(Memory, 0);

        bool bInstalled = false;
        {
            const FScopedTransaction Transaction(NSLOCTEXT("ReditusBuffs", "InstallTableTest", "Install Guard Up table test"));
            if (!Transaction.IsOutstanding())
            {
                UE_LOG(LogReditusGuardUpInstall, Error, TEXT("Unable to open an undo transaction. Existing assets were not changed."));
                return;
            }
            if (WireDefense(Controller, Defense, Reads))
            {
                Skills->Modify();
                FDataTableEditorUtils::AddRow(Skills, TestSkillID);
                if (uint8* Dest = Skills->GetRowMap().FindRef(TestSkillID))
                {
                    SkillStruct->CopyScriptStruct(Dest, Memory);
                    Skills->PostEditChange();
                    Skills->MarkPackageDirty();
                    bInstalled = true;
                }
            }
        }
        if (!bInstalled)
        {
            const bool bUndone = GEditor->UndoTransaction(false);
            if (bUndone) Compile(Controller);
            UE_LOG(LogReditusGuardUpInstall, Error, TEXT("Guard Up test install could not finish. Undo: %s. New unsaved assets may remain."),
                bUndone ? TEXT("succeeded") : TEXT("failed; do not save the changes"));
            return;
        }
        UE_LOG(LogReditusGuardUpInstall, Display, TEXT("Guard Up table test installed. Save DT_Buffs, DT_Skills, BP_GuardUpTableSkill and BP_BattleController. Assign SkillID GuardUp_Test to a DataTable-spawned test enemy."));
    }

    FAutoConsoleCommand GInstallGuardUpTableTest(
        TEXT("Reditus.InstallGuardUpTableTest"),
        TEXT("Create DT_Buffs/Protect, a GuardUp_Test skill, and a pass-through defense reader. Stop Play first."),
        FConsoleCommandDelegate::CreateStatic(&InstallGuardUpTableTest));

    struct FCatalogueSeed
    {
        const TCHAR* ID;
        const TCHAR* DisplayName;
        const TCHAR* Category;
        const TCHAR* ProposedBehavior;
        const TCHAR* DurationMode;
        float Duration;
        float BaseChancePercent;
        const TCHAR* ResistanceStatusKey;
        const TCHAR* ReapplyPolicy;
        int32 MaxStacks;
        const TCHAR* ConflictGroup;
        const TCHAR* Symbol;
        float DefenseMultiplier;
        float ATBRateMultiplier;
        float AggroGainMultiplier;
        int32 FlatAggroOnApply;
        const TCHAR* AllowedSources;
        const TCHAR* AllowedTargets;
        bool bRemoveOnKO;
        bool bRemoveOnBattleEnd;
        const TCHAR* LegacyBuffClass;
        const TCHAR* ImplementationRoute;
        const TCHAR* ImplementationStatus;

        FReditusTableBuffRow MakeDesignRow() const
        {
            FReditusTableBuffRow Row;
            Row.DisplayName = FText::FromString(DisplayName);
            auto& Design = Row.Catalogue;
            Design.bDesignOnly = true;
            Design.Category = FName(Category);
            Design.ProposedBehavior = FText::FromString(ProposedBehavior);
            Design.DurationMode = FName(DurationMode);
            Design.Duration = Duration;
            Design.BaseChancePercent = BaseChancePercent;
            Design.ResistanceStatusKey = FName(ResistanceStatusKey);
            Design.ReapplyPolicy = FName(ReapplyPolicy);
            Design.MaxStacks = MaxStacks;
            Design.ConflictGroup = FName(ConflictGroup);
            Design.Symbol = FText::FromString(Symbol);
            Design.DefenseMultiplier = DefenseMultiplier;
            Design.ATBRateMultiplier = ATBRateMultiplier;
            Design.AggroGainMultiplier = AggroGainMultiplier;
            Design.FlatAggroOnApply = FlatAggroOnApply;
            Design.AllowedSources = FName(AllowedSources);
            Design.AllowedTargets = FName(AllowedTargets);
            Design.bRemoveOnKO = bRemoveOnKO;
            Design.bRemoveOnBattleEnd = bRemoveOnBattleEnd;
            Design.LegacyBuffClass = LegacyBuffClass;
            Design.ImplementationRoute = ImplementationRoute;
            Design.ImplementationStatus = ImplementationStatus;
            return Row;
        }
    };

    const FCatalogueSeed CatalogueSeeds[] = {
#include "ReditusBuffCatalogueSeeds.inl"
    };

    void UseLegacyClass(FReditusTableBuffRow& Row, const TCHAR* ClassPath, EReditusLegacyBuffDuration Duration)
    {
        Row.RuntimeEffect = EReditusTableBuffEffect::LegacyBlueprint;
        Row.LegacyEffectClass = TSoftClassPtr<AActor>(FSoftObjectPath(ClassPath));
        Row.LegacyDuration = Duration;
        Row.Catalogue.LegacyBuffClass = ClassPath;
        Row.Catalogue.bDesignOnly = false;
        Row.Catalogue.ImplementationStatus = TEXT("Uses existing Blueprint behavior");
        if (Duration == EReditusLegacyBuffDuration::UntilCured)
        {
            Row.Catalogue.DurationMode = TEXT("UntilCured");
            Row.Catalogue.Duration = 0.f;
            Row.Catalogue.bRemoveOnBattleEnd = false;
        }
    }

    UClass* ResolveLegacyBuffClass(const TCHAR* Path, const UClass* Base)
    {
        auto* Class = Path && *Path ? LoadObject<UClass>(nullptr, Path) : nullptr;
        return Class && Class != Base && Class->IsChildOf(Base) &&
            !Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated) ? Class : nullptr;
    }

    void ConfigureCatalogueEffect(FName ID, const FCatalogueSeed& Seed, const UClass* Base,
        FReditusTableBuffRow& Row)
    {
        using E = EReditusTableBuffEffect;
        const FText OriginalSummary = Row.EffectSummary;
        Row.EffectSummary = Row.Catalogue.ProposedBehavior.IsEmpty()
            ? FText::FromString(Seed.ProposedBehavior) : Row.Catalogue.ProposedBehavior;
        if (ID == TEXT("Sap"))
        {
            Row.RuntimeEffect = E::PhysicalDefense;
            Row.PhysicalDefenseMultiplier = Row.Catalogue.DefenseMultiplier >= 0.1f &&
                Row.Catalogue.DefenseMultiplier < 1.f ? Row.Catalogue.DefenseMultiplier : Seed.DefenseMultiplier;
            Row.EffectSummary = NSLOCTEXT("ReditusBuffs", "SapSummary", "Lowers physical defense.");
        }
        else if (ID == TEXT("Haste") || ID == TEXT("Slow"))
        {
            Row.RuntimeEffect = E::GaugeRate;
            Row.RateMultiplier = Row.Catalogue.ATBRateMultiplier > 0.f &&
                Row.Catalogue.ATBRateMultiplier <= 10.f && Row.Catalogue.ATBRateMultiplier != 1.f
                ? Row.Catalogue.ATBRateMultiplier : Seed.ATBRateMultiplier;
            Row.EffectSummary = ID == TEXT("Haste")
                ? NSLOCTEXT("ReditusBuffs", "HasteSummary", "Fills the ATB gauge faster.")
                : NSLOCTEXT("ReditusBuffs", "SlowSummary", "Fills the ATB gauge more slowly.");
        }
        else if (ID == TEXT("Quickcast") || ID == TEXT("Heavycast"))
        {
            Row.RuntimeEffect = E::PreparationRate;
            Row.RateMultiplier = ID == TEXT("Quickcast") ? 1.5f : 0.5f;
            Row.EffectSummary = ID == TEXT("Quickcast")
                ? NSLOCTEXT("ReditusBuffs", "QuickcastSummary", "Speeds up action preparation.")
                : NSLOCTEXT("ReditusBuffs", "HeavycastSummary", "Slows down action preparation.");
        }
        else if (ID == TEXT("Burn") || ID == TEXT("Venom"))
        {
            Row.RuntimeEffect = E::PeriodicDamage;
            Row.MaxHPDamagePercent = ID == TEXT("Venom") ? 8.f : 4.f;
            Row.EffectSummary = NSLOCTEXT("ReditusBuffs", "BurnSummary", "Deals damage based on max HP at the end of each full recipient cycle.");
        }
        else if (ID == TEXT("Regen") || ID == TEXT("Refresh"))
        {
            Row.RuntimeEffect = ID == TEXT("Regen") ? E::PeriodicHealing : E::PeriodicMP;
            Row.RecoveryPercent = 5.f;
            Row.EffectSummary = ID == TEXT("Regen")
                ? NSLOCTEXT("ReditusBuffs", "RegenSummary", "Restores a percentage of max HP at each full recipient Cycle End.")
                : NSLOCTEXT("ReditusBuffs", "RefreshSummary", "Restores a percentage of max MP at each full recipient Cycle End.");
        }

        if (Row.RuntimeEffect != E::Unwired)
        {
            if (Row.ApplicationChancePercent == 100.f) Row.ApplicationChancePercent = Seed.BaseChancePercent;
            if (Row.OwnerCycles == 3 && FMath::IsFinite(Row.Catalogue.Duration) &&
                Row.Catalogue.Duration >= 1.f && Row.Catalogue.Duration <= 99.f)
                Row.OwnerCycles = FMath::RoundToInt(Row.Catalogue.Duration);
            Row.Catalogue.bDesignOnly = false;
            Row.Catalogue.ImplementationStatus = TEXT("Runtime configured");
        }
        else if (Seed.LegacyBuffClass[0] != 0)
        {
            using D = EReditusLegacyBuffDuration;
            const bool bPoison = ID == TEXT("Poison");
            const bool bBlind = ID == TEXT("Blind");
            const auto* LegacyClass = ResolveLegacyBuffClass(Seed.LegacyBuffClass, Base);
            if (!LegacyClass)
            {
                UE_LOG(LogReditusGuardUpInstall, Warning, TEXT("DT_Buffs [%s]: expected buff class could not be loaded: %s"),
                    *ID.ToString(), Seed.LegacyBuffClass);
                return;
            }
            UseLegacyClass(Row, *LegacyClass->GetPathName(), bPoison ? D::UntilCured : (bBlind ? D::OwnerCycles : D::ClassDefault));
            if (ID == TEXT("EnemyTaunt"))
            {
                Row.Catalogue.AllowedSources = TEXT("Enemy");
                Row.Catalogue.AllowedTargets = TEXT("Player");
            }
            if (bPoison)
            {
                Row.Catalogue.ProposedBehavior = NSLOCTEXT("ReditusBuffs", "PoisonUntilCured", "Poison persists for party members until cured; uses the existing poison damage and cure.");
                Row.EffectSummary = Row.Catalogue.ProposedBehavior;
            }
            else if (bBlind)
            {
                Row.EffectSummary = NSLOCTEXT("ReditusBuffs", "TimedBlind", "Reduces accuracy for the selected recipient cycles.");
            }
            else
            {
                Row.Catalogue.DurationMode = TEXT("ClassDefault");
                Row.EffectSummary = ID == TEXT("Berserk")
                    ? NSLOCTEXT("ReditusBuffs", "BerserkLegacySummary", "Applies the original Berserk status using BP_BerserkBuff.")
                    : FText::FromString(Seed.ProposedBehavior);
            }
        }
        if (!OriginalSummary.IsEmpty()) Row.EffectSummary = OriginalSummary;
        if (Row.RuntimeEffect != E::PhysicalDefense && Row.PhysicalDefenseMultiplier == 1.5f)
            Row.PhysicalDefenseMultiplier = 1.f;
    }

    bool FindProjectBuffClasses(const UClass* Base, TArray<UClass*>& Classes)
    {
        auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
        if (Registry.IsLoadingAssets()) return false;
        TArray<FName> Parents;
        Parents.Add(Base->GetFName());
        TSet<FName> Excluded;
        TSet<FName> Derived;
        Registry.GetDerivedClassNames(Parents, Excluded, Derived);
        TArray<FAssetData> Blueprints;
        Registry.GetAssetsByClass(UBlueprint::StaticClass()->GetFName(), Blueprints, true);
        for (const auto& Asset : Blueprints)
        {
            FString ExportPath;
            if (!Asset.GetTagValue(FName(TEXT("GeneratedClass")), ExportPath)) continue;
            const FString ClassPath = FPackageName::ExportTextPathToObjectPath(ExportPath);
            if (!Derived.Contains(FName(*FPackageName::ObjectPathToObjectName(ClassPath)))) continue;
            if (auto* Class = ResolveLegacyBuffClass(*ClassPath, Base)) Classes.AddUnique(Class);
        }
        return true;
    }

    // Verified names in the project's Buffs folder. Registry discovery below can
    // add further children, but these rows do not depend on the discovery cache.
    void AddReviewedLegacyVariants(const UClass* Base, TArray<UClass*>& Classes, TSet<FString>& ReviewedPaths)
    {
        const TCHAR* Names[] = {
            TEXT("BP_AmpBerserkBuff"), TEXT("BP_AugustusOverdriveBlind"), TEXT("BP_BlackSprayBlind"), TEXT("BP_BoarChargeStun"),
            TEXT("BP_DeathBoarPoison"), TEXT("BP_DefenseIncrease"), TEXT("BP_Exhausted"),
            TEXT("BP_Flee"), TEXT("BP_PlayerTauntBuff"), TEXT("BP_ResetIndex"), TEXT("BP_RobotOnlyStun"), TEXT("BP_Swallow"),
            TEXT("BP_WindVortexBlind")
        };
        for (const TCHAR* Name : Names)
        {
            const FString Path = FString::Printf(TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Skills/Buffs/%s.%s_C"),
                Name, Name);
            ReviewedPaths.Add(Path);
            if (auto* Class = ResolveLegacyBuffClass(*Path, Base))
                Classes.AddUnique(Class);
            else
                UE_LOG(LogReditusGuardUpInstall, Warning, TEXT("DT_Buffs: reviewed legacy buff class not found: %s"), *Path);
        }
    }

    void KeepPlayerTauntInactive(FReditusTableBuffRow& Row)
    {
        // A named side other than Both, Enemy or Player is rejected by
        // MatchesCatalogueSide. The class is indexed but cannot be cast by this table.
        Row.Catalogue.AllowedSources = TEXT("Disabled");
        Row.Catalogue.ImplementationStatus = TEXT("Legacy Blueprint assigned; table application disabled");
    }

    void UpdateBuffCatalogue(bool bApply)
    {
        if (!GEditor || GEditor->PlayWorld)
        {
            UE_LOG(LogReditusGuardUpInstall, Error, TEXT("Stop Play before auditing or repairing DT_Buffs."));
            return;
        }
        auto* Buffs = LoadObject<UDataTable>(nullptr, BuffPath);
        if (!Buffs || Buffs->GetRowStruct() != FReditusTableBuffRow::StaticStruct() ||
            !Buffs->GetRowMap().Contains(BuffID))
        {
            UE_LOG(LogReditusGuardUpInstall, Error, TEXT("Expected the existing DT_Buffs with its Protect row. No rows were added."));
            return;
        }

        using E = EReditusTableBuffEffect;
        using D = EReditusLegacyBuffDuration;
        const auto* Base = LoadObject<UClass>(nullptr, LegacyBuffBasePath);
        if (!Base || !Base->IsChildOf(AActor::StaticClass()))
        {
            UE_LOG(LogReditusGuardUpInstall, Error, TEXT("DT_Buffs: could not load BP_BuffBase. No rows were changed."));
            return;
        }
        TArray<UClass*> LegacyClasses;
        if (!FindProjectBuffClasses(Base, LegacyClasses))
            UE_LOG(LogReditusGuardUpInstall, Display,
                TEXT("DT_Buffs: asset discovery is still running. Reviewed rows are available; rerun after discovery to pick up additional classes."));
        TSet<FString> ReviewedPaths;
        for (const auto& Seed : CatalogueSeeds)
            if (Seed.LegacyBuffClass[0] != 0) ReviewedPaths.Add(FString(Seed.LegacyBuffClass));
        AddReviewedLegacyVariants(Base, LegacyClasses, ReviewedPaths);
        TMap<FName, const UClass*> CanonicalClasses;
        for (const auto& Seed : CatalogueSeeds)
            if (Seed.LegacyBuffClass[0] != 0)
                if (auto* Class = ResolveLegacyBuffClass(Seed.LegacyBuffClass, Base))
                {
                    CanonicalClasses.Add(FName(Seed.ID), Class);
                    LegacyClasses.AddUnique(Class);
                }
        if (const auto* Class = ResolveLegacyBuffClass(LegacyBlindPath, Base))
            CanonicalClasses.Add(FName(TEXT("BlindUntilCured")), Class);
        LegacyClasses.Sort([](const UClass& A, const UClass& B) { return A.GetPathName() < B.GetPathName(); });

        TArray<TPair<FName, FReditusTableBuffRow>> Changes;
        TSet<FString> CoveredClasses;
        TSet<FName> PlannedIDs;
        TArray<FName> Pending;
        int32 Added = 0;
        const auto Stage = [&](FName ID, FReditusTableBuffRow Row)
        {
            if (Row.RuntimeEffect == E::LegacyBlueprint && !Row.LegacyEffectClass.IsNull())
                CoveredClasses.Add(Row.LegacyEffectClass.ToSoftObjectPath().ToString());
            if (!Buffs->GetRowMap().Contains(ID)) ++Added;
            PlannedIDs.Add(ID);
            Changes.Emplace(ID, MoveTemp(Row));
        };

        // A user-selected class in the actual runtime field takes precedence.
        // Upgrade these rows even if the importer did not create their IDs.
        for (const auto& Entry : Buffs->GetRowMap())
        {
            const auto* Existing = reinterpret_cast<const FReditusTableBuffRow*>(Entry.Value);
            if (!Existing || Existing->LegacyEffectClass.IsNull()) continue;
            const FString Path = Existing->LegacyEffectClass.ToSoftObjectPath().ToString();
            const auto* Class = ResolveLegacyBuffClass(*Path, Base);
            const FName ID = Entry.Key;
            const auto* Expected = CanonicalClasses.Find(ID);
            if (Expected && *Expected != Class) continue;
            if (Existing->RuntimeEffect == E::LegacyBlueprint)
            {
                if (Class)
                {
                    CoveredClasses.Add(Class->GetPathName());
                    if (Class->GetFName() == TEXT("BP_PlayerTauntBuff_C") &&
                        Existing->Catalogue.AllowedSources != TEXT("Disabled"))
                    {
                        FReditusTableBuffRow Row = *Existing;
                        KeepPlayerTauntInactive(Row);
                        Stage(ID, MoveTemp(Row));
                    }
                }
                continue;
            }
            if (Existing->RuntimeEffect != E::Unwired || !Class || ID == BuffID) continue;
            FReditusTableBuffRow Row = *Existing;
            const D Duration = ID == TEXT("Poison") || ID == TEXT("BlindUntilCured") ? D::UntilCured
                : ID == TEXT("Blind") ? D::OwnerCycles : Row.LegacyDuration;
            UseLegacyClass(Row, *Class->GetPathName(), Duration);
            if (Class->GetFName() == TEXT("BP_PlayerTauntBuff_C")) KeepPlayerTauntInactive(Row);
            if (ID == TEXT("EnemyTaunt"))
            {
                Row.Catalogue.AllowedSources = TEXT("Enemy");
                Row.Catalogue.AllowedTargets = TEXT("Player");
            }
            if (Row.EffectSummary.IsEmpty())
                Row.EffectSummary = FText::Format(NSLOCTEXT("ReditusBuffs", "SelectedLegacyClass", "Uses the existing Blueprint {0}."),
                    FText::FromString(Class->GetName()));
            Stage(ID, MoveTemp(Row));
        }

        for (const auto& Seed : CatalogueSeeds)
        {
            const FName ID(Seed.ID);
            const auto* Existing = Buffs->FindRow<FReditusTableBuffRow>(ID, TEXT("Buff catalogue repair"), false);
            const auto* Expected = CanonicalClasses.Find(ID);
            const bool bClassLinkNeedsRepair = Expected && Existing &&
                (Existing->LegacyEffectClass.ToSoftObjectPath() != FSoftObjectPath((*Expected)->GetPathName()) ||
                    (ID != BuffID && Existing->RuntimeEffect != E::LegacyBlueprint));
            if (PlannedIDs.Contains(ID) || (Existing && !bClassLinkNeedsRepair && Existing->RuntimeEffect != E::Unwired &&
                !(Existing->RuntimeEffect == E::LegacyBlueprint && Existing->LegacyEffectClass.IsNull()))) continue;
            FReditusTableBuffRow Row = Existing ? *Existing : Seed.MakeDesignRow();
            if (Row.DisplayName.IsEmpty()) Row.DisplayName = FText::FromString(Seed.DisplayName);
            if (ID == BuffID)
            {
                if (Expected)
                {
                    const FString Path = (*Expected)->GetPathName();
                    Row.LegacyEffectClass = TSoftClassPtr<AActor>(FSoftObjectPath(Path));
                    Row.Catalogue.LegacyBuffClass = Path;
                }
                if (Row.RuntimeEffect == E::Unwired) Row.RuntimeEffect = E::PhysicalDefense;
                Row.Catalogue.bDesignOnly = false;
                Row.Catalogue.ImplementationStatus = TEXT("Runtime configured");
                if (Row.EffectSummary.IsEmpty())
                    Row.EffectSummary = NSLOCTEXT("ReditusBuffs", "ProtectSummary", "Raises physical defense.");
            }
            else
            {
                // A LegacyBlueprint row with an empty class pin is still a placeholder.
                Row.RuntimeEffect = E::Unwired;
                ConfigureCatalogueEffect(ID, Seed, Base, Row);
            }
            if (Row.RuntimeEffect != E::Unwired || !Existing)
            {
                if (Row.RuntimeEffect == E::Unwired) Pending.Add(ID);
                Stage(ID, MoveTemp(Row));
            }
        }

        const FName PermanentBlindID(TEXT("BlindUntilCured"));
        const auto* ExistingBlind = Buffs->FindRow<FReditusTableBuffRow>(PermanentBlindID, TEXT("Buff catalogue repair"), false);
        const auto* ExpectedBlind = CanonicalClasses.Find(PermanentBlindID);
        if (!PlannedIDs.Contains(PermanentBlindID) && (!ExistingBlind || ExistingBlind->RuntimeEffect == E::Unwired ||
            (ExpectedBlind && (ExistingBlind->RuntimeEffect != E::LegacyBlueprint ||
                ExistingBlind->LegacyEffectClass.ToSoftObjectPath() != FSoftObjectPath((*ExpectedBlind)->GetPathName())))))
        {
            if (const auto* Class = ResolveLegacyBuffClass(LegacyBlindPath, Base))
            {
                FReditusTableBuffRow Row = ExistingBlind ? *ExistingBlind : FReditusTableBuffRow();
                if (Row.DisplayName.IsEmpty())
                    Row.DisplayName = NSLOCTEXT("ReditusBuffs", "PermanentBlind", "Blind (Until Cured)");
                if (Row.EffectSummary.IsEmpty())
                    Row.EffectSummary = NSLOCTEXT("ReditusBuffs", "PermanentBlindSummary", "Reduces accuracy. Persists for party members until cured.");
                Row.Catalogue.Category = TEXT("Harmful");
                Row.Catalogue.ResistanceStatusKey = TEXT("Blind");
                UseLegacyClass(Row, *Class->GetPathName(), D::UntilCured);
                Stage(PermanentBlindID, MoveTemp(Row));
            }
        }
        for (const UClass* Class : LegacyClasses)
        {
            const FString Path = Class->GetPathName();
            if (CoveredClasses.Contains(Path)) continue;
            FString ClassName = Class->GetName();
            ClassName.RemoveFromStart(TEXT("BP_"));
            ClassName.RemoveFromEnd(TEXT("_C"));
            FName ID(*(TEXT("Legacy_") + ClassName));
            int32 Suffix = 1;
            const FReditusTableBuffRow* Placeholder = nullptr;
            for (;;)
            {
                Placeholder = Buffs->FindRow<FReditusTableBuffRow>(ID, TEXT("Legacy buff row"), false);
                const bool bCanRepair = !Placeholder || (Placeholder->LegacyEffectClass.IsNull() &&
                    (Placeholder->RuntimeEffect == E::Unwired || Placeholder->RuntimeEffect == E::LegacyBlueprint));
                if (!PlannedIDs.Contains(ID) && bCanRepair) break;
                ID = FName(*FString::Printf(TEXT("Legacy_%s_%d"), *ClassName, Suffix++));
            }

            FReditusTableBuffRow Row = Placeholder ? *Placeholder : FReditusTableBuffRow();
            const FText OriginalSummary = Row.EffectSummary;
            if (Row.DisplayName.IsEmpty())
                Row.DisplayName = FText::FromString(FName::NameToDisplayString(ClassName, false));
            Row.EffectSummary = FText::Format(NSLOCTEXT("ReditusBuffs", "ImportedClassSummary", "Existing Blueprint: {0}. Uses its class duration."),
                Row.DisplayName);
            Row.Catalogue.DurationMode = TEXT("ClassDefault");
            UseLegacyClass(Row, *Path, EReditusLegacyBuffDuration::ClassDefault);
            if (ClassName.Contains(TEXT("Blind")))
            {
                Row.LegacyImmunityClass = TSoftClassPtr<AActor>(FSoftObjectPath(LegacyBlindPath));
                Row.Catalogue.ResistanceStatusKey = TEXT("Blind");
                Row.EffectSummary = NSLOCTEXT("ReditusBuffs", "LegacyBlindVariant", "Reduces accuracy using the original Blind variant.");
            }
            else if (ClassName.Contains(TEXT("Poison")))
            {
                Row.LegacyImmunityClass = TSoftClassPtr<AActor>(FSoftObjectPath(LegacyPoisonPath));
                Row.Catalogue.ResistanceStatusKey = TEXT("Poison");
                UseLegacyClass(Row, *Path, EReditusLegacyBuffDuration::UntilCured);
                Row.EffectSummary = NSLOCTEXT("ReditusBuffs", "LegacyPoisonVariant", "Applies the original Poison variant until cured.");
            }
            else if (ClassName.Contains(TEXT("Stun")))
                Row.EffectSummary = NSLOCTEXT("ReditusBuffs", "LegacyStunVariant", "Applies the original stun and speed rules for this variant.");
            else if (ClassName == TEXT("Swallow"))
                Row.EffectSummary = NSLOCTEXT("ReditusBuffs", "LegacySwallow", "Hides and disables the swallowed unit through the original Swallow behavior.");
            else if (ClassName == TEXT("DefenseIncrease"))
                Row.EffectSummary = NSLOCTEXT("ReditusBuffs", "LegacyDefenseIncrease", "Raises physical defense through the original buff Blueprint.");
            else if (ClassName == TEXT("Exhausted"))
                Row.EffectSummary = NSLOCTEXT("ReditusBuffs", "LegacyExhausted", "Applies the original Exhausted state and its stat and AMP rules.");
            else if (ClassName == TEXT("Flee"))
                Row.EffectSummary = NSLOCTEXT("ReditusBuffs", "LegacyFlee", "Battle utility: triggers the existing flee outcome.");
            else if (ClassName == TEXT("ResetIndex"))
                Row.EffectSummary = NSLOCTEXT("ReditusBuffs", "LegacyResetIndex", "Battle utility: marks the target turn as skipped.");
            else if (ClassName == TEXT("PlayerTauntBuff"))
            {
                KeepPlayerTauntInactive(Row);
                Row.EffectSummary = NSLOCTEXT("ReditusBuffs", "LegacyPlayerTaunt", "Original player Taunt Blueprint. Table application is disabled; enemy Taunt uses EnemyTaunt.");
            }
            else if (ClassName.Contains(TEXT("Berserk")))
                Row.EffectSummary = NSLOCTEXT("ReditusBuffs", "LegacyBerserkVariant", "Applies the original berserk status for this variant.");
            if (!OriginalSummary.IsEmpty()) Row.EffectSummary = OriginalSummary;
            Stage(ID, MoveTemp(Row));
        }

        for (const auto& Entry : Buffs->GetRowMap())
        {
            if (PlannedIDs.Contains(Entry.Key)) continue;
            const auto* Row = reinterpret_cast<const FReditusTableBuffRow*>(Entry.Value);
            if (!Row || Row->RuntimeEffect == E::Unwired ||
                (Row->RuntimeEffect == E::LegacyBlueprint && !ResolveLegacyBuffClass(
                    *Row->LegacyEffectClass.ToSoftObjectPath().ToString(), Base)))
                Pending.Add(Entry.Key);
        }
        for (const auto& Change : Changes)
        {
            const auto& Row = Change.Value;
            if (Row.RuntimeEffect == E::Unwired) continue;
            const FString Handler = Row.RuntimeEffect == E::LegacyBlueprint
                ? Row.LegacyEffectClass.ToSoftObjectPath().ToString()
                : StaticEnum<E>()->GetNameStringByValue(static_cast<int64>(Row.RuntimeEffect));
            UE_LOG(LogReditusGuardUpInstall, Display, TEXT("DT_Buffs [%s]: %s -> %s"),
                *Change.Key.ToString(), Buffs->GetRowMap().Contains(Change.Key) ? TEXT("repair") : TEXT("add"), *Handler);
        }
        if (Pending.Num())
        {
            TArray<FString> Names;
            for (FName ID : Pending) Names.Add(ID.ToString());
            Names.Sort();
            UE_LOG(LogReditusGuardUpInstall, Warning, TEXT("DT_Buffs: %d rows still have no usable handler: %s"),
                Pending.Num(), *FString::Join(Names, TEXT(", ")));
        }
        const auto ReportLegacyCoverage = [&](const TSet<FString>& Assigned)
        {
            int32 Linked = 0;
            for (const FString& Path : ReviewedPaths)
            {
                const auto* Class = ResolveLegacyBuffClass(*Path, Base);
                if (Class && Assigned.Contains(Class->GetPathName()))
                    ++Linked;
                else
                    UE_LOG(LogReditusGuardUpInstall, Warning, TEXT("DT_Buffs: no legacy row assigned to %s"), *Path);
            }
            UE_LOG(LogReditusGuardUpInstall, Display, TEXT("DT_Buffs: %d/%d reviewed legacy Blueprint classes assigned%s."),
                Linked, ReviewedPaths.Num(), bApply ? TEXT("") : TEXT(" in repair preview"));
        };
        const auto ReadLegacyCoverage = [&]()
        {
            TSet<FString> Assigned;
            for (const auto& Entry : Buffs->GetRowMap())
            {
                const auto* Row = reinterpret_cast<const FReditusTableBuffRow*>(Entry.Value);
                if (!Row || Row->RuntimeEffect != E::LegacyBlueprint || Row->LegacyEffectClass.IsNull()) continue;
                if (const auto* Class = ResolveLegacyBuffClass(*Row->LegacyEffectClass.ToSoftObjectPath().ToString(), Base))
                    Assigned.Add(Class->GetPathName());
            }
            return Assigned;
        };
        if (!bApply)
        {
            ReportLegacyCoverage(CoveredClasses);
            UE_LOG(LogReditusGuardUpInstall, Display,
                TEXT("DT_Buffs audit: %d rows to add, %d to repair. No changes made."), Added, Changes.Num() - Added);
            return;
        }
        if (!Changes.Num())
        {
            ReportLegacyCoverage(ReadLegacyCoverage());
            UE_LOG(LogReditusGuardUpInstall, Display, TEXT("DT_Buffs: no additional handlers to install. No rows changed."));
            return;
        }

        const FScopedTransaction Transaction(NSLOCTEXT("ReditusBuffs", "PopulateBuffCatalogue", "Repair buff catalogue rows"));
        if (!Transaction.IsOutstanding())
        {
            UE_LOG(LogReditusGuardUpInstall, Error, TEXT("Unable to open an undo transaction. No rows were changed."));
            return;
        }
        Buffs->Modify();
        for (const auto& Change : Changes)
            Buffs->AddRow(Change.Key, Change.Value);
        Buffs->PostEditChange();
        Buffs->MarkPackageDirty();
        ReportLegacyCoverage(ReadLegacyCoverage());
        UE_LOG(LogReditusGuardUpInstall, Display, TEXT("DT_Buffs: added %d rows and repaired %d. Open DT_Buffs to verify, then save it."),
            Added, Changes.Num() - Added);
    }

    void PopulateBuffCatalogue()
    {
        UpdateBuffCatalogue(true);
    }

    void AuditBuffCatalogue()
    {
        UpdateBuffCatalogue(false);
    }

    FAutoConsoleCommand GAuditBuffCatalogue(
        TEXT("Reditus.AuditBuffCatalogue"),
        TEXT("Preview repairs for DT_Buffs and list rows without a runtime handler. Stop Play first."),
        FConsoleCommandDelegate::CreateStatic(&AuditBuffCatalogue));

    FAutoConsoleCommand GRepairBuffCatalogue(
        TEXT("Reditus.RepairBuffCatalogue"),
        TEXT("Repair placeholder rows in the existing DT_Buffs. Stop Play first."),
        FConsoleCommandDelegate::CreateStatic(&PopulateBuffCatalogue));

    FAutoConsoleCommand GPopulateBuffCatalogue(
        TEXT("Reditus.PopulateBuffCatalogue"),
        TEXT("Add or upgrade the Reditus buff catalogue in the installed DT_Buffs. Stop Play first."),
        FConsoleCommandDelegate::CreateStatic(&PopulateBuffCatalogue));
}

IMPLEMENT_MODULE(FDefaultModuleImpl, ReditusTableBuffsEditor)
