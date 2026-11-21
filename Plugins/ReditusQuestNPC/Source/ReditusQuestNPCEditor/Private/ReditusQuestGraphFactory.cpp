#include "ReditusQuestGraphFactory.h"
#include "ReditusQuestGraph.h"
#include "ReditusQuestGraphCompiler.h"
#include "ReditusQuestEditorToolkit.h"
#include "AssetToolsModule.h"
#include "AssetTypeActions_Base.h"
#include "Editor.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "ReditusQuestGraphFactory"

UReditusQuestGraphFactory::UReditusQuestGraphFactory()
{
    SupportedClass = UReditusQuestAsset::StaticClass();
    bCreateNew = true; bEditAfterNew = true;
}

UObject* UReditusQuestGraphFactory::FactoryCreateNew(UClass* Class, UObject* Parent, FName Name, EObjectFlags Flags,
    UObject* Context, FFeedbackContext* Warn)
{
    (void)Context; (void)Warn;
    if (GEditor && GEditor->PlayWorld) return nullptr;
    auto* Asset = NewObject<UReditusQuestAsset>(Parent, Class, Name, Flags | RF_Transactional);
    Asset->QuestID = Name; Asset->Settings.Title = FText::FromName(Name);
    Asset->EditorGraph = NewObject<UReditusQuestEdGraph>(Asset, TEXT("QuestGraph"), RF_Transactional);
    Asset->EditorGraph->Schema = UReditusQuestGraphSchema::StaticClass();
    Asset->EditorGraph->GetSchema()->CreateDefaultNodesForGraph(*Asset->EditorGraph);
    FString Error;
    if (!ReditusQuestGraphCompiler::Compile(Asset, Error))
        UE_LOG(LogTemp, Warning, TEXT("Quest graph created as an uncompiled draft: %s"), *Error);
    return Asset;
}

namespace
{
    class FQuestAssetActions : public FAssetTypeActions_Base
    {
        EAssetTypeCategories::Type Category;
    public:
        explicit FQuestAssetActions(EAssetTypeCategories::Type InCategory) : Category(InCategory) {}
        virtual FText GetName() const override { return LOCTEXT("AssetName", "Reditus Quest Graph"); }
        virtual FColor GetTypeColor() const override { return FColor(40, 105, 155); }
        virtual UClass* GetSupportedClass() const override { return UReditusQuestAsset::StaticClass(); }
        virtual uint32 GetCategories() override { return Category; }
        virtual void OpenAssetEditor(const TArray<UObject*>& Objects, TSharedPtr<IToolkitHost> Host) override
        {
            for (auto* Object : Objects) if (auto* Asset = Cast<UReditusQuestAsset>(Object))
                MakeShared<FReditusQuestEditorToolkit>()->Init(Asset, Host);
        }
    };
    TSharedPtr<FQuestAssetActions> QuestGraphAssetTypeActions;
}

void RegisterReditusQuestGraphEditor()
{
    auto& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    const auto Category = AssetTools.RegisterAdvancedAssetCategory(TEXT("Reditus"), LOCTEXT("Category", "Reditus"));
    QuestGraphAssetTypeActions = MakeShared<FQuestAssetActions>(Category);
    AssetTools.RegisterAssetTypeActions(QuestGraphAssetTypeActions.ToSharedRef());
}

void UnregisterReditusQuestGraphEditor()
{
    if (QuestGraphAssetTypeActions.IsValid() && FModuleManager::Get().IsModuleLoaded(TEXT("AssetTools")))
        FModuleManager::GetModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().UnregisterAssetTypeActions(QuestGraphAssetTypeActions.ToSharedRef());
    QuestGraphAssetTypeActions.Reset();
}

#undef LOCTEXT_NAMESPACE
