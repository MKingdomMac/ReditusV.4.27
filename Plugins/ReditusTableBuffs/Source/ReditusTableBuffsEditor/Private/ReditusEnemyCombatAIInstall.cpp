#include "ReditusEnemyCombatAIComponent.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ClassDynamicCast.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Knot.h"
#include "K2Node_Self.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompiler.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogReditusEnemyAIInstall, Log, All);

namespace ReditusEnemyAIInstall
{
    const TCHAR* EnemyBlueprintPath = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Units/EnemyUnits/BP_EnemyUnitBase.BP_EnemyUnitBase");
    const TCHAR* EnemyTablePath = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_Enemies.DT_Enemies");
    const TCHAR* ProfilesPackage = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_EnemyAIProfiles");
    const TCHAR* ProfilesPath = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_EnemyAIProfiles.DT_EnemyAIProfiles");
    const TCHAR* ProtectSkillPath = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Skills/BuffSkills/BP_ProtectSelfBuff.BP_ProtectSelfBuff_C");
    const FName EnemyCombatAIName(TEXT("EnemyCombatAI"));

    UEdGraphPin* Pin(UEdGraphNode* Node, FName Name)
    {
        UEdGraphPin* Result = Node ? Node->FindPin(Name) : nullptr;
        return Result && !Result->bOrphanedPin ? Result : nullptr;
    }

    UEdGraphPin* OneLink(const UEdGraphPin* Input)
    {
        return Input && Input->LinkedTo.Num() == 1 ? Input->LinkedTo[0] : nullptr;
    }

    UEdGraphPin* TraceSource(UEdGraphPin* Input)
    {
        TSet<UEdGraphNode*> Seen;
        UEdGraphPin* Link = OneLink(Input);
        while (Link)
        {
            auto* Reroute = Cast<UK2Node_Knot>(Link->GetOwningNode());
            if (!Reroute) return Link->Direction == EGPD_Output ? Link : nullptr;
            if (Seen.Contains(Reroute)) return nullptr;
            Seen.Add(Reroute);
            Link = OneLink(Pin(Reroute, TEXT("InputPin")));
        }
        return nullptr;
    }

    bool IsCall(const UEdGraphNode* Node, FName Name)
    {
        const auto* Call = Cast<UK2Node_CallFunction>(Node);
        return Call && Call->FunctionReference.GetMemberName() == Name;
    }

    bool IsSelf(const UEdGraphPin* Output)
    {
        return Output && Output->GetOwningNode()->IsA<UK2Node_Self>();
    }

    UEdGraph* FindFunctionGraph(UBlueprint* Blueprint, FName Name)
    {
        for (UEdGraph* Graph : Blueprint->FunctionGraphs)
            if (Graph && Graph->GetFName() == Name) return Graph;
        return nullptr;
    }

    struct FClassSkillSite
    {
        UEdGraphPin* Entry = nullptr;
        UEdGraphPin* OriginalNext = nullptr;
        UEdGraphPin* Return = nullptr;
        UK2Node_VariableGet* SkillGet = nullptr;
        UClass* SkillType = nullptr;
        UK2Node_CallFunction* OldHook = nullptr;
        UK2Node_IfThenElse* OldBranch = nullptr;
        bool bInstalled = false;
    };

    bool FindClassSkillSite(UEdGraph* Graph, FName HookName, FName OldHookName, FClassSkillSite& Site)
    {
        if (!Graph) return false;
        UK2Node_FunctionEntry* Entry = nullptr;
        UK2Node_FunctionResult* Result = nullptr;
        UK2Node_CallFunction* Hook = nullptr;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (auto* Candidate = Cast<UK2Node_FunctionEntry>(Node))
            {
                if (Entry) return false;
                Entry = Candidate;
            }
            if (auto* Candidate = Cast<UK2Node_FunctionResult>(Node))
            {
                if (Result) return false;
                Result = Candidate;
            }
            if (IsCall(Node, HookName) || IsCall(Node, OldHookName))
            {
                if (Hook) return false;
                Hook = CastChecked<UK2Node_CallFunction>(Node);
            }
        }
        Site.Entry = Pin(Entry, UEdGraphSchema_K2::PN_Then);
        Site.OriginalNext = OneLink(Site.Entry);
        Site.Return = Pin(Result, UEdGraphSchema_K2::PN_Execute);
        auto* Skill = TraceSource(Pin(Result, TEXT("skill")));
        Site.SkillGet = Skill ? Cast<UK2Node_VariableGet>(Skill->GetOwningNode()) : nullptr;
        Site.SkillType = Skill ? Cast<UClass>(Skill->PinType.PinSubCategoryObject.Get()) : nullptr;
        if (!Site.OriginalNext || !Site.Return || Site.Return->LinkedTo.Num() == 0 || !Site.SkillGet ||
            !Site.SkillType || !Site.SkillType->IsChildOf(AActor::StaticClass()) ||
            Skill->PinType.PinCategory != UEdGraphSchema_K2::PC_Class ||
            Site.SkillGet->VariableReference.GetMemberName() != TEXT("currentSkill")) return false;
        if (!Hook) return Site.OriginalNext != Site.Return;

        auto* AfterHook = OneLink(Pin(Hook, UEdGraphSchema_K2::PN_Then));
        auto* Branch = AfterHook ? Cast<UK2Node_IfThenElse>(AfterHook->GetOwningNode()) : nullptr;
        auto* Fallback = OneLink(Pin(Branch, UEdGraphSchema_K2::PN_Else));
        if (Site.OriginalNext != Pin(Hook, UEdGraphSchema_K2::PN_Execute) ||
            !IsSelf(TraceSource(Pin(Hook, TEXT("Enemy")))) || !Branch ||
            AfterHook != Pin(Branch, UEdGraphSchema_K2::PN_Execute) || !Fallback || Fallback == Site.Return ||
            !Pin(Hook, UEdGraphSchema_K2::PN_ReturnValue) ||
            TraceSource(Pin(Branch, TEXT("Condition"))) != Pin(Hook, UEdGraphSchema_K2::PN_ReturnValue)) return false;

        auto* ProfiledPath = OneLink(Pin(Branch, UEdGraphSchema_K2::PN_Then));
        if (IsCall(Hook, OldHookName))
        {
            if (ProfiledPath != Site.Return) return false;
            Site.OldHook = Hook;
            Site.OldBranch = Branch;
            return true;
        }

        auto* SetSkill = ProfiledPath ? Cast<UK2Node_VariableSet>(ProfiledPath->GetOwningNode()) : nullptr;
        if (!SetSkill || ProfiledPath != Pin(SetSkill, UEdGraphSchema_K2::PN_Execute) ||
            OneLink(Pin(SetSkill, UEdGraphSchema_K2::PN_Then)) != Site.Return) return false;
        const auto& SkillReference = Site.SkillGet->VariableReference;
        const auto& SetReference = SetSkill->VariableReference;
        auto* SelectedSkillPin = Pin(Hook, TEXT("SelectedSkill"));
        auto* ConvertedSkill = TraceSource(Pin(SetSkill, SkillReference.GetMemberName()));
        auto* ClassCast = ConvertedSkill ? Cast<UK2Node_ClassDynamicCast>(ConvertedSkill->GetOwningNode()) : nullptr;
        Site.bInstalled = SetReference.GetMemberName() == SkillReference.GetMemberName() &&
            SetReference.GetMemberGuid() == SkillReference.GetMemberGuid() &&
            SetReference.IsLocalScope() == SkillReference.IsLocalScope() &&
            ClassCast && ClassCast->IsNodePure() && ClassCast->TargetType == Site.SkillType && SelectedSkillPin &&
            TraceSource(Pin(ClassCast, TEXT("Class"))) == SelectedSkillPin;
        return Site.bInstalled;
    }

    struct FIDWeightSite
    {
        UEdGraphPin* Weight = nullptr;
        UEdGraphPin* SkillID = nullptr;
        TArray<UEdGraphPin*> Consumers;
        bool bInstalled = false;
    };

    bool FindIDWeightSite(UEdGraph* Graph, FName HookName, FIDWeightSite& Site)
    {
        if (!Graph) return false;
        UK2Node_CallFunction* Hook = nullptr;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (IsCall(Node, HookName))
            {
                if (Hook) return false;
                Hook = CastChecked<UK2Node_CallFunction>(Node);
            }
            if (!IsCall(Node, TEXT("Map_Find"))) continue;
            const auto* Map = TraceSource(Pin(Node, TEXT("TargetMap")));
            const auto* Get = Map ? Cast<UK2Node_VariableGet>(Map->GetOwningNode()) : nullptr;
            if (!Get) continue;
            const FName Member = Get->VariableReference.GetMemberName();
            if (Member != TEXT("Enemy Skill Weights by ID") && Member != TEXT("EnemySkillWeightsByID")) continue;
            if (Site.Weight) return false;
            Site.Weight = Pin(Node, TEXT("Value"));
            Site.SkillID = TraceSource(Pin(Node, TEXT("Key")));
        }
        if (!Site.Weight || Site.Weight->PinType.PinCategory != UEdGraphSchema_K2::PC_Float ||
            !Site.SkillID || Site.SkillID->PinType.PinCategory != UEdGraphSchema_K2::PC_Name) return false;

        auto* Output = Site.Weight;
        if (Hook)
        {
            Site.bInstalled = IsSelf(TraceSource(Pin(Hook, TEXT("Enemy")))) &&
                TraceSource(Pin(Hook, TEXT("SkillID"))) == Site.SkillID &&
                OneLink(Site.Weight) == Pin(Hook, TEXT("OriginalWeight"));
            if (!Site.bInstalled) return false;
            Output = Pin(Hook, UEdGraphSchema_K2::PN_ReturnValue);
        }
        if (!Output) return false;

        bool bHasPositiveWeightGate = false;
        for (auto* Consumer : Output->LinkedTo)
        {
            if (!Consumer || Consumer->Direction != EGPD_Input || Consumer->bOrphanedPin) return false;
            Site.Consumers.Add(Consumer);
            bHasPositiveWeightGate |= IsCall(Consumer->GetOwningNode(), TEXT("Greater_FloatFloat"));
        }
        return bHasPositiveWeightGate && Site.Consumers.Num() >= 3;
    }

    bool Link(UEdGraphPin* From, UEdGraphPin* To)
    {
        if (!From || !To) return false;
        UEdGraph* Graph = From->GetOwningNode()->GetGraph();
        const auto* Schema = Cast<UEdGraphSchema_K2>(Graph->GetSchema());
        return Schema && Schema->TryCreateConnection(From, To);
    }

    template <class T> T* AddNode(UEdGraph* Graph, int32 X, int32 Y)
    {
        auto* Node = NewObject<T>(Graph, NAME_None, RF_Transactional);
        Graph->AddNode(Node, false, false);
        Node->CreateNewGuid();
        Node->NodePosX = X;
        Node->NodePosY = Y;
        return Node;
    }

    UK2Node_CallFunction* AddCallWithSelf(UEdGraph* Graph, UFunction* Function, int32 X, int32 Y,
        UEdGraphPin* SelfPin = nullptr)
    {
        auto* Call = AddNode<UK2Node_CallFunction>(Graph, X, Y);
        Call->SetFromFunction(Function);
        Call->AllocateDefaultPins();
        if (!SelfPin)
        {
            auto* Self = AddNode<UK2Node_Self>(Graph, X - 155, Y + 110);
            Self->AllocateDefaultPins();
            SelfPin = Pin(Self, UEdGraphSchema_K2::PN_Self);
        }
        if (!SelfPin) return nullptr;
        SelfPin->GetOwningNode()->Modify();
        return Link(SelfPin, Pin(Call, TEXT("Enemy"))) ? Call : nullptr;
    }

    bool WireClassSkill(const FClassSkillSite& Site, UFunction* Function)
    {
        if (Site.bInstalled) return true;
        auto* EntryNode = Site.Entry->GetOwningNode();
        auto* Graph = EntryNode->GetGraph();
        Graph->Modify();
        EntryNode->Modify();
        Site.OriginalNext->GetOwningNode()->Modify();
        Site.Return->GetOwningNode()->Modify();

        const int32 X = Site.OldHook ? Site.OldHook->NodePosX : EntryNode->NodePosX + 250;
        const int32 Y = Site.OldHook ? Site.OldHook->NodePosY : EntryNode->NodePosY - 220;
        auto* Call = AddCallWithSelf(Graph, Function, X, Y, TraceSource(Pin(Site.OldHook, TEXT("Enemy"))));
        if (!Call) return false;

        auto* Branch = Site.OldBranch;
        if (Branch) Branch->Modify();
        else
        {
            Branch = AddNode<UK2Node_IfThenElse>(Graph, X + 275, Y);
            Branch->AllocateDefaultPins();
        }
        auto* ClassCast = AddNode<UK2Node_ClassDynamicCast>(Graph, Branch->NodePosX, Branch->NodePosY - 135);
        ClassCast->TargetType = Site.SkillType;
        ClassCast->AllocateDefaultPins();
        if (!ClassCast->IsNodePure()) ClassCast->SetPurity(true);
        if (!ClassCast->IsNodePure()) return false;
        UEdGraphPin* CastResult = nullptr;
        for (auto* Candidate : ClassCast->Pins)
        {
            if (Candidate && !Candidate->bOrphanedPin && Candidate->Direction == EGPD_Output &&
                Candidate->PinType.PinCategory == UEdGraphSchema_K2::PC_Class)
            {
                CastResult = Candidate;
                break;
            }
        }

        auto* SetSkill = AddNode<UK2Node_VariableSet>(Graph, Branch->NodePosX + 270, Branch->NodePosY);
        SetSkill->VariableReference = Site.SkillGet->VariableReference;
        SetSkill->AllocateDefaultPins();
        if (!Link(Pin(Call, TEXT("SelectedSkill")), Pin(ClassCast, TEXT("Class"))) ||
            !Link(CastResult, Pin(SetSkill, SetSkill->VariableReference.GetMemberName())) ||
            !Link(Pin(SetSkill, UEdGraphSchema_K2::PN_Then), Site.Return)) return false;

        if (Site.OldHook)
        {
            Site.OldHook->Modify();
            Pin(Branch, UEdGraphSchema_K2::PN_Then)->BreakLinkTo(Site.Return);
            Site.OldHook->DestroyNode();
        }
        else Site.Entry->BreakLinkTo(Site.OriginalNext);

        return Link(Site.Entry, Pin(Call, UEdGraphSchema_K2::PN_Execute)) &&
            Link(Pin(Call, UEdGraphSchema_K2::PN_Then), Pin(Branch, UEdGraphSchema_K2::PN_Execute)) &&
            Link(Pin(Call, UEdGraphSchema_K2::PN_ReturnValue), Pin(Branch, TEXT("Condition"))) &&
            Link(Pin(Branch, UEdGraphSchema_K2::PN_Then), Pin(SetSkill, UEdGraphSchema_K2::PN_Execute)) &&
            (Site.OldHook || Link(Pin(Branch, UEdGraphSchema_K2::PN_Else), Site.OriginalNext));
    }

    bool WireIDWeight(const FIDWeightSite& Site, UFunction* Function)
    {
        if (Site.bInstalled) return true;
        auto* Previous = Site.Weight->GetOwningNode();
        auto* Graph = Previous->GetGraph();
        Graph->Modify();
        Previous->Modify();
        Site.SkillID->GetOwningNode()->Modify();
        auto* Call = AddCallWithSelf(Graph, Function, Previous->NodePosX + 240, Previous->NodePosY + 140);
        if (!Call || !Link(Site.SkillID, Pin(Call, TEXT("SkillID"))) ||
            !Link(Site.Weight, Pin(Call, TEXT("OriginalWeight")))) return false;
        for (auto* Consumer : Site.Consumers)
        {
            Consumer->GetOwningNode()->Modify();
            Site.Weight->BreakLinkTo(Consumer);
            if (!Link(Pin(Call, UEdGraphSchema_K2::PN_ReturnValue), Consumer)) return false;
        }
        return true;
    }

    bool FindComponent(UBlueprint* Blueprint, UReditusEnemyCombatAIComponent*& Component)
    {
        if (!Blueprint->SimpleConstructionScript) return false;
        for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
        {
            if (!Node) continue;
            if (auto* Found = Cast<UReditusEnemyCombatAIComponent>(Node->ComponentTemplate))
            {
                if (Component) return false;
                Component = Found;
            }
            else if (Node->GetVariableName() == EnemyCombatAIName) return false;
        }
        return Component || !Blueprint->GeneratedClass || !FindFProperty<FProperty>(Blueprint->GeneratedClass, EnemyCombatAIName);
    }

    UDataTable* FindOrCreateProfiles()
    {
        UObject* Existing = LoadObject<UObject>(nullptr, ProfilesPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (Existing)
        {
            auto* Table = Cast<UDataTable>(Existing);
            return Table && Table->GetRowStruct() == FReditusEnemyAIProfile::StaticStruct() ? Table : nullptr;
        }
        if (FPackageName::DoesPackageExist(ProfilesPackage)) return nullptr;
        auto* Package = CreatePackage(ProfilesPackage);
        auto* Table = NewObject<UDataTable>(Package, FName(TEXT("DT_EnemyAIProfiles")), RF_Public | RF_Standalone | RF_Transactional);
        Table->RowStruct = FReditusEnemyAIProfile::StaticStruct();
        if (auto* Skill = LoadClass<AActor>(nullptr, ProtectSkillPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
        {
            FReditusEnemyAISkillRule Rule;
            Rule.SkillClass = Skill;
            Rule.SkillID = TEXT("ProtectSelfBuff");
            Rule.Condition = EReditusEnemyAISkillCondition::SkipWhileStatusActive;
            Rule.StatusFlag = TEXT("IsProtected");
            Rule.StatusBuffID = TEXT("Protect");
            FReditusEnemyAIProfile Profile;
            Profile.SkillRules.Add(Rule);
            Table->AddRow(TEXT("LizardMan"), Profile);
        }
        FAssetRegistryModule::AssetCreated(Table);
        Table->MarkPackageDirty();
        return Table;
    }

    FReditusEnemyAIProfile MakeProtectExample()
    {
        FReditusEnemyAISkillRule Protect;
        Protect.SkillID = TEXT("ProtectSelfBuff");
        Protect.SkillClass = LoadClass<AActor>(nullptr, ProtectSkillPath, nullptr, LOAD_NoWarn | LOAD_Quiet);

        FReditusEnemyAISkillRule Require = Protect;
        FReditusEnemyAIUnitTest HP;
        HP.Type = EReditusEnemyAITestType::HPPercent;
        HP.Value = 50.f;
        FReditusEnemyAIUnitTest Stamina = HP;
        Stamina.Type = EReditusEnemyAITestType::StaminaPercent;
        FReditusEnemyAIUnitCheck CasterHealth;
        CasterHealth.AllTests = {HP, Stamina};
        FReditusEnemyAIConditionGroup HealthGroup;
        HealthGroup.AllChecks.Add(CasterHealth);

        FReditusEnemyAIUnitCheck Opponents;
        Opponents.Subject = EReditusEnemyAISubject::LivingOpponents;
        Opponents.Count = 2;
        FReditusEnemyAIConditionGroup OpponentGroup;
        OpponentGroup.AllChecks.Add(Opponents);
        Require.AnyOf = {HealthGroup, OpponentGroup};

        FReditusEnemyAISkillRule Skip = Protect;
        Skip.ConditionEffect = EReditusEnemyAIRuleEffect::SkipOnMatch;
        FReditusEnemyAIUnitTest Protected;
        Protected.Type = EReditusEnemyAITestType::StatusPresent;
        Protected.StatusFlag = TEXT("IsProtected");
        Protected.StatusBuffID = TEXT("Protect");
        FReditusEnemyAIConditionGroup ProtectionGroup;
        FReditusEnemyAIUnitCheck CasterProtection;
        CasterProtection.AllTests.Add(Protected);
        ProtectionGroup.AllChecks.Add(CasterProtection);
        Skip.AnyOf.Add(ProtectionGroup);

        FReditusEnemyAISkillRule Prefer = Protect;
        Prefer.ConditionEffect = EReditusEnemyAIRuleEffect::AdjustWeight;
        Prefer.WeightMultiplier = 3.f;
        FReditusEnemyAIConditionGroup LowHealthGroup = HealthGroup;
        HP.Value = 25.f;
        LowHealthGroup.AllChecks[0].AllTests = {HP};
        Prefer.AnyOf.Add(LowHealthGroup);

        FReditusEnemyAIProfile Profile;
        Profile.SkillRules = {Require, Skip, Prefer};
        return Profile;
    }

    FReditusEnemyAIProfile MakeSelfHealExample()
    {
        FReditusEnemyAIUnitTest LowHP;
        LowHP.Type = EReditusEnemyAITestType::HPPercent;
        LowHP.Value = 40.f;
        FReditusEnemyAIUnitCheck Caster;
        Caster.AllTests.Add(LowHP);
        FReditusEnemyAIConditionGroup WhenHurt;
        WhenHurt.AllChecks.Add(Caster);
        FReditusEnemyAISkillRule Heal;
        Heal.AnyOf.Add(WhenHurt);
        // The designer chooses an existing self-heal class or Skill ID.
        FReditusEnemyAIProfile Profile;
        Profile.SkillRules.Add(Heal);
        return Profile;
    }

    FReditusEnemyAIProfile MakeStaggerFinisherExample()
    {
        FReditusEnemyAIUnitTest Stagger;
        Stagger.Type = EReditusEnemyAITestType::Staggered;
        FReditusEnemyAIUnitCheck BrokenOpponent;
        BrokenOpponent.Subject = EReditusEnemyAISubject::LivingOpponents;
        BrokenOpponent.AllTests.Add(Stagger);

        FReditusEnemyAIUnitTest RageBait;
        RageBait.Type = EReditusEnemyAITestType::StatusPresent;
        RageBait.StatusBuffID = TEXT("AggroFocus");
        FReditusEnemyAIUnitCheck FocusedOpponents = BrokenOpponent;
        FocusedOpponents.CountComparison = EReditusEnemyAIComparison::Exactly;
        FocusedOpponents.Count = 0;
        FocusedOpponents.AllTests = {RageBait};

        FReditusEnemyAIConditionGroup Opening;
        Opening.AllChecks = {BrokenOpponent, FocusedOpponents};
        FReditusEnemyAISkillRule Finisher;
        Finisher.AnyOf.Add(Opening);
        FReditusEnemyAIProfile Profile;
        Profile.SkillRules.Add(Finisher);
        return Profile;
    }

    FReditusEnemyAIProfile MakeRegularAttackAmpExample()
    {
        FReditusEnemyAIAmpRule Attack;
        Attack.Action = EReditusEnemyAIAmpAction::RegularAttack;
        Attack.Points = 1;
        FReditusEnemyAIProfile Profile;
        Profile.bEnableAMP = true;
        Profile.AmpRules.Add(Attack);
        return Profile;
    }

    void AddExampleProfiles(UDataTable* Table)
    {
        const FName ProtectID(TEXT("Example_ProtectSelf"));
        const FName HealID(TEXT("Example_SelfHeal"));
        const FName FinisherID(TEXT("Example_StaggerFinisher"));
        const FName AttackAmpID(TEXT("Example_RegularAttackAMP"));
        const bool bNeedsProtect = !Table->FindRow<FReditusEnemyAIProfile>(ProtectID, TEXT("Enemy AI examples"), false);
        const bool bNeedsHeal = !Table->FindRow<FReditusEnemyAIProfile>(HealID, TEXT("Enemy AI examples"), false);
        const bool bNeedsFinisher = !Table->FindRow<FReditusEnemyAIProfile>(FinisherID, TEXT("Enemy AI examples"), false);
        const bool bNeedsAttackAmp = !Table->FindRow<FReditusEnemyAIProfile>(AttackAmpID, TEXT("Enemy AI examples"), false);
        if (!bNeedsProtect && !bNeedsHeal && !bNeedsFinisher && !bNeedsAttackAmp) return;

        const FScopedTransaction Transaction(NSLOCTEXT("ReditusEnemyAI", "AddExampleProfiles", "Add Enemy AI Example Profiles"));
        if (!Transaction.IsOutstanding()) return;
        Table->Modify();
        if (bNeedsProtect) Table->AddRow(ProtectID, MakeProtectExample());
        if (bNeedsHeal) Table->AddRow(HealID, MakeSelfHealExample());
        if (bNeedsFinisher) Table->AddRow(FinisherID, MakeStaggerFinisherExample());
        if (bNeedsAttackAmp) Table->AddRow(AttackAmpID, MakeRegularAttackAmpExample());
        Table->PostEditChange();
        Table->MarkPackageDirty();
        UE_LOG(LogReditusEnemyAIInstall, Display,
            TEXT("Enemy AI examples are available in %s. Look for the Example_ rows. Save the table to keep new rows."),
            *Table->GetName());
    }

    bool Compile(UBlueprint* Blueprint)
    {
        FCompilerResultsLog Log;
        FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Log);
        return Log.NumErrors == 0 && Blueprint->Status != BS_Error;
    }

    void Install()
    {
        if (!GEditor || GEditor->PlayWorld)
        {
            UE_LOG(LogReditusEnemyAIInstall, Error, TEXT("Stop Play before running Reditus.InstallEnemyCombatAI."));
            return;
        }
        auto* Blueprint = LoadObject<UBlueprint>(nullptr, EnemyBlueprintPath);
        auto* AIClass = UReditusEnemyCombatAIComponent::StaticClass();
        const FName ClassHook = GET_FUNCTION_NAME_CHECKED(UReditusEnemyCombatAIComponent, TrySelectProfiledClassSkill);
        const FName OldClassHook = GET_FUNCTION_NAME_CHECKED(UReditusEnemyCombatAIComponent, TryChooseProfiledClassSkill);
        const FName IDHook = GET_FUNCTION_NAME_CHECKED(UReditusEnemyCombatAIComponent, FilterEnemySkillIDWeight);
        UFunction* ClassFunction = AIClass->FindFunctionByName(ClassHook);
        UFunction* IDFunction = AIClass->FindFunctionByName(IDHook);
        FClassSkillSite ClassSite;
        FIDWeightSite IDSite;
        UReditusEnemyCombatAIComponent* Component = nullptr;
        if (!Blueprint || !ClassFunction || !IDFunction || !FindComponent(Blueprint, Component))
        {
            UE_LOG(LogReditusEnemyAIInstall, Error, TEXT("Could not find BP_EnemyUnitBase or its component slot. No Blueprint was edited."));
            return;
        }
        const bool bClassOK = FindClassSkillSite(FindFunctionGraph(Blueprint, TEXT("GetRandomSkill")), ClassHook, OldClassHook, ClassSite);
        const bool bIDOK = FindIDWeightSite(FindFunctionGraph(Blueprint, TEXT("GetRandomSkillByID")), IDHook, IDSite);
        if (!bClassOK || !bIDOK)
        {
            UE_LOG(LogReditusEnemyAIInstall, Error,
                TEXT("The existing skill selectors differ from the supported layout. GetRandomSkill: %s. GetRandomSkillByID: %s. No Blueprint was edited."),
                bClassOK ? TEXT("recognized") : TEXT("unrecognized"), bIDOK ? TEXT("recognized") : TEXT("unrecognized"));
            return;
        }
        auto* Profiles = Component && Component->ProfileTable ? Component->ProfileTable : FindOrCreateProfiles();
        if (!Profiles || Profiles->GetRowStruct() != FReditusEnemyAIProfile::StaticStruct())
        {
            UE_LOG(LogReditusEnemyAIInstall, Error, TEXT("The AI profile table needs the Reditus Enemy AI Profile row type. No Blueprint was edited."));
            return;
        }
        auto* Definitions = Component && Component->EnemyDefinitions ? Component->EnemyDefinitions
            : LoadObject<UDataTable>(nullptr, EnemyTablePath, nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (Component && Component->ProfileTable == Profiles && Component->EnemyDefinitions == Definitions &&
            ClassSite.bInstalled && IDSite.bInstalled)
        {
            AddExampleProfiles(Profiles);
            UE_LOG(LogReditusEnemyAIInstall, Display, TEXT("Enemy Combat AI is already installed on BP_EnemyUnitBase."));
            return;
        }

        bool bSuccess = false;
        {
            const FScopedTransaction Transaction(NSLOCTEXT("ReditusEnemyAI", "Install", "Install Reditus Enemy Combat AI"));
            if (!Transaction.IsOutstanding()) return;
            Blueprint->Modify();
            if (!Component)
            {
                auto* Script = Blueprint->SimpleConstructionScript;
                Script->Modify();
                auto* Node = Script->CreateNode(AIClass, EnemyCombatAIName);
                Component = Node ? Cast<UReditusEnemyCombatAIComponent>(Node->ComponentTemplate) : nullptr;
                if (Component) Script->AddNode(Node);
            }
            if (Component)
            {
                Component->Modify();
                Component->ProfileTable = Profiles;
                Component->EnemyDefinitions = Definitions;
                if (WireClassSkill(ClassSite, ClassFunction) && WireIDWeight(IDSite, IDFunction))
                {
                    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
                    bSuccess = Compile(Blueprint);
                }
            }
        }
        if (!bSuccess)
        {
            const bool bUndone = GEditor->UndoTransaction(false);
            if (bUndone) Compile(Blueprint);
            UE_LOG(LogReditusEnemyAIInstall, Error,
                TEXT("The install did not compile. Blueprint undo %s. The new unsaved profile table may still exist."),
                bUndone ? TEXT("succeeded") : TEXT("failed. Leave BP_EnemyUnitBase unsaved"));
            return;
        }
        AddExampleProfiles(Profiles);
        UE_LOG(LogReditusEnemyAIInstall, Display,
            TEXT("Enemy Combat AI installed. Save BP_EnemyUnitBase and DT_EnemyAIProfiles. To try it, choose the LizardMan profile on BP_LizardMan's inherited component."));
    }

    FAutoConsoleCommand GInstallEnemyAI(TEXT("Reditus.InstallEnemyCombatAI"),
        TEXT("Attach the C++ enemy AI component and connect both skill selectors. Stop Play first."),
        FConsoleCommandDelegate::CreateStatic(&Install));

    bool IsAttackerValue(UEdGraphPin* Value)
    {
        if (!Value || Value->Direction != EGPD_Output || Value->PinType.PinCategory != UEdGraphSchema_K2::PC_Object)
            return false;
        const auto* Type = Cast<UClass>(Value->PinType.PinSubCategoryObject.Get());
        auto* Getter = Cast<UK2Node_VariableGet>(Value->GetOwningNode());
        auto* Target = Pin(Getter, UEdGraphSchema_K2::PN_Self);
        return Type && Type->IsChildOf(AActor::StaticClass()) && Getter &&
            Getter->VariableReference.GetMemberName() == TEXT("currentAttackingUnit") &&
            (!Target || !Target->LinkedTo.Num() || IsSelf(TraceSource(Target)));
    }

    bool IsDamageTextSpawn(UEdGraphNode* Node)
    {
        auto* Spawn = Cast<UK2Node_SpawnActorFromClass>(Node);
        auto* Class = Pin(Spawn, TEXT("Class"));
        // This is the amount exposed on the project's damage popup actor.
        for (const UClass* Type = Class ? Cast<UClass>(Class->DefaultObject) : nullptr;
            Type; Type = Type->GetSuperClass())
        {
            if (Type->GetPathName() == TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Battle/BP_DamageText.BP_DamageText_C"))
            {
                const auto* Heal = Pin(Spawn, TEXT("isHeal"));
                return !Heal || Heal->LinkedTo.Num() || !Heal->DefaultValue.Equals(TEXT("true"), ESearchCase::IgnoreCase);
            }
        }
        return false;
    }

    struct FEnemyAmpDamageWires
    {
        UEdGraphPin* Attacker = nullptr;
        TMap<UEdGraphPin*, UEdGraphPin*> ScaledBySource;
        TMap<UEdGraphPin*, TArray<UEdGraphPin*>> ConsumersBySource;
    };

    bool FindEnemyAmpDamageWires(UEdGraph* Graph, UFunction* Scale, FEnemyAmpDamageWires& Wires)
    {
        TSet<UEdGraphPin*> ScaledOutputs;
        TArray<UEdGraphNode*> Pending;
        for (auto* Node : Graph->Nodes)
        {
            if (Cast<UK2Node_FunctionEntry>(Node)) Pending.Add(Node);
            if (!Wires.Attacker && Cast<UK2Node_VariableGet>(Node))
            {
                auto* Value = Pin(Node, TEXT("currentAttackingUnit"));
                if (IsAttackerValue(Value)) Wires.Attacker = Value;
            }
            auto* Call = Cast<UK2Node_CallFunction>(Node);
            if (!Call || Call->GetTargetFunction() != Scale) continue;

            auto* Base = TraceSource(Pin(Call, TEXT("Damage")));
            auto* Result = Pin(Call, UEdGraphSchema_K2::PN_ReturnValue);
            if (!Base || Base->PinType.PinCategory != UEdGraphSchema_K2::PC_Int || !Result ||
                !IsAttackerValue(TraceSource(Pin(Call, TEXT("Attacker")))) ||
                !IsSelf(TraceSource(Pin(Call, TEXT("BattleController")))))
            {
                UE_LOG(LogReditusEnemyAIInstall, Error, TEXT("Review the existing Scale Enemy Amp Damage wiring in BP_BattleController.DealDamage."));
                return false;
            }
            Wires.ScaledBySource.Add(Base, Result);
            ScaledOutputs.Add(Result);
        }
        for (const auto& Pair : Wires.ScaledBySource)
        {
            if (ScaledOutputs.Contains(Pair.Key))
            {
                UE_LOG(LogReditusEnemyAIInstall, Error, TEXT("DealDamage has chained Scale Enemy Amp Damage nodes. Review the existing AMP wiring."));
                return false;
            }
        }

        // Follow every live execution branch from the function entry. Scale the
        // final amount at its consumers so intermediate calculations can chain.
        TSet<UEdGraphNode*> Seen;
        bool bFoundHealthDamage = false;
        while (Pending.Num())
        {
            auto* Node = Pending.Pop(false);
            if (!Node || Seen.Contains(Node)) continue;
            Seen.Add(Node);
            for (const auto* Output : Node->Pins)
            {
                if (!Output || Output->bOrphanedPin || Output->Direction != EGPD_Output ||
                    Output->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) continue;
                for (const auto* Next : Output->LinkedTo)
                    if (Next && !Next->bOrphanedPin) Pending.Add(Next->GetOwningNode());
            }

            const bool bAppliesDamage = IsCall(Node, TEXT("TakeDamage"));
            if (!bAppliesDamage && !IsCall(Node, TEXT("Unit Accumulate Aggro")) &&
                !Cast<UK2Node_FunctionResult>(Node) && !IsDamageTextSpawn(Node)) continue;
            auto* Input = Pin(Node, TEXT("Damage"));
            if (!Input || !Input->LinkedTo.Num()) continue;
            auto* Base = TraceSource(Input);
            if (!Base || Input->Direction != EGPD_Input || Input->PinType.PinCategory != UEdGraphSchema_K2::PC_Int ||
                Base->PinType.PinCategory != UEdGraphSchema_K2::PC_Int)
            {
                UE_LOG(LogReditusEnemyAIInstall, Error, TEXT("DealDamage has a damage connection that needs review. Export BP_BattleController.DealDamage."));
                return false;
            }
            bFoundHealthDamage |= bAppliesDamage;
            if (!ScaledOutputs.Contains(Base)) Wires.ConsumersBySource.FindOrAdd(Base).Add(Input);
        }
        if (!Wires.Attacker || !bFoundHealthDamage)
        {
            UE_LOG(LogReditusEnemyAIInstall, Error, TEXT("DealDamage needs Current Attacking Unit and a live Take Damage amount. Export BP_BattleController.DealDamage."));
            return false;
        }
        return true;
    }

    bool WireEnemyAmpDamage(UEdGraph* Graph, UFunction* Scale, const FEnemyAmpDamageWires& Wires)
    {
        UEdGraphPin* BattleController = nullptr;
        Wires.Attacker->GetOwningNode()->Modify();
        for (const auto& Pair : Wires.ConsumersBySource)
        {
            auto* Base = Pair.Key;
            auto* Scaled = Wires.ScaledBySource.FindRef(Base);
            if (!Scaled)
            {
                auto* Source = Base->GetOwningNode();
                Source->Modify();
                auto* Call = AddNode<UK2Node_CallFunction>(Graph, Source->NodePosX + 240, Source->NodePosY + 300);
                Call->SetFromFunction(Scale);
                Call->AllocateDefaultPins();
                if (!BattleController)
                {
                    auto* Self = AddNode<UK2Node_Self>(Graph, Call->NodePosX - 160, Call->NodePosY + 100);
                    Self->AllocateDefaultPins();
                    BattleController = Pin(Self, UEdGraphSchema_K2::PN_Self);
                }
                if (!Link(BattleController, Pin(Call, TEXT("BattleController"))) ||
                    !Link(Wires.Attacker, Pin(Call, TEXT("Attacker"))) || !Link(Base, Pin(Call, TEXT("Damage"))))
                    return false;
                Scaled = Pin(Call, UEdGraphSchema_K2::PN_ReturnValue);
            }
            if (!Scaled) return false;
            Scaled->GetOwningNode()->Modify();
            for (auto* Input : Pair.Value)
            {
                auto* Previous = OneLink(Input);
                if (!Previous) return false;
                Previous->GetOwningNode()->Modify();
                Input->GetOwningNode()->Modify();
                Previous->BreakLinkTo(Input);
                if (!Link(Scaled, Input)) return false;
            }
        }
        return true;
    }

    void InstallEnemyAmpDamage()
    {
        if (!GEditor || GEditor->PlayWorld)
        {
            UE_LOG(LogReditusEnemyAIInstall, Error, TEXT("Stop Play before running Reditus.InstallEnemyAmpDamage."));
            return;
        }
        auto* Blueprint = LoadObject<UBlueprint>(nullptr,
            TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Battle/BP_BattleController.BP_BattleController"));
        auto* ATBClass = FindObject<UClass>(nullptr, TEXT("/Script/Chapter1Reditus.ReditusATBComponent"));
        UFunction* Scale = ATBClass ? ATBClass->FindFunctionByName(TEXT("ScaleEnemyAmpDamage")) : nullptr;
        auto* Graph = Blueprint ? FindFunctionGraph(Blueprint, TEXT("DealDamage")) : nullptr;
        if (!Graph || !Scale)
        {
            UE_LOG(LogReditusEnemyAIInstall, Error, TEXT("The DealDamage graph or Scale Enemy Amp Damage node was unavailable."));
            return;
        }

        FEnemyAmpDamageWires Wires;
        if (!FindEnemyAmpDamageWires(Graph, Scale, Wires)) return;
        if (!Wires.ConsumersBySource.Num())
        {
            UE_LOG(LogReditusEnemyAIInstall, Display, TEXT("Enemy AMP damage is already wired in BP_BattleController.DealDamage."));
            return;
        }

        bool bSuccess = false;
        {
            const FScopedTransaction Transaction(NSLOCTEXT("ReditusEnemyAI", "InstallAmpDamage", "Wire Enemy AMP Damage"));
            if (!Transaction.IsOutstanding()) return;
            Blueprint->Modify();
            Graph->Modify();
            if (WireEnemyAmpDamage(Graph, Scale, Wires))
            {
                FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
                bSuccess = Compile(Blueprint);
            }
        }
        if (!bSuccess)
        {
            const bool bUndone = GEditor->UndoTransaction(false);
            if (bUndone) Compile(Blueprint);
            UE_LOG(LogReditusEnemyAIInstall, Error, TEXT("Enemy AMP damage wiring could not compile. Blueprint undo %s."),
                bUndone ? TEXT("succeeded") : TEXT("failed. Leave BP_BattleController unsaved"));
            return;
        }
        UE_LOG(LogReditusEnemyAIInstall, Display, TEXT("Enemy AMP damage installed in DealDamage. Save BP_BattleController."));
    }

    FAutoConsoleCommand GInstallEnemyAmpDamage(TEXT("Reditus.InstallEnemyAmpDamage"),
        TEXT("Wire spent enemy AMP into live damage amounts and feedback. Stop Play first."),
        FConsoleCommandDelegate::CreateStatic(&InstallEnemyAmpDamage));
}
