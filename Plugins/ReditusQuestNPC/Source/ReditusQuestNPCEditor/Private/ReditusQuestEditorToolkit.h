#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "UObject/GCObject.h"
#include "EditorUndoClient.h"
#include "GraphEditor.h"

class UReditusQuestAsset;
class IDetailsView;
class SVerticalBox;
class SDockTab;
struct FPropertyChangedEvent;

class FReditusQuestEditorToolkit : public FAssetEditorToolkit, public FGCObject, public FEditorUndoClient
{
public:
    virtual ~FReditusQuestEditorToolkit();
    void Init(UReditusQuestAsset* Asset, const TSharedPtr<IToolkitHost>& Host);
    virtual FName GetToolkitFName() const override;
    virtual FText GetBaseToolkitName() const override;
    virtual FString GetWorldCentricTabPrefix() const override;
    virtual FLinearColor GetWorldCentricTabColorScale() const override;
    virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& Manager) override;
    virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& Manager) override;
    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual FString GetReferencerName() const override { return TEXT("FReditusQuestEditorToolkit"); }
    virtual void PostUndo(bool Success) override;
    virtual void PostRedo(bool Success) override;
    virtual void SaveAsset_Execute() override;
private:
    UReditusQuestAsset* Quest = nullptr;
    TSharedPtr<SGraphEditor> Graph;
    TSharedPtr<IDetailsView> Details;
    TSharedPtr<SVerticalBox> ConversationPanel;
    TSharedPtr<FUICommandList> GraphCommands;
    TSharedPtr<FWorkspaceItem> MenuGroup;
    FString Status;
    bool LastCompileSucceeded = false;
    TSharedRef<SDockTab> SpawnGraph(const FSpawnTabArgs& Args);
    TSharedRef<SDockTab> SpawnDetails(const FSpawnTabArgs& Args);
    TSharedRef<SDockTab> SpawnConversations(const FSpawnTabArgs& Args);
    void SelectionChanged(const TSet<UObject*>& Selection);
    void RefreshConversations();
    void PropertiesChanged(const FPropertyChangedEvent& Event);
    void Compile();
    bool CanEdit() const;
    void DeleteSelected();
    bool CanDelete() const;
};
