#include "ReditusQuestGraphCompiler.h"
#include "ReditusQuestGraph.h"
#include "ReditusQuestGraphPolicy.h"
#include "ReditusQuestCatalog.h"
#include "ReditusQuestRuntime.h"
#include "AssetRegistryModule.h"
#include "Editor.h"
#include "EdGraph/EdGraphPin.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

namespace ReditusQuestGraphCompiler
{
    UDataTable* DefaultCatalog(FString& Error)
    {
        if (GEditor && GEditor->PlayWorld) { Error = TEXT("Stop Play before creating a quest catalogue."); return nullptr; }
        const TCHAR* PackagePath = TEXT("/Game/Reditus/Story/DT_QuestCatalog");
        const FString Path = FString(PackagePath) + TEXT(".DT_QuestCatalog");
        if (auto* Existing = LoadObject<UObject>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
        {
            auto* Table = Cast<UDataTable>(Existing);
            if (Table && Table->GetRowStruct() == FReditusQuestCatalogRow::StaticStruct()) return Table;
            Error = TEXT("DT_QuestCatalog already exists with a different asset/row type. It was preserved."); return nullptr;
        }
        if (FPackageName::DoesPackageExist(PackagePath))
        { Error = TEXT("The catalogue package exists but could not be loaded. It was preserved."); return nullptr; }
        auto* Package = CreatePackage(PackagePath);
        auto* Table = NewObject<UDataTable>(Package, TEXT("DT_QuestCatalog"), RF_Public | RF_Standalone | RF_Transactional);
        Table->RowStruct = FReditusQuestCatalogRow::StaticStruct();
        FAssetRegistryModule::AssetCreated(Table); Table->MarkPackageDirty(); return Table;
    }

    bool Compile(UReditusQuestAsset* Asset, FString& Error, bool Register)
    {
        Error.Reset();
        if (!Asset || (GEditor && GEditor->PlayWorld))
        { Error = TEXT("Stop Play before compiling quest assets."); return false; }
        Asset->Modify(); Asset->InvalidateCompilation();
        if (Asset->QuestID.IsNone() || Asset->QuestID.ToString().Contains(TEXT("/")) || !Asset->EditorGraph
            || Asset->EditorGraph->GetOuter() != Asset)
        { Error = TEXT("The quest needs a stable Quest ID and its own authoring graph."); return false; }

        TArray<UReditusQuestGraphNode*> Nodes;
        for (auto* Base : Asset->EditorGraph->Nodes)
        {
            auto* Node = Cast<UReditusQuestGraphNode>(Base);
            if (!Node) { Error = TEXT("The graph contains an unsupported node."); return false; }
            Nodes.Add(Node);
        }
        std::vector<ReditusQuestGraphPolicy::Node> Input;
        for (auto* Node : Nodes)
        {
            ReditusQuestGraphPolicy::Node N;
            N.Type = static_cast<ReditusQuestGraphPolicy::Kind>(Node->Kind);
            N.ID = TCHAR_TO_UTF8(*Node->Stage.StageID.ToString().ToLower());
            if (Node->Kind != EReditusQuestGraphNodeKind::Stage) N.ID.clear();
            int32 ExpectedPins = Node->Kind == EReditusQuestGraphNodeKind::Finish ? 0 : 1;
            if (Node->Kind == EReditusQuestGraphNodeKind::Stage)
                for (const auto& Choice : Node->Stage.Choices) ExpectedPins += Choice.Options.Num();
            for (const auto* Pin : Node->Pins)
            {
                if (!Pin || Pin->Direction != EGPD_Output) continue;
                if (Pin->LinkedTo.Num() != 1 || !Pin->LinkedTo[0] || Pin->LinkedTo[0]->Direction != EGPD_Input
                    || !Pin->LinkedTo[0]->LinkedTo.Contains(const_cast<UEdGraphPin*>(Pin)))
                { Error = TEXT("Connect every output exactly once: ") + Node->GetNodeTitle(ENodeTitleType::ListView).ToString() + TEXT(" / ") + Pin->PinName.ToString(); return false; }
                ReditusQuestGraphPolicy::Exit E;
                E.Target = Nodes.IndexOfByKey(Cast<UReditusQuestGraphNode>(Pin->LinkedTo[0]->GetOwningNode()));
                bool Known = Pin->PinName == TEXT("Next");
                if (!Known && Node->Kind == EReditusQuestGraphNodeKind::Stage)
                    for (const auto& Choice : Node->Stage.Choices) for (const auto& Option : Choice.Options)
                        if (Pin->PinName == UReditusQuestGraphNode::ChoicePin(Choice.ChoiceID, Option.OptionID))
                        {
                            Known = true;
                            E.Choice = TCHAR_TO_UTF8(*Choice.ChoiceID.ToString().ToLower());
                            E.Option = TCHAR_TO_UTF8(*Option.OptionID.ToString().ToLower());
                        }
                if (!Known) { Error = TEXT("Unknown output pin. Refresh the stage's choices."); return false; }
                N.Exits.push_back(E);
            }
            if (int32(N.Exits.size()) != ExpectedPins)
            { Error = TEXT("Stage pins do not match its authored choices. Edit the stage to refresh its pins."); return false; }
            Input.push_back(N);
        }
        ReditusQuestGraphPolicy::Plan Plan; std::string Why;
        if (!ReditusQuestGraphPolicy::Compile(Input, Plan, Why))
        { Error = UTF8_TO_TCHAR(Why.c_str()); return false; }

        FReditusQuestDefinition Definition = Asset->Settings;
        Definition.Stages.Reset();
        TArray<FReditusQuestStageConversation> Conversations;
        for (int Index : Plan.Stages)
        {
            auto Stage = Nodes[Index]->Stage;
            Stage.Routes.Reset(); Stage.NextStageID = NAME_None; Stage.FinishAfterStage = false;
            for (const auto& Exit : Input[unsigned(Index)].Exits)
            {
                const auto* Target = Nodes[Exit.Target];
                const bool Finish = Target->Kind == EReditusQuestGraphNodeKind::Finish;
                if (Exit.Choice.empty())
                { Stage.FinishAfterStage = Finish; Stage.NextStageID = Finish ? NAME_None : Target->Stage.StageID; }
                else
                {
                    FReditusQuestStageRoute Route;
                    Route.ChoiceID = FName(UTF8_TO_TCHAR(Exit.Choice.c_str()));
                    Route.OptionID = FName(UTF8_TO_TCHAR(Exit.Option.c_str()));
                    Route.Finish = Finish; Route.NextStageID = Finish ? NAME_None : Target->Stage.StageID;
                    Stage.Routes.Add(Route);
                }
            }
            Definition.Stages.Add(Stage);
            if (Nodes[Index]->Conversation)
            {
                FReditusQuestStageConversation Binding;
                Binding.StageID = Stage.StageID; Binding.Dialogue = Nodes[Index]->Conversation;
                Conversations.Add(Binding);
            }
        }

        // Row handles expose a const table in UE4.27. Only this editor compiler writes it.
        UDataTable* Catalog = const_cast<UDataTable*>(Asset->RegisteredQuest.DataTable);
        if (Catalog && (Catalog->GetRowStruct() != FReditusQuestCatalogRow::StaticStruct()
            || Asset->RegisteredQuest.RowName != Asset->QuestID))
        { Error = TEXT("A registered quest's catalogue and Quest ID are stable. Restore its original ID."); return false; }
        if (!Catalog) Catalog = DefaultCatalog(Error);
        if (!Catalog) return false;
        const auto* Existing = Catalog->FindRow<FReditusQuestCatalogRow>(Asset->QuestID, TEXT("Register quest"), false);
        if (Existing && (Existing->Kind != EReditusQuestCatalogKind::Graph || Existing->GraphQuest != Asset))
        { Error = TEXT("This Quest ID already belongs to another catalogue entry. That entry was preserved."); return false; }

        const FString Key = (Catalog->GetPathName() + TEXT(":") + Asset->QuestID.ToString()).ToLower();
        ReditusQuestRuntime::Definition RuntimeDefinition;
        if (!ReditusQuestCatalog::BuildDefinition(Definition, Key, RuntimeDefinition, Error)) return false;
        ReditusQuestRuntime::Engine Validator;
        if (!Validator.AddDefinition(RuntimeDefinition, Why))
        { Error = UTF8_TO_TCHAR(Why.c_str()); return false; }
        // Check prerequisite chains with the same definitions used at runtime.
        TSet<FString> Visiting, Validated;
        TFunction<bool(FDataTableRowHandle)> ValidateDependency;
        ValidateDependency = [&](FDataTableRowHandle Handle)
        {
            Handle = ReditusQuestCatalog::Canonical(Handle);
            const auto* Row = ReditusQuestCatalog::Definition(Handle);
            if (!Row) { Error = TEXT("A prerequisite is missing or has not been compiled."); return false; }
            const FString ID = (Handle.DataTable->GetPathName() + TEXT(":") + Handle.RowName.ToString()).ToLower();
            if (ID == Key || Visiting.Contains(ID)) { Error = TEXT("Cyclic quest prerequisites."); return false; }
            if (Validated.Contains(ID)) return true;
            Visiting.Add(ID);
            for (const auto& Required : Row->RequiredCompletedQuests) if (!ValidateDependency(Required)) return false;
            Visiting.Remove(ID); Validated.Add(ID); return true;
        };
        for (const auto& Required : Definition.RequiredCompletedQuests) if (!ValidateDependency(Required)) return false;
        if (!Register) return true;

        Catalog->Modify();
        FReditusQuestCatalogRow Entry; Entry.GraphQuest = Asset;
        Catalog->AddRow(Asset->QuestID, Entry);
        Asset->RegisteredQuest.DataTable = Catalog; Asset->RegisteredQuest.RowName = Asset->QuestID;
        Asset->CompiledDefinition = MoveTemp(Definition); Asset->StageConversations = MoveTemp(Conversations);
        Asset->Compiled = true;
        Catalog->MarkPackageDirty(); Asset->MarkPackageDirty(); return true;
    }
}
