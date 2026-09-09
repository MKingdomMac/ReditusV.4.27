#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class UBlueprint;
class UEdGraph;
class FJsonObject;

class FReditusProjectExporterModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    void RegisterMenus();
    void ExportProjectForAnalysis();

    void ExportBlueprint(UBlueprint* Blueprint, const FString& OutputRoot) const;
    void ExportGraph(UEdGraph* Graph, TSharedRef<FJsonObject> GraphJson) const;

    void ExportDataTable(UObject* Asset, const FString& PackageName, const FString& OutputRoot) const;
    void ExportUserDefinedEnum(UObject* Asset, const FString& PackageName, const FString& OutputRoot) const;
    void ExportUserDefinedStruct(UObject* Asset, const FString& PackageName, const FString& OutputRoot) const;

    void ExportTextTrees(const FString& OutputRoot) const;
    void CopyTextTree(const FString& SourceRoot, const FString& DestRoot) const;

    bool WriteJsonFile(const FString& Filename, const TSharedRef<FJsonObject>& JsonObject) const;
    bool WriteTextFile(const FString& Filename, const FString& Text) const;

    FString SafeAssetFilename(const FString& InPath) const;
};
