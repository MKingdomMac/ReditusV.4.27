#include "ReditusEnemyCombatAIComponent.h"
#include "ReditusTechMasteryLibrary.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/UserDefinedStruct.h"
#include "HAL/IConsoleManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Knot.h"
#include "K2Node_Self.h"
#include "K2Node_Variable.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/StructureEditorUtils.h"
#include "KismetCompiler.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/ScopeExit.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"

DEFINE_LOG_CATEGORY_STATIC(LogReditusMasteryInstall, Log, All);

namespace ReditusTechMasteryInstall
{
    const TCHAR* EnemyBlueprintPath = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Units/EnemyUnits/BP_EnemyUnitBase.BP_EnemyUnitBase");
    const TCHAR* EnemyTablePath = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_Enemies.DT_Enemies");
    const TCHAR* ProfileTablePath = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_EnemyAIProfiles.DT_EnemyAIProfiles");
    const TCHAR* SkillTablePath = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_Skills.DT_Skills");
    const TCHAR* EquipmentTablePath = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_Equipment.DT_Equipment");
    const FName MasteryCostName(TEXT("MasteryPointsRequired"));
    const FName RewardOverrideName(TEXT("MasteryRewardOverride"));
    const FName SaveVariableName(TEXT("TechMastery"));

    bool CheckEditor()
    {
        if (GEditor && !GEditor->PlayWorld) return true;
        UE_LOG(LogReditusMasteryInstall, Error, TEXT("Stop Play in Editor before running this command."));
        return false;
    }

    bool IsProfileTable(const UDataTable* Table)
    {
        return IsValid(Table) && Table->GetRowStruct() == FReditusEnemyAIProfile::StaticStruct();
    }

    UDataTable* DefaultProfileTable()
    {
        auto* Enemy = LoadObject<UBlueprint>(nullptr, EnemyBlueprintPath);
        if (Enemy && Enemy->SimpleConstructionScript)
        {
            for (auto* Node : Enemy->SimpleConstructionScript->GetAllNodes())
            {
                const auto* AI = Node ? Cast<UReditusEnemyCombatAIComponent>(Node->ComponentTemplate) : nullptr;
                if (AI && IsProfileTable(AI->ProfileTable)) return AI->ProfileTable;
            }
        }
        return LoadObject<UDataTable>(nullptr, ProfileTablePath);
    }

    void MigrateEnemyProfiles()
    {
        if (!CheckEditor()) return;
        auto* Enemies = LoadObject<UDataTable>(nullptr, EnemyTablePath);
        auto* Profiles = DefaultProfileTable();
        const UScriptStruct* RowType = Enemies ? Enemies->GetRowStruct() : nullptr;
        const auto* HandleField = RowType ? FindFProperty<FStructProperty>(RowType, TEXT("AIProfile")) : nullptr;
        const auto* IDField = RowType ? FindFProperty<FNameProperty>(RowType, TEXT("AIProfileID")) : nullptr;
        if (!HandleField || HandleField->Struct != FDataTableRowHandle::StaticStruct() || !IDField || !IsProfileTable(Profiles))
        {
            UE_LOG(LogReditusMasteryInstall, Error, TEXT("DT_Enemies AI Profile or DT_EnemyAIProfiles has an unexpected type."));
            return;
        }

        const FScopedTransaction Transaction(NSLOCTEXT("ReditusMastery", "MigrateProfiles", "Migrate enemy AI profiles"));
        if (!Transaction.IsOutstanding()) return;
        int32 Migrated = 0;
        int32 Preset = 0;
        int32 Unresolved = 0;
        for (const auto& Entry : Enemies->GetRowMap())
        {
            if (!Entry.Value) continue;
            auto& Handle = *HandleField->ContainerPtrToValuePtr<FDataTableRowHandle>(Entry.Value);
            if (!Handle.RowName.IsNone()) continue;
            const FName ID = IDField->GetPropertyValue_InContainer(Entry.Value);
            const UDataTable* Candidate = Handle.DataTable ? Handle.DataTable : Profiles;
            if (!ID.IsNone() && (!IsProfileTable(Candidate) || !Candidate->GetRowMap().Contains(ID)))
            {
                ++Unresolved;
                UE_LOG(LogReditusMasteryInstall, Warning, TEXT("%s keeps its legacy AI profile ID %s. The selected table has no matching profile."),
                    *Entry.Key.ToString(), *ID.ToString());
                continue;
            }
            if (ID.IsNone() && Handle.DataTable) continue;
            if (Migrated + Preset == 0)
            {
                Enemies->SetFlags(RF_Transactional);
                Enemies->Modify();
            }
            if (!Handle.DataTable) Handle.DataTable = Profiles;
            if (ID.IsNone())
            {
                ++Preset;
            }
            else
            {
                Handle.RowName = ID;
                IDField->SetPropertyValue_InContainer(Entry.Value, NAME_None);
                ++Migrated;
            }
        }
        if (Migrated + Preset > 0) Enemies->MarkPackageDirty();
        UE_LOG(LogReditusMasteryInstall, Display, TEXT("Enemy AI profile migration complete. %d moved, %d table pickers set, %d legacy IDs kept. Save DT_Enemies."),
            Migrated, Preset, Unresolved);
    }

    UEdGraphPin* Pin(UEdGraphNode* Node, FName Name)
    {
        auto* Result = Node ? Node->FindPin(Name) : nullptr;
        return Result && !Result->bOrphanedPin ? Result : nullptr;
    }

    UEdGraphPin* OneLink(const UEdGraphPin* Input)
    {
        return Input && Input->LinkedTo.Num() == 1 ? Input->LinkedTo[0] : nullptr;
    }

    UEdGraphPin* Source(UEdGraphPin* Input)
    {
        auto* Link = OneLink(Input);
        TSet<UEdGraphNode*> Visited;
        while (Link)
        {
            auto* Knot = Cast<UK2Node_Knot>(Link->GetOwningNode());
            if (!Knot) return Link;
            if (Visited.Contains(Knot)) return nullptr;
            Visited.Add(Knot);
            Link = OneLink(Pin(Knot, TEXT("InputPin")));
        }
        return nullptr;
    }

    UEdGraphNode* FindAnchor(UBlueprint* Blueprint, const TCHAR* ID)
    {
        FGuid Guid;
        if (!Blueprint || !FGuid::ParseExact(ID, EGuidFormats::Digits, Guid)) return nullptr;
        for (UEdGraph* Graph : Blueprint->UbergraphPages)
        {
            if (!Graph) continue;
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (Node && Node->NodeGuid == Guid) return Node;
            }
        }
        return nullptr;
    }

    bool IsCall(const UEdGraphNode* Node, FName Name)
    {
        const auto* Call = Cast<UK2Node_CallFunction>(Node);
        return Call && Call->FunctionReference.GetMemberName() == Name;
    }

    bool IsVariable(const UEdGraphNode* Node, FName Name)
    {
        const auto* Var = Cast<UK2Node_Variable>(Node);
        return Var && Var->VariableReference.GetMemberName() == Name;
    }

    bool IsOutputOf(UEdGraphPin* Input, FName Name)
    {
        const auto* Output = Source(Input);
        return Output && Output->PinName == Name;
    }

    template <typename NodeType>
    NodeType* AddNode(UEdGraph* Graph, UEdGraphNode* Anchor, int32 OffsetY)
    {
        auto* Node = NewObject<NodeType>(Graph, NAME_None, RF_Transactional);
        Graph->AddNode(Node, false, false);
        Node->CreateNewGuid();
        Node->NodePosX = Anchor->NodePosX + 48;
        Node->NodePosY = Anchor->NodePosY + OffsetY;
        return Node;
    }

    bool Link(UEdGraphPin* Output, UEdGraphPin* Input)
    {
        if (!Output || !Input) return false;
        auto* Graph = Output->GetOwningNode()->GetGraph();
        const auto* Schema = Cast<UEdGraphSchema_K2>(Graph->GetSchema());
        if (!Schema) return false;
        Output->GetOwningNode()->Modify();
        Input->GetOwningNode()->Modify();
        return Schema->TryCreateConnection(Output, Input);
    }

    struct FExecSite
    {
        UEdGraphPin* Then = nullptr;
        FName Function;
        FName SelfParameter;
        TArray<TPair<FName, UEdGraphPin*>> Inputs;
        bool bTableEnemy = false;
    };

    bool AddExecSite(TArray<FExecSite>& Sites, UEdGraphNode* Anchor, FName Function, FName SelfParameter,
        TArray<TPair<FName, UEdGraphPin*>> Inputs = {}, bool bTableEnemy = false)
    {
        auto* Then = Pin(Anchor, UEdGraphSchema_K2::PN_Then);
        if (!Then || Then->LinkedTo.Num() > 1) return false;
        for (const auto& Input : Inputs)
        {
            if (!Input.Value || Input.Value->Direction != EGPD_Output) return false;
        }
        Sites.Add(FExecSite{Then, Function, SelfParameter, MoveTemp(Inputs), bTableEnemy});
        return true;
    }

    UK2Node_CallFunction* AddCall(UEdGraphNode* Anchor, FName FunctionName, FName SelfParameter)
    {
        auto* Function = UReditusTechMasteryLibrary::StaticClass()->FindFunctionByName(FunctionName);
        if (!Function) return nullptr;

        auto* Graph = Anchor->GetGraph();
        Graph->Modify();
        auto* Call = AddNode<UK2Node_CallFunction>(Graph, Anchor, 164);
        Call->SetFromFunction(Function);
        Call->AllocateDefaultPins();
        auto* Self = AddNode<UK2Node_Self>(Graph, Anchor, 254);
        Self->NodePosX -= 128;
        Self->AllocateDefaultPins();
        return Link(Pin(Self, UEdGraphSchema_K2::PN_Self), Pin(Call, SelfParameter)) ? Call : nullptr;
    }

    bool IsSelfInput(UEdGraphNode* Node, FName Name)
    {
        const auto* Self = Source(Pin(Node, Name));
        return Self && Self->GetOwningNode()->IsA<UK2Node_Self>();
    }

    bool WireExec(const FExecSite& Site, bool& bChanged)
    {
        auto* ExistingNext = OneLink(Site.Then);
        if (ExistingNext && IsCall(ExistingNext->GetOwningNode(), Site.Function))
        {
            auto* Call = ExistingNext->GetOwningNode();
            if (!IsSelfInput(Call, Site.SelfParameter)) return false;
            for (const auto& Input : Site.Inputs)
            {
                if (OneLink(Pin(Call, Input.Key)) != Input.Value) return false;
            }
            const auto* TableInput = Pin(Call, TEXT("bTableEnemy"));
            return !TableInput || (TableInput->DefaultValue == TEXT("true")) == Site.bTableEnemy;
        }

        auto* Call = AddCall(Site.Then->GetOwningNode(), Site.Function, Site.SelfParameter);
        if (!Call) return false;
        for (const auto& Input : Site.Inputs)
        {
            if (!Link(Input.Value, Pin(Call, Input.Key))) return false;
        }
        if (auto* TableInput = Pin(Call, TEXT("bTableEnemy")))
        {
            TableInput->DefaultValue = Site.bTableEnemy ? TEXT("true") : TEXT("false");
        }
        if (ExistingNext)
        {
            Site.Then->GetOwningNode()->Modify();
            ExistingNext->GetOwningNode()->Modify();
            Site.Then->BreakLinkTo(ExistingNext);
            if (!Link(Pin(Call, UEdGraphSchema_K2::PN_Then), ExistingNext)) return false;
        }
        bChanged = true;
        return Link(Site.Then, Pin(Call, UEdGraphSchema_K2::PN_Execute));
    }

    bool WireFormattedText(UEdGraphPin* Input, FName Function, FName SelfParameter, FName TextParameter,
        bool& bChanged)
    {
        auto* Original = OneLink(Input);
        if (!Original) return false;
        if (IsCall(Original->GetOwningNode(), Function))
        {
            auto* Call = Original->GetOwningNode();
            return IsSelfInput(Call, SelfParameter) && OneLink(Pin(Call, TextParameter));
        }

        auto* Call = AddCall(Input->GetOwningNode(), Function, SelfParameter);
        if (!Call || !Link(Original, Pin(Call, TextParameter))) return false;
        Original->GetOwningNode()->Modify();
        Input->GetOwningNode()->Modify();
        Original->BreakLinkTo(Input);
        bChanged = true;
        return Link(Pin(Call, UEdGraphSchema_K2::PN_ReturnValue), Input);
    }

    bool FindAppendSite(UBlueprint* Blueprint, const TCHAR* ID, TArray<FExecSite>& Sites)
    {
        auto* Append = FindAnchor(Blueprint, ID);
        if (!IsCall(Append, TEXT("Array_Append")) ||
            !IsOutputOf(Pin(Append, TEXT("TargetArray")), TEXT("availableBattleSkills")) ||
            !IsOutputOf(Pin(Append, TEXT("SourceArray")), TEXT("equipmentBattleSkills"))) return false;
        return AddExecSite(Sites, Append, GET_FUNCTION_NAME_CHECKED(UReditusTechMasteryLibrary, AppendLearnedTechs),
            TEXT("SkillOwner"));
    }

    bool FindOverdriveAppendSite(UBlueprint* Blueprint, const TCHAR* ID, FName Target, TArray<FExecSite>& Sites)
    {
        auto* Append = FindAnchor(Blueprint, ID);
        return IsCall(Append, TEXT("Array_Append")) &&
            IsOutputOf(Pin(Append, TEXT("TargetArray")), Target) &&
            IsOutputOf(Pin(Append, TEXT("SourceArray")), TEXT("equipmentoverdriveskills")) &&
            AddExecSite(Sites, Append,
                GET_FUNCTION_NAME_CHECKED(UReditusTechMasteryLibrary, AppendEquipmentAndLearnedOverdrives), TEXT("SkillOwner"));
    }

    bool WireEquipmentPreview(UBlueprint* Techs, bool& bChanged)
    {
        auto* Set = FindAnchor(Techs, TEXT("2399DCF04344820FB156F6AD1250607E"));
        auto* Regular = FindAnchor(Techs, TEXT("9650F72149D94DF40DB040B26BDAD540"));
        auto* Overdrive = FindAnchor(Techs, TEXT("BAB94E8A4D78BAD64D9C9C8068D0BE0B"));
        auto* Then = Pin(Set, UEdGraphSchema_K2::PN_Then);
        auto* Next = OneLink(Then);
        const FName Function = GET_FUNCTION_NAME_CHECKED(UReditusTechMasteryLibrary, ShowEquipmentSkillPages);
        if (!IsVariable(Set, TEXT("AvailableOverdrive_Skills")) || !Next) return false;
        if (IsCall(Next->GetOwningNode(), Function)) return IsSelfInput(Next->GetOwningNode(), TEXT("Menu"));
        if (!IsCall(Regular, TEXT("ShowSkills")) || !IsCall(Overdrive, TEXT("ShowSkills")) ||
            Next != Pin(Regular, UEdGraphSchema_K2::PN_Execute) ||
            OneLink(Pin(Regular, UEdGraphSchema_K2::PN_Then)) != Pin(Overdrive, UEdGraphSchema_K2::PN_Execute) ||
            OneLink(Pin(Overdrive, UEdGraphSchema_K2::PN_Then))) return false;

        auto* Call = AddCall(Set, Function, TEXT("Menu"));
        if (!Call) return false;
        Set->Modify();
        Regular->Modify();
        Then->BreakLinkTo(Next);
        bChanged = true;
        return Link(Then, Pin(Call, UEdGraphSchema_K2::PN_Execute));
    }

    bool FindOverdriveDescriptionSite(UBlueprint* Dialogue, TArray<FExecSite>& Sites)
    {
        auto* Text = FindAnchor(Dialogue, TEXT("20391DAE44E2C112504DC0BAB273DE1C"));
        auto* Definition = FindAnchor(Dialogue, TEXT("94568E9748B79489F93A60B71F9635F5"));
        auto* Skill = Source(Pin(Definition, TEXT("Skill ID")));
        if (!IsCall(Text, TEXT("SetText")) || !Skill || Skill->PinType.PinCategory != UEdGraphSchema_K2::PC_Name ||
            !IsOutputOf(Pin(Text, UEdGraphSchema_K2::PN_Self), TEXT("SkillDescriptionText"))) return false;
        TArray<TPair<FName, UEdGraphPin*>> Inputs;
        Inputs.Emplace(FName(TEXT("SkillID")), Skill);
        return AddExecSite(Sites, Text,
            GET_FUNCTION_NAME_CHECKED(UReditusTechMasteryLibrary, UpdateOverdriveDescription), TEXT("Menu"), MoveTemp(Inputs));
    }

    bool WireOverdriveRows(UBlueprint* Dialogue, bool& bChanged)
    {
        auto* Found = Cast<UK2Node_IfThenElse>(FindAnchor(Dialogue, TEXT("86C4F04F4FDA4BE0B16C20BBCBBB57E2")));
        auto* OldGate = Cast<UK2Node_IfThenElse>(FindAnchor(Dialogue, TEXT("E11FF09F4C4D019A7FDBA894C65BA9F9")));
        auto* Definition = FindAnchor(Dialogue, TEXT("39BB483C437F3ACBE8B85D9C63F851A3"));
        auto* Create = FindAnchor(Dialogue, TEXT("91D6A49E4583B2A9FF9C42A1A2AE0495"));
        auto* Skill = Source(Pin(Definition, TEXT("Skill ID")));
        auto* UseSkill = Pin(Create, TEXT("skill"));
        auto* Then = Pin(Found, UEdGraphSchema_K2::PN_Then);
        auto* Build = Pin(Create, UEdGraphSchema_K2::PN_Execute);
        if (!Then || !Build || !UseSkill || !Skill || Skill->PinType.PinCategory != UEdGraphSchema_K2::PC_Name ||
            !IsOutputOf(Pin(Found, TEXT("Condition")), TEXT("Found"))) return false;
        if (OneLink(Then) == Build) return OneLink(UseSkill) == Skill;
        auto* Clear = FindAnchor(Dialogue, TEXT("20A874F346B3900045448CA902316E34"));
        auto* Check = Pin(OldGate, UEdGraphSchema_K2::PN_Then);
        auto* Before = OneLink(Then);
        if (!Check || !Before || Before != Pin(Clear, UEdGraphSchema_K2::PN_Execute) || OneLink(Build) != Check ||
            (Source(UseSkill) && Source(UseSkill) != Skill)) return false;

        Found->Modify();
        Clear->Modify();
        Create->Modify();
        OldGate->Modify();
        Then->BreakLinkTo(Before);
        Check->BreakLinkTo(Build);
        UseSkill->BreakAllPinLinks();
        bChanged = true;
        return Link(Skill, UseSkill) && Link(Then, Build);
    }

    bool WireOverdriveConfirmation(UBlueprint* Dialogue, bool& bChanged)
    {
        auto* Acquire = FindAnchor(Dialogue, TEXT("574059B2447F2F4BA01B4FA9D33345FA"));
        auto* SetInput = FindAnchor(Dialogue, TEXT("38C4DDFE411540FE441F8D9BA0C3A970"));
        auto* Broadcast = FindAnchor(Dialogue, TEXT("0A2CFA87430C1CC490A954AF08C65CC7"));
        auto* Valid = Pin(Acquire, TEXT("Is Valid"));
        auto* Continue = Pin(SetInput, UEdGraphSchema_K2::PN_Execute);
        auto* Skill = Source(Pin(Broadcast, TEXT("selectedOverdrive")));
        auto* Next = OneLink(Valid);
        const FName Function = GET_FUNCTION_NAME_CHECKED(UReditusTechMasteryLibrary, CanConfirmOverdrive);
        if (!Next || !Continue || !Skill || Skill->PinType.PinCategory != UEdGraphSchema_K2::PC_Name ||
            !IsCall(SetInput, TEXT("SetUIInput"))) return false;
        if (Next != Continue)
        {
            auto* Gate = Cast<UK2Node_IfThenElse>(Next->GetOwningNode());
            auto* Condition = OneLink(Pin(Gate, TEXT("Condition")));
            auto* Call = Condition ? Condition->GetOwningNode() : nullptr;
            return IsCall(Call, Function) && IsSelfInput(Call, TEXT("Menu")) &&
                OneLink(Pin(Call, TEXT("SkillID"))) == Skill && OneLink(Pin(Gate, UEdGraphSchema_K2::PN_Then)) == Continue;
        }

        SetInput->GetGraph()->Modify();
        auto* Gate = AddNode<UK2Node_IfThenElse>(SetInput->GetGraph(), SetInput, -120);
        Gate->AllocateDefaultPins();
        auto* Check = AddCall(Gate, Function, TEXT("Menu"));
        auto* Status = AddCall(Gate, GET_FUNCTION_NAME_CHECKED(UReditusTechMasteryLibrary, UpdateOverdriveDescription), TEXT("Menu"));
        if (!Check || !Status || !Link(Skill, Pin(Check, TEXT("SkillID"))) || !Link(Skill, Pin(Status, TEXT("SkillID"))) ||
            !Link(Pin(Check, UEdGraphSchema_K2::PN_ReturnValue), Pin(Gate, TEXT("Condition"))) ||
            !Link(Pin(Gate, TEXT("else")), Pin(Status, UEdGraphSchema_K2::PN_Execute))) return false;
        Acquire->Modify();
        SetInput->Modify();
        Valid->BreakLinkTo(Continue);
        bChanged = true;
        return Link(Valid, Pin(Gate, UEdGraphSchema_K2::PN_Execute)) && Link(Pin(Gate, UEdGraphSchema_K2::PN_Then), Continue);
    }

    bool FindSkillButtonSite(UBlueprint* Blueprint, const TCHAR* CreateID, const TCHAR* AddID, TArray<FExecSite>& Sites)
    {
        auto* Create = FindAnchor(Blueprint, CreateID);
        auto* Add = FindAnchor(Blueprint, AddID);
        auto* Widget = Pin(Create, UEdGraphSchema_K2::PN_ReturnValue);
        auto* Skill = Source(Pin(Create, TEXT("skill")));
        if (!Widget || !Skill || Skill->PinType.PinCategory != UEdGraphSchema_K2::PC_Name ||
            Source(Pin(Add, TEXT("Content"))) != Widget ||
            OneLink(Pin(Create, UEdGraphSchema_K2::PN_Then)) != Pin(Add, UEdGraphSchema_K2::PN_Execute)) return false;
        TArray<TPair<FName, UEdGraphPin*>> Inputs;
        Inputs.Emplace(FName(TEXT("SkillButton")), Widget);
        Inputs.Emplace(FName(TEXT("SkillID")), Skill);
        return AddExecSite(Sites, Add, GET_FUNCTION_NAME_CHECKED(UReditusTechMasteryLibrary, ShowMasteryOnSkillButton),
            TEXT("Menu"), MoveTemp(Inputs));
    }

    bool FindSelectedTechSites(UBlueprint* Techs, TArray<FExecSite>& Sites)
    {
        auto* Description = FindAnchor(Techs, TEXT("B361C07D4A03764CC110C48A4F4E378B"));
        auto* Clear = FindAnchor(Techs, TEXT("90CE1C8F46FB3167D60A5981D419071F"));
        auto* Definition = FindAnchor(Techs, TEXT("284304204DB8EBAC733EDBA544B86F77"));
        auto* Skill = Source(Pin(Definition, TEXT("Skill ID")));
        for (auto* Text : {Description, Clear})
        {
            if (!IsCall(Text, TEXT("SetText")) ||
                !IsOutputOf(Pin(Text, UEdGraphSchema_K2::PN_Self), TEXT("SkillDescription"))) return false;
        }
        if (!Skill || Skill->PinType.PinCategory != UEdGraphSchema_K2::PC_Name ||
            !OneLink(Pin(Description, TEXT("InText"))) || OneLink(Pin(Clear, TEXT("InText")))) return false;

        const FName Function = GET_FUNCTION_NAME_CHECKED(UReditusTechMasteryLibrary, UpdateSelectedTechMastery);
        TArray<TPair<FName, UEdGraphPin*>> Inputs;
        Inputs.Emplace(FName(TEXT("SkillID")), Skill);
        return AddExecSite(Sites, Description, Function, TEXT("Menu"), MoveTemp(Inputs)) &&
            AddExecSite(Sites, Clear, Function, TEXT("Menu"));
    }

    bool FindLevelUpSites(UBlueprint* LevelUp, TArray<FExecSite>& Sites)
    {
        auto* ActionTimeText = FindAnchor(LevelUp, TEXT("8EB0AF1745489463D8CBAB82FFD6BCA2"));
        auto* ActionTimeHidden = FindAnchor(LevelUp, TEXT("3A5ABF574DBECC28F4FD4EAF318474D0"));
        const FName Function = GET_FUNCTION_NAME_CHECKED(UReditusTechMasteryLibrary, PresentLevelUp);
        return IsCall(ActionTimeText, TEXT("SetText")) &&
            IsOutputOf(Pin(ActionTimeText, UEdGraphSchema_K2::PN_Self), TEXT("ActionTimeText")) &&
            IsCall(ActionTimeHidden, TEXT("SetVisibility")) &&
            IsOutputOf(Pin(ActionTimeHidden, UEdGraphSchema_K2::PN_Self), TEXT("ActionTimeOverlay")) &&
            AddExecSite(Sites, ActionTimeText, Function, TEXT("LevelUpWidget")) &&
            AddExecSite(Sites, ActionTimeHidden, Function, TEXT("LevelUpWidget"));
    }

    bool FindEquipmentDetailsSite(UBlueprint* Gear, TArray<FExecSite>& Sites)
    {
        auto* Create = FindAnchor(Gear, TEXT("A0E9A8EC4263127E92A3A1ADE590A21C"));
        auto* Add = FindAnchor(Gear, TEXT("794D84284A8902B522B475886ED452A3"));
        auto* Details = Pin(Create, UEdGraphSchema_K2::PN_ReturnValue);
        auto* EquipmentID = Source(Pin(Create, TEXT("equipment")));
        if (!IsCall(Add, TEXT("AddToViewport")) || !Details || !EquipmentID ||
            EquipmentID->PinType.PinCategory != UEdGraphSchema_K2::PC_Name ||
            Source(Pin(Add, UEdGraphSchema_K2::PN_Self)) != Details ||
            OneLink(Pin(Create, UEdGraphSchema_K2::PN_Then)) != Pin(Add, UEdGraphSchema_K2::PN_Execute)) return false;

        TArray<TPair<FName, UEdGraphPin*>> Inputs;
        Inputs.Emplace(FName(TEXT("Details")), Details);
        Inputs.Emplace(FName(TEXT("EquipmentID")), EquipmentID);
        return AddExecSite(Sites, Add, GET_FUNCTION_NAME_CHECKED(UReditusTechMasteryLibrary, ShowEquipmentMastery),
            TEXT("Menu"), MoveTemp(Inputs));
    }

    bool FindRewardSite(UBlueprint* Battle, const TCHAR* SetID, const TCHAR* GetterID,
        bool bTableEnemy, TArray<FExecSite>& Sites)
    {
        auto* SetGold = FindAnchor(Battle, SetID);
        auto* Gold = FindAnchor(Battle, GetterID);
        if (!IsVariable(SetGold, TEXT("totalGoldReward")) || !IsVariable(Gold, TEXT("goldReward"))) return false;
        TArray<TPair<FName, UEdGraphPin*>> Inputs;
        Inputs.Emplace(FName(TEXT("Enemy")), OneLink(Pin(Gold, UEdGraphSchema_K2::PN_Self)));
        return AddExecSite(Sites, SetGold, GET_FUNCTION_NAME_CHECKED(UReditusTechMasteryLibrary, IncludeEnemyMasteryReward),
            TEXT("Battle"), MoveTemp(Inputs), bTableEnemy);
    }

    FBPVariableDescription* FindVariable(UBlueprint* Blueprint, FName Name)
    {
        return Blueprint->NewVariables.FindByPredicate([Name](const FBPVariableDescription& Var) { return Var.VarName == Name; });
    }

    bool PrepareVariable(UBlueprint* Blueprint, FName Name, const FEdGraphPinType& Type,
        const TCHAR* DefaultValue, EPropertyFlags Flag, FText Category)
    {
        auto* Var = FindVariable(Blueprint, Name);
        if (!Var)
        {
            Blueprint->Modify();
            if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, Name, Type, DefaultValue)) return false;
            Var = FindVariable(Blueprint, Name);
        }
        if (!Var || Var->VarType.PinCategory != Type.PinCategory || Var->VarType.ContainerType != Type.ContainerType ||
            Var->VarType.PinSubCategoryObject.Get() != Type.PinSubCategoryObject.Get()) return false;
        if ((Var->PropertyFlags & Flag) == 0 || !Var->Category.EqualTo(Category))
        {
            Blueprint->Modify();
            Var->PropertyFlags |= Flag;
            Var->Category = Category;
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        }
        return true;
    }

    using FRowSnapshot = TMap<FName, TMap<FName, FString>>;

    FString TextValue(const FProperty* Property, const uint8* Row)
    {
        FString Result;
        Property->ExportTextItem(Result, Property->ContainerPtrToValuePtr<void>(Row), nullptr, nullptr, PPF_None);
        return Result;
    }

    FRowSnapshot SnapshotRows(const UDataTable* Table)
    {
        FRowSnapshot Result;
        for (const auto& Row : Table->GetRowMap())
        {
            if (!Row.Value) continue;
            auto& Values = Result.Add(Row.Key);
            for (TFieldIterator<FProperty> Field(Table->GetRowStruct()); Field; ++Field)
            {
                Values.Add(Field->GetFName(), TextValue(*Field, Row.Value));
            }
        }
        return Result;
    }

    bool RowsPreserved(const UDataTable* Table, const FRowSnapshot& Original)
    {
        if (Original.Num() != Table->GetRowMap().Num()) return false;
        for (const auto& Row : Original)
        {
            const uint8* Data = Table->GetRowMap().FindRef(Row.Key);
            if (!Data) return false;
            for (const auto& Value : Row.Value)
            {
                const auto* Field = FindFProperty<FProperty>(Table->GetRowStruct(), Value.Key);
                if (!Field || TextValue(Field, Data) != Value.Value) return false;
            }
        }
        return true;
    }

    bool PrepareSkillTable(UDataTable* Table)
    {
        auto* Struct = Table ? Cast<UUserDefinedStruct>(const_cast<UScriptStruct*>(Table->GetRowStruct())) : nullptr;
        if (!Struct) return false;
        const auto& Variables = FStructureEditorUtils::GetVarDesc(Struct);
        for (const auto& Var : Variables)
        {
            if (Var.FriendlyName == MasteryCostName.ToString())
            {
                return FindFProperty<FIntProperty>(Struct, Var.VarName) != nullptr;
            }
        }

        const auto Original = SnapshotRows(Table);
        Table->SetFlags(RF_Transactional);
        Struct->SetFlags(RF_Transactional);
        Table->Modify();
        Struct->Modify();
        FEdGraphPinType Type;
        Type.PinCategory = UEdGraphSchema_K2::PC_Int;
        if (!FStructureEditorUtils::AddVariable(Struct, Type)) return false;
        const FGuid NewId = FStructureEditorUtils::GetVarDesc(Struct).Last().VarGuid;
        if (!FStructureEditorUtils::RenameVariable(Struct, NewId, MasteryCostName.ToString()) ||
            !RowsPreserved(Table, Original))
        {
            UE_LOG(LogReditusMasteryInstall, Error, TEXT("DT_Skills could not preserve its original rows during the new field change."));
            return false;
        }
        const auto* Var = FStructureEditorUtils::GetVarDescByGuid(Struct, NewId);
        const auto* Cost = Var ? FindFProperty<FIntProperty>(Struct, Var->VarName) : nullptr;
        if (!Cost) return false;
        if (uint8* Sample = Table->GetRowMap().FindRef(TEXT("ProtectOtherBuff")))
        {
            Cost->SetPropertyValue_InContainer(Sample, 4);
            UE_LOG(LogReditusMasteryInstall, Display, TEXT("ProtectOtherBuff now takes 4 Mastery to learn."));
        }
        Struct->MarkPackageDirty();
        Table->MarkPackageDirty();
        return true;
    }

    bool SeedEquipmentOverdriveCosts(UDataTable* Skills)
    {
        auto* Equipment = LoadObject<UDataTable>(nullptr, EquipmentTablePath);
        const UScriptStruct* Type = Equipment ? Equipment->GetRowStruct() : nullptr;
        if (!Type || !Skills->GetRowStruct()) return false;
        const FArrayProperty* Overdrives = nullptr;
        for (TFieldIterator<FArrayProperty> It(Type); It; ++It)
        {
            if (It->GetName().StartsWith(TEXT("OverDriveSkillIDs_")) ||
                Type->GetAuthoredNameForField(*It).Equals(TEXT("OverDriveSkillIDs"), ESearchCase::IgnoreCase))
            {
                Overdrives = *It;
                break;
            }
        }
        const auto* ID = Overdrives ? CastField<FNameProperty>(Overdrives->Inner) : nullptr;
        const FIntProperty* Cost = nullptr;
        for (TFieldIterator<FIntProperty> It(Skills->GetRowStruct()); It; ++It)
        {
            if (It->GetName().StartsWith(TEXT("MasteryPointsRequired_")) ||
                Skills->GetRowStruct()->GetAuthoredNameForField(*It).Equals(MasteryCostName.ToString(), ESearchCase::IgnoreCase))
            {
                Cost = *It;
                break;
            }
        }
        if (!ID || !Cost) return false;

        bool bSaved = false;
        for (const auto& Entry : Equipment->GetRowMap())
        {
            if (!Entry.Value) continue;
            FScriptArrayHelper Values(Overdrives, Overdrives->ContainerPtrToValuePtr<void>(Entry.Value));
            for (int32 Index = 0; Index < Values.Num(); ++Index)
            {
                auto* Skill = Skills->GetRowMap().FindRef(ID->GetPropertyValue(Values.GetRawPtr(Index)));
                if (!Skill || Cost->GetPropertyValue_InContainer(Skill) != 0) continue;
                if (!bSaved)
                {
                    Skills->SetFlags(RF_Transactional);
                    Skills->Modify();
                    bSaved = true;
                }
                Cost->SetPropertyValue_InContainer(Skill, 4);
            }
        }
        if (bSaved) Skills->MarkPackageDirty();
        return true;
    }

    class FScopedMasteryCompileLog final : public FOutputDevice
    {
    public:
        FScopedMasteryCompileLog()
        {
            if (GLog) GLog->AddOutputDevice(this);
        }

        ~FScopedMasteryCompileLog() override
        {
            if (GLog) GLog->RemoveOutputDevice(this);
        }

        void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override
        {
            static const FName EnsureCategory(TEXT("LogOutputDevice"));
            if (IsInGameThread() && Category == EnsureCategory && Verbosity == ELogVerbosity::Error &&
                FCString::Strstr(Message, TEXT("Ensure condition failed")))
            {
                bSawEnsure = true;
            }
        }

        bool SawEnsure() const { return bSawEnsure; }

    private:
        bool bSawEnsure = false;
    };

    bool Compile(UBlueprint* Blueprint, const FScopedMasteryCompileLog& CompileLog)
    {
        if (CompileLog.SawEnsure()) return false;
        UE_LOG(LogReditusMasteryInstall, Display, TEXT("Compiling %s for Tech Mastery."), *Blueprint->GetName());
        FCompilerResultsLog Results;
        FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
        if (!CompileLog.SawEnsure() && Blueprint->Status != BS_Error && Results.NumErrors == 0 &&
            IsValid(Blueprint->GeneratedClass)) return true;
        UE_LOG(LogReditusMasteryInstall, Error, TEXT("Blueprint compile failed for %s."), *Blueprint->GetName());
        return false;
    }

    bool InstallMasteryAssets(const FScopedMasteryCompileLog& CompileLog)
    {
        auto* Battle = LoadObject<UBlueprint>(nullptr, TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Battle/BP_BattleBase.BP_BattleBase"));
        auto* Controller = LoadObject<UBlueprint>(nullptr, TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Battle/BP_BattleController.BP_BattleController"));
        auto* Enemy = LoadObject<UBlueprint>(nullptr, EnemyBlueprintPath);
        auto* Player = LoadObject<UBlueprint>(nullptr, TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Units/PlayerUnits/BP_PlayerUnitBase.BP_PlayerUnitBase"));
        auto* Save = LoadObject<UBlueprint>(nullptr, TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Controllers/BP_JRPGSaveGame.BP_JRPGSaveGame"));
        auto* Techs = LoadObject<UBlueprint>(nullptr, TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/UI/MainMenu/BP_SkillDetails.BP_SkillDetails"));
        auto* Gear = LoadObject<UBlueprint>(nullptr, TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/UI/MainMenu/BP_WearEquipmentUI.BP_WearEquipmentUI"));
        auto* Victory = LoadObject<UBlueprint>(nullptr, TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/UI/Dialogues/BP_VictoryDialogue.BP_VictoryDialogue"));
        auto* LevelUp = LoadObject<UBlueprint>(nullptr, TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/UI/Dialogues/BP_LevelUpDialogue.BP_LevelUpDialogue"));
        auto* Overdrive = LoadObject<UBlueprint>(nullptr, TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/UI/Dialogues/BP_OverdriveDialogue.BP_OverdriveDialogue"));
        auto* Skills = LoadObject<UDataTable>(nullptr, SkillTablePath);
        if (CompileLog.SawEnsure() || !Battle || !Controller || !Enemy || !Player || !Save || !Techs || !Gear ||
            !Victory || !LevelUp || !Overdrive || !Skills) return false;

        TArray<FExecSite> Sites;
        auto* ResetGold = FindAnchor(Battle, TEXT("10AD59C84060B26E6F8C7EA984C06FF0"));
        auto* Spawn = FindAnchor(Battle, TEXT("E1DF3A73471C66A9E6E7F3AE236495C8"));
        auto* Win = Cast<UK2Node_CustomEvent>(FindAnchor(Controller, TEXT("C39DA34845132315640F28A93D2F594D")));
        auto* VictoryText = FindAnchor(Victory, TEXT("4D5B57DD467530E748DCB5B7A6836A7F"));
        auto* VictoryInput = Pin(VictoryText, TEXT("InText"));
        auto* VictoryItems = FindAnchor(Victory, TEXT("74ED7FF8461A7987A98258A5A37619AF"));
        const TCHAR* MissingSite = nullptr;
        if (!IsVariable(ResetGold, TEXT("totalGoldReward")) ||
            !AddExecSite(Sites, ResetGold, GET_FUNCTION_NAME_CHECKED(UReditusTechMasteryLibrary, BeginMasteryEncounter), TEXT("Battle")))
            MissingSite = TEXT("BP_BattleBase SpawnEnemyUnits reward reset");
        else if (!IsCall(Spawn, TEXT("SpawnEnemyUnits")) ||
            !AddExecSite(Sites, Spawn, GET_FUNCTION_NAME_CHECKED(UReditusTechMasteryLibrary, HoldBattleUnitsForHUD), TEXT("Battle")))
            MissingSite = TEXT("BP_BattleBase battle intro");
        else if (!FindRewardSite(Battle, TEXT("08F7C92D445FB01B1CC19BA9DC4BF7FF"), TEXT("D1F2378E4AB0CDE53A043D8D5024DCAC"), false, Sites))
            MissingSite = TEXT("BP_BattleBase class enemy reward");
        else if (!FindRewardSite(Battle, TEXT("ED77EEB74AB4A4DD6555DBAFC4178CB6"), TEXT("CE46EB2D4DC1133803D62C973C018EC1"), true, Sites))
            MissingSite = TEXT("BP_BattleBase table enemy reward");
        else if (!Win || Win->CustomFunctionName != TEXT("PlayerWon") ||
            !AddExecSite(Sites, Win, GET_FUNCTION_NAME_CHECKED(UReditusTechMasteryLibrary, AwardVictoryMastery), TEXT("BattleController")))
            MissingSite = TEXT("BP_BattleController PlayerWon");
        else if (!FindAppendSite(Player, TEXT("66D5FF074EBAEED9BE1BE9AB930CCDAA"), Sites))
            MissingSite = TEXT("BP_PlayerUnitBase battle skill list");
        else if (!FindAppendSite(Techs, TEXT("2AA7894643B79D38E785CD856C4E5A28"), Sites))
            MissingSite = TEXT("BP_SkillDetails Tech list");
        else if (!FindOverdriveAppendSite(Player, TEXT("66EA5570483B373C2A92BDA3502098AA"), TEXT("availableOverdriveTechs"), Sites) ||
            !FindOverdriveAppendSite(Techs, TEXT("9F1A75ED4A3A59CBA014438C48B972D3"), TEXT("availableOverDriveSkills"), Sites))
            MissingSite = TEXT("equipment Overdrive lists");
        else if (!FindSkillButtonSite(Gear, TEXT("B78885A940BA80540BB02FAD9C38F0F8"), TEXT("40BDEFA045A2D2E9125CE482DE09C5F4"), Sites))
            MissingSite = TEXT("BP_WearEquipmentUI skill button");
        else if (!FindEquipmentDetailsSite(Gear, Sites))
            MissingSite = TEXT("BP_WearEquipmentUI equipment details");
        else if (!FindSelectedTechSites(Techs, Sites))
            MissingSite = TEXT("BP_SkillDetails selected Tech details");
        else if (!IsCall(VictoryText, TEXT("SetText")) ||
            !IsOutputOf(Pin(VictoryText, UEdGraphSchema_K2::PN_Self), TEXT("ExpRewardText")) || !OneLink(VictoryInput))
            MissingSite = TEXT("BP_VictoryDialogue EXP text");
        else if (!IsCall(VictoryItems, TEXT("ShowItemsObtained")) ||
            !AddExecSite(Sites, VictoryItems, GET_FUNCTION_NAME_CHECKED(UReditusTechMasteryLibrary, PresentVictory), TEXT("VictoryWidget")))
            MissingSite = TEXT("BP_VictoryDialogue results");
        else if (!FindLevelUpSites(LevelUp, Sites))
            MissingSite = TEXT("BP_LevelUpDialogue results");
        else if (!FindOverdriveDescriptionSite(Overdrive, Sites))
            MissingSite = TEXT("BP_OverdriveDialogue description");

        if (MissingSite)
        {
            UE_LOG(LogReditusMasteryInstall, Error, TEXT("Could not identify %s. The installer made no asset edits."), MissingSite);
            return false;
        }

        bool bSuccess = false;
        {
            const FScopedTransaction Transaction(NSLOCTEXT("ReditusMastery", "Install", "Install Reditus Tech Mastery"));
            if (!Transaction.IsOutstanding()) return false;
            TSet<UBlueprint*> Changed;
            bool bNewOverdriveMastery = false;
            bSuccess = true;
            for (const auto& Site : Sites)
            {
                auto* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Site.Then->GetOwningNode()->GetGraph());
                if (!Blueprint)
                {
                    bSuccess = false;
                    break;
                }
                Blueprint->Modify();
                bool bChanged = false;
                bSuccess = WireExec(Site, bChanged) && !CompileLog.SawEnsure();
                if (!bSuccess)
                {
                    UE_LOG(LogReditusMasteryInstall, Error, TEXT("Could not connect %s in %s."),
                        *Site.Function.ToString(), *Blueprint->GetName());
                    break;
                }
                if (bChanged)
                {
                    Changed.Add(Blueprint);
                    bNewOverdriveMastery |= Blueprint == Player &&
                        Site.Function == GET_FUNCTION_NAME_CHECKED(UReditusTechMasteryLibrary, AppendEquipmentAndLearnedOverdrives);
                }
            }
            if (bSuccess)
            {
                Techs->Modify();
                Overdrive->Modify();
                bool bPreviewChanged = false;
                bool bDialogueChanged = false;
                bSuccess = WireEquipmentPreview(Techs, bPreviewChanged) &&
                    WireOverdriveRows(Overdrive, bDialogueChanged) && WireOverdriveConfirmation(Overdrive, bDialogueChanged) &&
                    !CompileLog.SawEnsure();
                if (bPreviewChanged) Changed.Add(Techs);
                if (bDialogueChanged) Changed.Add(Overdrive);
                if (!bSuccess)
                    UE_LOG(LogReditusMasteryInstall, Error, TEXT("Could not connect equipment preview or Overdrive dialogue."));
            }
            if (bSuccess)
            {
                Victory->Modify();
                bool bChanged = false;
                bSuccess = WireFormattedText(VictoryInput, GET_FUNCTION_NAME_CHECKED(UReditusTechMasteryLibrary, FormatVictoryMastery),
                    TEXT("VictoryWidget"), TEXT("RewardText"), bChanged) && !CompileLog.SawEnsure();
                if (bChanged) Changed.Add(Victory);
            }
            if (bSuccess)
            {
                FEdGraphPinType SaveType;
                SaveType.PinCategory = UEdGraphSchema_K2::PC_Struct;
                SaveType.PinSubCategoryObject = FReditusTechMasterySaveData::StaticStruct();
                FEdGraphPinType RewardType;
                RewardType.PinCategory = UEdGraphSchema_K2::PC_Int;
                bSuccess = PrepareVariable(Save, SaveVariableName, SaveType, TEXT(""), CPF_SaveGame, FText::FromString(TEXT("Tech Mastery"))) &&
                    !CompileLog.SawEnsure() &&
                    PrepareVariable(Enemy, RewardOverrideName, RewardType, TEXT("0"), CPF_Edit, FText::FromString(TEXT("Rewards"))) &&
                    !CompileLog.SawEnsure() &&
                    PrepareSkillTable(Skills) && !CompileLog.SawEnsure() &&
                    (!bNewOverdriveMastery || SeedEquipmentOverdriveCosts(Skills)) && !CompileLog.SawEnsure();
            }
            if (bSuccess)
            {
                for (auto* Blueprint : Changed)
                {
                    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
                }
                for (auto* Blueprint : {Save, Enemy, Battle, Controller, Player, Techs, Gear, Victory, LevelUp, Overdrive})
                {
                    if (!Compile(Blueprint, CompileLog))
                    {
                        bSuccess = false;
                        break;
                    }
                }
            }
        }
        // A failed reinstancing can leave temporary classes in this session. Undo can compile again.
        if (CompileLog.SawEnsure()) return false;
        if (!bSuccess)
        {
            const bool bUndone = GEditor->UndoTransaction(false);
            if (bUndone)
            {
                for (auto* Blueprint : {Save, Enemy, Battle, Controller, Player, Techs, Gear, Victory, LevelUp, Overdrive})
                {
                    if (!Compile(Blueprint, CompileLog)) break;
                }
            }
            UE_LOG(LogReditusMasteryInstall, Error, TEXT("Tech Mastery installation stopped. Undo result: %s. Close without saving the affected assets if any edits remain."),
                bUndone ? TEXT("success") : TEXT("failed"));
            return false;
        }
        return true;
    }

    void InstallMastery()
    {
        if (!CheckEditor()) return;
        static bool bNeedsEditorRestart = false;
        if (bNeedsEditorRestart)
        {
            UE_LOG(LogReditusMasteryInstall, Error, TEXT("Restart the editor after the previous Blueprint compiler assertion."));
            return;
        }

        // UE 4.27's updated path moves dependent skeleton classes together during recompilation.
        auto* SkeletonUpdate = IConsoleManager::Get().FindConsoleVariable(TEXT("BP.bEnableSkelReinstUpdate"));
        const bool bEnableForInstall = SkeletonUpdate && SkeletonUpdate->GetInt() == 0;
        const auto Priority = SkeletonUpdate
            ? static_cast<EConsoleVariableFlags>(SkeletonUpdate->GetFlags() & ECVF_SetByMask)
            : ECVF_SetByConstructor;
        if (bEnableForInstall) SkeletonUpdate->Set(1, Priority);
        ON_SCOPE_EXIT
        {
            if (bEnableForInstall) SkeletonUpdate->Set(0, Priority);
        };

        const FScopedMasteryCompileLog CompileLog;
        const bool bInstalled = InstallMasteryAssets(CompileLog);
        if (CompileLog.SawEnsure())
        {
            bNeedsEditorRestart = true;
            UE_LOG(LogReditusMasteryInstall, Error,
                TEXT("Unreal reported an assertion during installation. Leave this run's Mastery changes unsaved and restart the editor."));
        }
        else if (bInstalled)
        {
            UE_LOG(LogReditusMasteryInstall, Display, TEXT("Tech Mastery installation complete. Save All before starting Play."));
        }
        else
        {
            UE_LOG(LogReditusMasteryInstall, Error, TEXT("Tech Mastery installation failed. See the preceding messages."));
        }
    }

    FAutoConsoleCommand MigrateCommand(
        TEXT("Reditus.MigrateEnemyAIProfiles"),
        TEXT("Convert existing DT_Enemies AI profile IDs to row handles."),
        FConsoleCommandDelegate::CreateStatic(&MigrateEnemyProfiles));

    FAutoConsoleCommand InstallCommand(
        TEXT("Reditus.InstallTechMastery"),
        TEXT("Install the Reditus Tech Mastery Blueprint hooks and skill field."),
        FConsoleCommandDelegate::CreateStatic(&InstallMastery));
}
