#include "ReditusQuestNPCComponent.h"
#include "ReditusQuestObjectiveComponent.h"
#include "ReditusQuestNPCTypes.h"
#include "ReditusQuestSystemTypes.h"
#include "DialogueUserWidget.h"
#include "Dom/JsonObject.h"
#include "MapIconComponent.h"
#include "PaperFlipbookComponent.h"
#include "AssetRegistryModule.h"
#include "Components/SceneComponent.h"
#include "Editor.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/DataTable.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogReditusQuestNPCEditor, Log, All);
void IntegrateReditusQuestJournal();

namespace ReditusQuestNPCEditor
{
    const TCHAR* NPCPackage = TEXT("/Game/Reditus/Story/BP_QuestNPC_New");
    const TCHAR* ObjectivePackage = TEXT("/Game/Reditus/Story/BP_QuestObjective_New");
    const TCHAR* NPCTablePackage = TEXT("/Game/Reditus/Story/DT_QuestNPCs");
    const TCHAR* ObjectiveTablePackage = TEXT("/Game/Reditus/Story/DT_QuestObjectives");
    const TCHAR* QuestTablePackage = TEXT("/Game/Reditus/Story/DT_Quests");
    const TCHAR* ParentPath = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Interactables/BP_InteractionDetector.BP_InteractionDetector_C");
    const TCHAR* WidgetPath = TEXT("/DialoguePlugin/UI/DemoDialogueWidget.DemoDialogueWidget_C");

    FString ObjectPath(const TCHAR* Package)
    {
        return FString(Package) + TEXT(".") + FPackageName::GetLongPackageAssetName(Package);
    }

    bool IsOwned(UObject* Object)
    {
        return Object && FString(Object->GetOutermost()->GetMetaData()->GetValue(Object, TEXT("ReditusQuestNPCVersion"))) == TEXT("1");
    }

    void MarkOwned(UObject* Object)
    {
        Object->GetOutermost()->GetMetaData()->SetValue(Object, TEXT("ReditusQuestNPCVersion"), TEXT("1"));
        Object->MarkPackageDirty();
        FAssetRegistryModule::AssetCreated(Object);
    }

    bool PathAvailable(const TCHAR* Package)
    {
        UObject* Existing = LoadObject<UObject>(nullptr, *ObjectPath(Package), nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (Existing) return IsOwned(Existing);
        return !FPackageName::DoesPackageExist(Package);
    }

    UDataTable* Table(const TCHAR* PackageName, UScriptStruct* RowStruct)
    {
        if (auto* Existing = LoadObject<UDataTable>(nullptr, *ObjectPath(PackageName), nullptr, LOAD_NoWarn | LOAD_Quiet))
            return IsOwned(Existing) && Existing->GetRowStruct() == RowStruct ? Existing : nullptr;
        UPackage* Package = CreatePackage(PackageName);
        auto* Result = NewObject<UDataTable>(Package, FName(*FPackageName::GetLongPackageAssetName(PackageName)), RF_Public | RF_Standalone | RF_Transactional);
        Result->RowStruct = RowStruct;
        MarkOwned(Result);
        return Result;
    }

    UBlueprint* Blueprint(const TCHAR* PackageName, UClass* Parent, bool& Created)
    {
        Created = false;
        if (auto* Existing = LoadObject<UBlueprint>(nullptr, *ObjectPath(PackageName), nullptr, LOAD_NoWarn | LOAD_Quiet))
            return IsOwned(Existing) ? Existing : nullptr;
        UPackage* Package = CreatePackage(PackageName);
        auto* Result = FKismetEditorUtilities::CreateBlueprint(Parent, Package,
            FName(*FPackageName::GetLongPackageAssetName(PackageName)), BPTYPE_Normal,
            UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), TEXT("ReditusQuestNPC"));
        if (Result) { MarkOwned(Result); Created = true; }
        return Result;
    }

    USCS_Node* Node(UBlueprint* BP, UClass* Class, FName Name, USCS_Node* Parent, bool ExternalParent)
    {
        auto* Result = BP->SimpleConstructionScript->CreateNode(Class, Name);
        if (!Result) return nullptr;
        if (Parent && !ExternalParent) Parent->AddChildNode(Result);
        else
        {
            BP->SimpleConstructionScript->AddNode(Result);
            if (Parent) Result->SetParent(Parent);
        }
        return Result;
    }

    bool Compile(UBlueprint* BP)
    {
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
        FKismetEditorUtilities::CompileBlueprint(BP);
        if (BP->Status == BS_Error)
        {
            UE_LOG(LogReditusQuestNPCEditor, Error, TEXT("New asset %s has compile errors. Check Compiler Results before saving."), *BP->GetPathName());
            return false;
        }
        BP->MarkPackageDirty();
        return true;
    }

    TSharedRef<FJsonObject> ExportBlueprint(const TCHAR* Path)
    {
        auto Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("Path"), Path);
        auto* BP = LoadObject<UBlueprint>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (!BP) { Result->SetBoolField(TEXT("Found"), false); return Result; }
        Result->SetBoolField(TEXT("Found"), true);
        Result->SetStringField(TEXT("ParentClass"), BP->ParentClass ? BP->ParentClass->GetPathName() : FString());
        TArray<UEdGraph*> Graphs;
        BP->GetAllGraphs(Graphs);
        TArray<TSharedPtr<FJsonValue>> GraphValues;
        for (auto* Graph : Graphs)
        {
            if (!Graph) continue;
            auto G = MakeShared<FJsonObject>(); G->SetStringField(TEXT("Name"), Graph->GetName());
            TArray<TSharedPtr<FJsonValue>> Nodes;
            for (auto* N : Graph->Nodes)
            {
                if (!N) continue;
                auto J = MakeShared<FJsonObject>();
                J->SetStringField(TEXT("Name"), N->GetName());
                J->SetStringField(TEXT("Class"), N->GetClass()->GetPathName());
                J->SetStringField(TEXT("Title"), N->GetNodeTitle(ENodeTitleType::ListView).ToString());
                J->SetStringField(TEXT("Guid"), N->NodeGuid.ToString());
                TArray<TSharedPtr<FJsonValue>> Pins;
                for (auto* P : N->Pins)
                {
                    if (!P) continue;
                    auto V = MakeShared<FJsonObject>();
                    V->SetStringField(TEXT("Name"), P->PinName.ToString());
                    V->SetStringField(TEXT("Category"), P->PinType.PinCategory.ToString());
                    V->SetStringField(TEXT("Direction"), P->Direction == EGPD_Output ? TEXT("Output") : TEXT("Input"));
                    V->SetStringField(TEXT("DefaultValue"), P->DefaultValue);
                    V->SetStringField(TEXT("DefaultObject"), P->DefaultObject ? P->DefaultObject->GetPathName() : FString());
                    TArray<TSharedPtr<FJsonValue>> Links;
                    for (auto* L : P->LinkedTo) if (L && L->GetOwningNode())
                        Links.Add(MakeShared<FJsonValueString>(L->GetOwningNode()->GetName() + TEXT(".") + L->PinName.ToString()));
                    V->SetArrayField(TEXT("LinkedTo"), Links); Pins.Add(MakeShared<FJsonValueObject>(V));
                }
                J->SetArrayField(TEXT("Pins"), Pins); Nodes.Add(MakeShared<FJsonValueObject>(J));
            }
            G->SetArrayField(TEXT("Nodes"), Nodes); GraphValues.Add(MakeShared<FJsonValueObject>(G));
        }
        Result->SetArrayField(TEXT("Graphs"), GraphValues);
        return Result;
    }

    void ExportIntegration()
    {
        auto Document = MakeShared<FJsonObject>();
        TArray<TSharedPtr<FJsonValue>> Assets;
        for (const TCHAR* Path : {
            TEXT("/DialoguePlugin/UI/DemoDialogueWidget.DemoDialogueWidget"),
            TEXT("/DialoguePlugin/UI/DemoReplyWidget.DemoReplyWidget"),
            TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/UI/MainMenu/BP_SaveUI.BP_SaveUI"),
            TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/UI/MainMenu/BP_QuestDetails.BP_QuestDetails"),
            TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/UI/Dialogues/BP_QuestButton.BP_QuestButton"),
            TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Controllers/BP_JRPGSaveGame.BP_JRPGSaveGame"),
            TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Controllers/BP_JRPGPlayerController.BP_JRPGPlayerController")})
            Assets.Add(MakeShared<FJsonValueObject>(ExportBlueprint(Path)));
        Document->SetArrayField(TEXT("Blueprints"), Assets);
        FString Text;
        auto Writer = TJsonWriterFactory<>::Create(&Text);
        if (!FJsonSerializer::Serialize(Document, Writer)) return;
        const FString Folder = FPaths::ProjectSavedDir() / TEXT("ReditusQuestNPC");
        IFileManager::Get().MakeDirectory(*Folder, true);
        if (FFileHelper::SaveStringToFile(Text, *(Folder / TEXT("StoryIntegration.json"))))
            UE_LOG(LogReditusQuestNPCEditor, Display, TEXT("Exported Saved/ReditusQuestNPC/StoryIntegration.json for the next dialogue/sequence UI integration step."));
    }

    void CreateAssets()
    {
        if (GEditor && GEditor->PlayWorld)
        { UE_LOG(LogReditusQuestNPCEditor, Error, TEXT("Stop Play before creating quest NPC assets.")); return; }
        // Preflight every destination before creating anything. Never edit an existing game Blueprint.
        for (const TCHAR* Path : {NPCPackage, ObjectivePackage, NPCTablePackage, ObjectiveTablePackage})
            if (!PathAvailable(Path))
            { UE_LOG(LogReditusQuestNPCEditor, Error, TEXT("%s is occupied by an unrelated asset. Nothing overwritten."), Path); return; }

        UClass* Parent = LoadClass<AActor>(nullptr, ParentPath);
        UClass* WidgetClass = LoadClass<UDialogueUserWidget>(nullptr, WidgetPath);
        auto* ParentBP = Parent ? Cast<UBlueprint>(Parent->ClassGeneratedBy) : nullptr;
        USCS_Node* ParentRoot = nullptr;
        if (ParentBP && ParentBP->SimpleConstructionScript)
            for (auto* Candidate : ParentBP->SimpleConstructionScript->GetAllNodes())
                if (Candidate && Candidate->GetVariableName() == TEXT("DefaultSceneRoot")) ParentRoot = Candidate;
        if (!Parent || !WidgetClass || !ParentRoot)
        { UE_LOG(LogReditusQuestNPCEditor, Error, TEXT("The expected interaction parent, scene root or dialogue widget is missing. Nothing created.")); return; }

        UDataTable* NPCs = Table(NPCTablePackage, FReditusQuestNPCDefinition::StaticStruct());
        UDataTable* Objectives = Table(ObjectiveTablePackage, FReditusQuestObjectiveDefinition::StaticStruct());
        if (!NPCs || !Objectives) return;

        bool Created = false;
        UBlueprint* NPC = Blueprint(NPCPackage, Parent, Created);
        if (!NPC) return;
        if (Created)
        {
            auto* SpriteNode = Node(NPC, UPaperFlipbookComponent::StaticClass(), TEXT("QuestSprite"), ParentRoot, true);
            auto* IconNode = Node(NPC, UMapIconComponent::StaticClass(), TEXT("MapIcon"), ParentRoot, true);
            auto* BrainNode = Node(NPC, UReditusQuestNPCComponent::StaticClass(), TEXT("QuestNPC"), nullptr, false);
            if (!SpriteNode || !IconNode || !BrainNode) return;
            auto* Sprite = CastChecked<UPaperFlipbookComponent>(SpriteNode->ComponentTemplate);
            Sprite->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            auto* Icon = CastChecked<UMapIconComponent>(IconNode->ComponentTemplate);
            Icon->SetIconVisible(false);
            auto* Brain = CastChecked<UReditusQuestNPCComponent>(BrainNode->ComponentTemplate);
            Brain->DefinitionRow.DataTable = NPCs;
            Brain->DefaultDialogueWidgetClass = WidgetClass;
            if (!Compile(NPC)) return;
        }

        UBlueprint* Objective = Blueprint(ObjectivePackage, AActor::StaticClass(), Created);
        if (!Objective) return;
        if (Created)
        {
            USCS_Node* Root = nullptr;
            for (auto* Candidate : Objective->SimpleConstructionScript->GetRootNodes())
                if (Candidate && Cast<USceneComponent>(Candidate->ComponentTemplate)) { Root = Candidate; break; }
            if (!Root) Root = Node(Objective, USceneComponent::StaticClass(), TEXT("QuestObjectiveRoot"), nullptr, false);
            auto* IconNode = Node(Objective, UMapIconComponent::StaticClass(), TEXT("MapIcon"), Root, false);
            auto* BrainNode = Node(Objective, UReditusQuestObjectiveComponent::StaticClass(), TEXT("QuestObjective"), nullptr, false);
            if (!Root || !IconNode || !BrainNode) return;
            CastChecked<UMapIconComponent>(IconNode->ComponentTemplate)->SetIconVisible(false);
            auto* Brain = CastChecked<UReditusQuestObjectiveComponent>(BrainNode->ComponentTemplate);
            Brain->DefinitionRow.DataTable = Objectives;
            if (!Compile(Objective)) return;
        }

        UE_LOG(LogReditusQuestNPCEditor, Display, TEXT("Ready: /Game/Reditus/Story contains BP_QuestNPC_New, BP_QuestObjective_New, DT_QuestNPCs and DT_QuestObjectives. Existing assets were not edited. Save All when ready."));
        ExportIntegration();
    }

    void CreateQuestSystem()
    {
        if (GEditor && GEditor->PlayWorld)
        { UE_LOG(LogReditusQuestNPCEditor, Error, TEXT("Stop Play before creating quest definitions.")); return; }
        if (!PathAvailable(QuestTablePackage))
        { UE_LOG(LogReditusQuestNPCEditor, Error, TEXT("DT_Quests path is occupied by an unrelated asset. Nothing overwritten.")); return; }
        const bool NewTable = LoadObject<UDataTable>(nullptr, *ObjectPath(QuestTablePackage), nullptr, LOAD_NoWarn | LOAD_Quiet) == nullptr;
        auto* Quests = Table(QuestTablePackage, FReditusQuestDefinition::StaticStruct());
        if (!Quests) return;
        if (NewTable)
        {
            FDataTableRowHandle Introduction; Introduction.DataTable = Quests; Introduction.RowName = TEXT("Test_Introduction");
            FDataTableRowHandle FollowUp; FollowUp.DataTable = Quests; FollowUp.RowName = TEXT("Test_FollowUp");
            FReditusQuestDefinition First;
            First.Title = FText::FromString(TEXT("A New Beginning"));
            First.Description = FText::FromString(TEXT("Speak to the guide, then reach the marked destination."));
            First.IsMainQuest = true;
            FReditusQuestStageDefinition Talk;
            Talk.StageID = TEXT("SpeakToGuide"); Talk.Description = FText::FromString(TEXT("Speak to the guide."));
            FReditusQuestTaskDefinition TalkTask;
            TalkTask.ObjectiveID = TEXT("TalkToGuide"); TalkTask.TargetID = TEXT("TestGuide");
            TalkTask.Description = Talk.Description;
            Talk.Objectives.Add(TalkTask); First.Stages.Add(Talk);
            FReditusQuestStageDefinition Reach;
            Reach.StageID = TEXT("FindDestination"); Reach.Description = FText::FromString(TEXT("Reach the destination."));
            FReditusQuestTaskDefinition ReachTask;
            ReachTask.ObjectiveID = TEXT("ReachMarker"); ReachTask.TargetID = TEXT("TestDestination");
            ReachTask.Kind = EReditusQuestObjectiveKind::ReachLocation; ReachTask.Description = Reach.Description;
            Reach.Objectives.Add(ReachTask); First.Stages.Add(Reach);
            First.StartQuestsOnCompletion.Add(FollowUp);
            FReditusQuestDefinition Second;
            Second.Title = FText::FromString(TEXT("The Next Step"));
            Second.Description = FText::FromString(TEXT("An example quest received after completing another quest."));
            Second.RequiredCompletedQuests.Add(Introduction);
            FReditusQuestStageDefinition Custom;
            Custom.StageID = TEXT("NextTask");
            FReditusQuestTaskDefinition CustomTask;
            CustomTask.ObjectiveID = TEXT("TestSignal"); CustomTask.TargetID = TEXT("FollowUpTest");
            CustomTask.Kind = EReditusQuestObjectiveKind::Custom;
            CustomTask.Description = FText::FromString(TEXT("Report the follow-up test event."));
            Custom.Objectives.Add(CustomTask); Second.Stages.Add(Custom);
            Quests->AddRow(Introduction.RowName, First); Quests->AddRow(FollowUp.RowName, Second);
            FReditusQuestDefinition WaitQuest;
            WaitQuest.Title = FText::FromString(TEXT("Wait One Minute"));
            WaitQuest.Description = FText::FromString(TEXT("Timer example: wait 60 seconds, then turn in from any Blueprint."));
            FReditusQuestStageDefinition WaitStage; WaitStage.StageID = TEXT("WaitForSignal");
            WaitStage.Description = FText::FromString(TEXT("Wait for the signal."));
            WaitStage.Timer.Mode = EReditusQuestTimerMode::Wait; WaitStage.Timer.DurationSeconds = 60;
            WaitStage.Timer.Label = WaitStage.Description;
            FReditusQuestTaskDefinition WaitTask; WaitTask.ObjectiveID = TEXT("Wait60");
            WaitTask.Kind = EReditusQuestObjectiveKind::TimerElapsed; WaitTask.Description = WaitStage.Description;
            WaitStage.Objectives.Add(WaitTask); WaitQuest.Stages.Add(WaitStage);
            Quests->AddRow(TEXT("Test_Wait60"), WaitQuest);
            FReditusQuestDefinition DeadlineQuest;
            DeadlineQuest.Title = FText::FromString(TEXT("Reach the Gate"));
            DeadlineQuest.Description = FText::FromString(TEXT("Timer example: reach TestGate within five minutes."));
            FReditusQuestStageDefinition DeadlineStage; DeadlineStage.StageID = TEXT("ReachGateInTime");
            DeadlineStage.Description = FText::FromString(TEXT("Reach the gate before time runs out."));
            DeadlineStage.Timer.Mode = EReditusQuestTimerMode::Deadline; DeadlineStage.Timer.DurationSeconds = 300;
            DeadlineStage.Timer.Label = DeadlineStage.Description;
            FReditusQuestTaskDefinition GateTask; GateTask.ObjectiveID = TEXT("ReachGate");
            GateTask.Kind = EReditusQuestObjectiveKind::ReachLocation; GateTask.TargetID = TEXT("TestGate");
            GateTask.Description = DeadlineStage.Description; DeadlineStage.Objectives.Add(GateTask);
            DeadlineQuest.Stages.Add(DeadlineStage); Quests->AddRow(TEXT("Test_Deadline300"), DeadlineQuest);
            FReditusQuestDefinition HuntQuest;
            HuntQuest.Title = FText::FromString(TEXT("Defeat the Death Boar"));
            HuntQuest.Description = FText::FromString(TEXT("Example boss objective. Report its confirmed defeat, not cleanup/destruction."));
            FReditusQuestStageDefinition HuntStage; HuntStage.StageID = TEXT("Hunt");
            FReditusQuestTaskDefinition HuntTask; HuntTask.ObjectiveID = TEXT("DefeatBoss");
            HuntTask.Kind = EReditusQuestObjectiveKind::DefeatEnemy; HuntTask.TargetID = TEXT("DeathBoar");
            HuntTask.Description = HuntQuest.Title; HuntStage.Description = HuntQuest.Title;
            HuntStage.Objectives.Add(HuntTask); HuntQuest.Stages.Add(HuntStage);
            Quests->AddRow(TEXT("Test_DefeatBoss"), HuntQuest);
            // Use the existing key-item row from the supplied project when available.
            if (auto* KeyItems = LoadObject<UDataTable>(nullptr, TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_KeyItems.DT_KeyItems")))
                if (KeyItems->GetRowMap().Contains(TEXT("Letter")))
                {
                    FReditusQuestDefinition ItemQuest;
                    ItemQuest.Title = FText::FromString(TEXT("Carry the Letter"));
                    ItemQuest.Description = FText::FromString(TEXT("Inventory example: possess the Letter key item. It is not consumed."));
                    FReditusQuestStageDefinition ItemStage; ItemStage.StageID = TEXT("PossessLetter");
                    ItemStage.Description = ItemQuest.Description;
                    FReditusQuestTaskDefinition ItemTask; ItemTask.ObjectiveID = TEXT("HaveLetter");
                    ItemTask.Kind = EReditusQuestObjectiveKind::CollectItem;
                    ItemTask.ItemRow.DataTable = KeyItems; ItemTask.ItemRow.RowName = TEXT("Letter");
                    ItemTask.Description = ItemQuest.Title; ItemStage.Objectives.Add(ItemTask);
                    ItemQuest.Stages.Add(ItemStage); Quests->AddRow(TEXT("Test_PossessLetter"), ItemQuest);
                }
            Quests->MarkPackageDirty();
        }
        // Add new upgrade examples only when missing. Never replace authored rows.
        if (!Quests->GetRowMap().Contains(TEXT("Test_RecruitRobot")))
        {
            FReditusQuestDefinition Recruit;
            Recruit.Title = FText::FromString(TEXT("Recruit a Robot"));
            Recruit.Description = FText::FromString(TEXT("Recruit the Heremus Rover through its successful dialogue or existing recruitment handler."));
            FReditusQuestStageDefinition Stage; Stage.StageID = TEXT("RecruitRobot"); Stage.Description = Recruit.Description;
            FReditusQuestTaskDefinition Task; Task.ObjectiveID = TEXT("RecruitRover");
            Task.Kind = EReditusQuestObjectiveKind::RecruitUnit; Task.TargetID = TEXT("HeremusRover"); Task.Description = Recruit.Description;
            Stage.Objectives.Add(Task); Recruit.Stages.Add(Stage);
            Quests->AddRow(TEXT("Test_RecruitRobot"), Recruit); Quests->MarkPackageDirty();
        }
        if (!Quests->GetRowMap().Contains(TEXT("Test_NPCChallenge")))
        {
            FReditusQuestDefinition Challenge;
            Challenge.Title = FText::FromString(TEXT("Win the Challenge"));
            Challenge.Description = FText::FromString(TEXT("Win the NPC encounter; escaping, losing or cancelling does not complete it."));
            FReditusQuestStageDefinition Stage; Stage.StageID = TEXT("WinChallenge"); Stage.Description = Challenge.Description;
            FReditusQuestTaskDefinition Task; Task.ObjectiveID = TEXT("WinEncounter");
            Task.Kind = EReditusQuestObjectiveKind::WinBattle; Task.TargetID = TEXT("NPCChallenge"); Task.Description = Challenge.Description;
            Stage.Objectives.Add(Task); Challenge.Stages.Add(Stage);
            Quests->AddRow(TEXT("Test_NPCChallenge"), Challenge); Quests->MarkPackageDirty();
        }
        CreateAssets();
        UE_LOG(LogReditusQuestNPCEditor, Display, TEXT("DT_Quests is ready. Select its row through the NPC's Quest row handle. Existing authored rows were preserved. Save All."));
    }
}

void CreateReditusQuestStepExamples();

class FReditusQuestNPCEditorModule : public IModuleInterface
{
    TUniquePtr<FAutoConsoleCommand> CreateCommand;
    TUniquePtr<FAutoConsoleCommand> ExportCommand;
    TUniquePtr<FAutoConsoleCommand> QuestCommand;
    TUniquePtr<FAutoConsoleCommand> JournalCommand;
    TUniquePtr<FAutoConsoleCommand> StepExamplesCommand;
public:
    virtual void StartupModule() override
    {
        CreateCommand = MakeUnique<FAutoConsoleCommand>(TEXT("Reditus.CreateQuestNPC"),
            TEXT("Create separate reusable NPC/objective Blueprints and their definition tables."),
            FConsoleCommandDelegate::CreateStatic(&ReditusQuestNPCEditor::CreateAssets));
        ExportCommand = MakeUnique<FAutoConsoleCommand>(TEXT("Reditus.ExportStoryIntegration"),
            TEXT("Read-only export of the actual dialogue widgets and current controller graphs."),
            FConsoleCommandDelegate::CreateStatic(&ReditusQuestNPCEditor::ExportIntegration));
        JournalCommand = MakeUnique<FAutoConsoleCommand>(TEXT("Reditus.IntegrateQuestJournal"),
            TEXT("Back up and connect existing BP_QuestDetails to the new quest system; keep Designer layout."),
            FConsoleCommandDelegate::CreateStatic(&IntegrateReditusQuestJournal));
        StepExamplesCommand = MakeUnique<FAutoConsoleCommand>(TEXT("Reditus.CreateQuestStepExamples"),
            TEXT("Add missing branching extortion examples; preserve authored rows and dialogue assets."),
            FConsoleCommandDelegate::CreateStatic(&CreateReditusQuestStepExamples));
        QuestCommand = MakeUnique<FAutoConsoleCommand>(TEXT("Reditus.CreateQuestSystem"),
            TEXT("Create DT_Quests with example quests; preserve existing definitions and NPCs."),
            FConsoleCommandDelegate::CreateStatic(&ReditusQuestNPCEditor::CreateQuestSystem));
    }
    virtual void ShutdownModule() override
    {
        StepExamplesCommand.Reset(); JournalCommand.Reset(); QuestCommand.Reset(); ExportCommand.Reset(); CreateCommand.Reset();
    }
};

IMPLEMENT_MODULE(FReditusQuestNPCEditorModule, ReditusQuestNPCEditor)
