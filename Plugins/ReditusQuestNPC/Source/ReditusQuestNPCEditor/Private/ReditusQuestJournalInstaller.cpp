#include "ReditusQuestUI.h"
#include "Blueprint/WidgetTree.h"
#include "WidgetBlueprint.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Self.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/DateTime.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "UObject/MetaData.h"

namespace
{
    FString Normalized(FName Name)
    { return Name.ToString().Replace(TEXT(" "), TEXT("")).ToLower(); }
    struct Hook
    {
        UK2Node_CustomEvent* Event = nullptr;
        UEdGraphPin* Exec = nullptr;
        UK2Node_CallFunction* Call = nullptr;
        UK2Node_Self* Self = nullptr;
        FString Page;
    };
}

// Explicit editor command. Keeps all Designer widgets and old detached graph nodes
// for review. Only six legacy quest-data event bodies are rerouted.
void IntegrateReditusQuestJournal()
{
    if (GEditor && GEditor->PlayWorld)
    { UE_LOG(LogTemp, Error, TEXT("Stop Play before integrating the quest journal.")); return; }
    const TCHAR* Path = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/UI/MainMenu/BP_QuestDetails.BP_QuestDetails");
    auto* BP = LoadObject<UWidgetBlueprint>(nullptr, Path);
    if (!BP || !BP->WidgetTree)
    { UE_LOG(LogTemp, Error, TEXT("BP_QuestDetails was not found. No changes made.")); return; }
    auto* Metadata = BP->GetOutermost()->GetMetaData();
    if (FString(Metadata->GetValue(BP, TEXT("ReditusQuestJournal2"))) == TEXT("1"))
    { UE_LOG(LogTemp, Display, TEXT("Quest journal is already integrated. Designer layout is preserved.")); return; }
    for (const TCHAR* Name : {TEXT("QuestList"), TEXT("QuestName"), TEXT("QuestDescription"), TEXT("QuestBar"), TEXT("ProgressDetails")})
        if (!BP->WidgetTree->FindWidget(FName(Name)))
        { UE_LOG(LogTemp, Error, TEXT("Expected quest widget %s is missing. No changes made."), Name); return; }
    TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
    const TCHAR* Names[] = {TEXT("ShowMainQuests"), TEXT("ShowSideQuests"), TEXT("ShowCompletedQuest"),
        TEXT("HighlightSelectedQuest"), TEXT("ChangeHighlightedQuest"), TEXT("ShowProgress")};
    TArray<Hook> Hooks;
    for (int32 I = 0; I < 6; ++I)
    {
        Hook H; int32 Matches = 0;
        for (auto* Graph : Graphs) for (auto* Node : Graph->Nodes)
            if (auto* Event = Cast<UK2Node_CustomEvent>(Node))
                if (Normalized(Event->CustomFunctionName) == Normalized(FName(Names[I])))
                { H.Event = Event; ++Matches; }
        H.Exec = H.Event ? H.Event->FindPin(UEdGraphSchema_K2::PN_Then) : nullptr;
        if (Matches != 1 || !H.Exec)
        { UE_LOG(LogTemp, Error, TEXT("Expected exactly one %s event. No changes made."), Names[I]); return; }
        if (I < 3) H.Page = StaticEnum<EReditusQuestJournalPage>()->GetNameByValue(I).ToString();
        Hooks.Add(H);
    }
    FString Filename;
    if (!FPackageName::DoesPackageExist(BP->GetOutermost()->GetName(), nullptr, &Filename))
    { UE_LOG(LogTemp, Error, TEXT("Save BP_QuestDetails before integrating. No changes made.")); return; }
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("ReditusQuestNPC/Backups") / FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
    IFileManager::Get().MakeDirectory(*Folder, true);
    if (IFileManager::Get().Copy(*(Folder / TEXT("BP_QuestDetails.uasset")), *Filename) != COPY_OK)
    { UE_LOG(LogTemp, Error, TEXT("Could not back up BP_QuestDetails. No changes made.")); return; }
    const FScopedTransaction Transaction(NSLOCTEXT("ReditusQuestNPC", "IntegrateJournal", "Connect new quests to existing journal"));
    BP->Modify();
    bool Valid = true;
    for (auto& H : Hooks)
    {
        auto* Graph = H.Event->GetGraph(); Graph->Modify(); H.Event->Modify();
        H.Call = NewObject<UK2Node_CallFunction>(Graph);
        H.Call->SetFromFunction(UReditusQuestUILibrary::StaticClass()->FindFunctionByName(H.Page.IsEmpty()
            ? GET_FUNCTION_NAME_CHECKED(UReditusQuestUILibrary, SelectQuestJournalEntry)
            : GET_FUNCTION_NAME_CHECKED(UReditusQuestUILibrary, ShowQuestJournalPage)));
        Graph->AddNode(H.Call, false, false); H.Call->CreateNewGuid(); H.Call->AllocateDefaultPins();
        H.Call->NodePosX = H.Event->NodePosX + 330; H.Call->NodePosY = H.Event->NodePosY - 80;
        H.Call->NodeComment = TEXT("Reditus 2: existing artwork, new quest state"); H.Call->bCommentBubbleVisible = true;
        H.Self = NewObject<UK2Node_Self>(Graph); Graph->AddNode(H.Self, false, false);
        H.Self->CreateNewGuid(); H.Self->AllocateDefaultPins();
        H.Self->NodePosX = H.Event->NodePosX + 140; H.Self->NodePosY = H.Event->NodePosY + 110;
        const auto* Schema = GetDefault<UEdGraphSchema_K2>();
        auto* JournalPin = H.Call->FindPin(TEXT("Journal"));
        if (!JournalPin || !Schema->TryCreateConnection(H.Self->FindPin(UEdGraphSchema_K2::PN_Self), JournalPin)) Valid = false;
        if (!H.Page.IsEmpty())
        {
            auto* PagePin = H.Call->FindPin(TEXT("Page"));
            if (!PagePin) Valid = false; else Schema->TrySetDefaultValue(*PagePin, H.Page);
        }
    }
    if (!Valid)
    {
        for (auto& H : Hooks) { H.Call->DestroyNode(); H.Self->DestroyNode(); }
        UE_LOG(LogTemp, Error, TEXT("Journal node preflight failed. Original event connections are intact.")); return;
    }
    TArray<TArray<UEdGraphPin*>> Previous;
    bool Connected = true;
    for (auto& H : Hooks)
    {
        Previous.Add(H.Exec->LinkedTo); H.Exec->BreakAllPinLinks();
        if (!GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(H.Exec, H.Call->GetExecPin())) Connected = false;
    }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    if (Connected) FKismetEditorUtilities::CompileBlueprint(BP);
    if (!Connected || BP->Status == BS_Error)
    {
        for (int32 I = 0; I < Hooks.Num(); ++I)
        {
            Hooks[I].Exec->BreakAllPinLinks();
            for (auto* Pin : Previous[I]) Hooks[I].Exec->MakeLinkTo(Pin);
            Hooks[I].Call->DestroyNode(); Hooks[I].Self->DestroyNode();
        }
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP); FKismetEditorUtilities::CompileBlueprint(BP);
        UE_LOG(LogTemp, Error, TEXT("Journal compile failed; original connections restored. Review Compiler Results.")); return;
    }
    Metadata->SetValue(BP, TEXT("ReditusQuestJournal2"), TEXT("1")); BP->MarkPackageDirty();
    UE_LOG(LogTemp, Display, TEXT("BP_QuestDetails connected. Map artwork, panels, Back button and menu input retained. Backup: %s. Test, then Save All."), *Folder);
}
