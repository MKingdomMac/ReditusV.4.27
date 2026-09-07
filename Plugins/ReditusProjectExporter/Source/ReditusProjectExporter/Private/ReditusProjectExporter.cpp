#include "ReditusProjectExporter.h"

#include "ToolMenus.h"
#include "Framework/Commands/UIAction.h"

#include "AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "Engine/UserDefinedEnum.h"
#include "Engine/UserDefinedStruct.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "FReditusProjectExporterModule"

namespace ReditusProjectExporter
{
    static TSharedPtr<FJsonValue> StringValue(const FString& Value)
    {
        return MakeShared<FJsonValueString>(Value);
    }

    static FString PinDirectionToString(EEdGraphPinDirection Direction)
    {
        switch (Direction)
        {
        case EGPD_Input:
            return TEXT("Input");
        case EGPD_Output:
            return TEXT("Output");
        default:
            return TEXT("Unknown");
        }
    }
}

void FReditusProjectExporterModule::StartupModule()
{
    UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateRaw(
            this,
            &FReditusProjectExporterModule::RegisterMenus
        )
    );
}

void FReditusProjectExporterModule::ShutdownModule()
{
    UToolMenus::UnRegisterStartupCallback(this);
    UToolMenus::UnregisterOwner(this);
}

void FReditusProjectExporterModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);

    UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Window"));
    FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("ReditusProjectExporter"));

    Section.AddMenuEntry(
        TEXT("ReditusExportProjectForAnalysis"),
        LOCTEXT("ReditusExportProjectForAnalysis_Label", "Export Project For Analysis"),
        LOCTEXT(
            "ReditusExportProjectForAnalysis_Tooltip",
            "Exports project architecture, Blueprint graphs, assets, dependencies, source and config as text/JSON."
        ),
        FSlateIcon(),
        FUIAction(
            FExecuteAction::CreateRaw(
                this,
                &FReditusProjectExporterModule::ExportProjectForAnalysis
            )
        )
    );
}

void FReditusProjectExporterModule::ExportProjectForAnalysis()
{
    const FString OutputRoot = FPaths::Combine(
        FPaths::ProjectDir(),
        TEXT("ReditusProjectDump")
    );

    // Only our generated export folder is removed/recreated.
    IFileManager::Get().DeleteDirectory(*OutputRoot, false, true);
    IFileManager::Get().MakeDirectory(*OutputRoot, true);

    IFileManager::Get().MakeDirectory(*FPaths::Combine(OutputRoot, TEXT("Blueprints")), true);
    IFileManager::Get().MakeDirectory(*FPaths::Combine(OutputRoot, TEXT("DataTables")), true);
    IFileManager::Get().MakeDirectory(*FPaths::Combine(OutputRoot, TEXT("Enums")), true);
    IFileManager::Get().MakeDirectory(*FPaths::Combine(OutputRoot, TEXT("Structs")), true);

    FAssetRegistryModule& AssetRegistryModule =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
    AssetRegistry.SearchAllAssets(true);

    TArray<FAssetData> Assets;
    AssetRegistry.GetAssetsByPath(FName(TEXT("/Game")), Assets, true, false);

    FScopedSlowTask SlowTask(
        static_cast<float>(Assets.Num()),
        LOCTEXT("ReditusExportingProject", "Exporting Reditus project for analysis...")
    );
    SlowTask.MakeDialog(true);

    TArray<TSharedPtr<FJsonValue>> AssetArray;
    TSharedRef<FJsonObject> DependencyRoot = MakeShared<FJsonObject>();

    int32 BlueprintCount = 0;
    int32 DataTableCount = 0;
    int32 EnumCount = 0;
    int32 StructCount = 0;

    for (const FAssetData& AssetData : Assets)
    {
        SlowTask.EnterProgressFrame(1.0f, FText::FromName(AssetData.AssetName));

        const FString PackageName = AssetData.PackageName.ToString();
        const FString AssetClass = AssetData.AssetClass.ToString();

        TSharedRef<FJsonObject> AssetJson = MakeShared<FJsonObject>();
        AssetJson->SetStringField(TEXT("AssetName"), AssetData.AssetName.ToString());
        AssetJson->SetStringField(TEXT("PackageName"), PackageName);
        AssetJson->SetStringField(TEXT("PackagePath"), AssetData.PackagePath.ToString());
        AssetJson->SetStringField(TEXT("ObjectPath"), AssetData.ObjectPath.ToString());
        AssetJson->SetStringField(TEXT("AssetClass"), AssetClass);
        AssetArray.Add(MakeShared<FJsonValueObject>(AssetJson));

        TArray<FName> Dependencies;
        AssetRegistry.GetDependencies(
            AssetData.PackageName,
            Dependencies,
            EAssetRegistryDependencyType::All
        );

        TArray<TSharedPtr<FJsonValue>> DependencyArray;
        for (const FName& Dependency : Dependencies)
        {
            DependencyArray.Add(ReditusProjectExporter::StringValue(Dependency.ToString()));
        }
        DependencyRoot->SetArrayField(PackageName, DependencyArray);

        const bool bLooksLikeBlueprint =
            AssetClass.Contains(TEXT("Blueprint"), ESearchCase::IgnoreCase);

        if (bLooksLikeBlueprint)
        {
            if (UBlueprint* Blueprint = Cast<UBlueprint>(AssetData.GetAsset()))
            {
                ExportBlueprint(Blueprint, OutputRoot);
                ++BlueprintCount;
            }
            continue;
        }

        if (AssetClass.Equals(TEXT("DataTable"), ESearchCase::IgnoreCase))
        {
            if (UObject* Asset = AssetData.GetAsset())
            {
                ExportDataTable(Asset, PackageName, OutputRoot);
                ++DataTableCount;
            }
            continue;
        }

        if (AssetClass.Equals(TEXT("UserDefinedEnum"), ESearchCase::IgnoreCase))
        {
            if (UObject* Asset = AssetData.GetAsset())
            {
                ExportUserDefinedEnum(Asset, PackageName, OutputRoot);
                ++EnumCount;
            }
            continue;
        }

        if (AssetClass.Equals(TEXT("UserDefinedStruct"), ESearchCase::IgnoreCase))
        {
            if (UObject* Asset = AssetData.GetAsset())
            {
                ExportUserDefinedStruct(Asset, PackageName, OutputRoot);
                ++StructCount;
            }
            continue;
        }
    }

    TSharedRef<FJsonObject> AssetsRoot = MakeShared<FJsonObject>();
    AssetsRoot->SetArrayField(TEXT("Assets"), AssetArray);
    WriteJsonFile(FPaths::Combine(OutputRoot, TEXT("Assets.json")), AssetsRoot);

    WriteJsonFile(FPaths::Combine(OutputRoot, TEXT("Dependencies.json")), DependencyRoot);

    TSharedRef<FJsonObject> Manifest = MakeShared<FJsonObject>();
    Manifest->SetStringField(TEXT("ProjectName"), FApp::GetProjectName());
    Manifest->SetStringField(TEXT("EngineVersion"), FEngineVersion::Current().ToString());
    Manifest->SetStringField(TEXT("ExportedAtUtc"), FDateTime::UtcNow().ToIso8601());
    Manifest->SetNumberField(TEXT("GameAssetCount"), Assets.Num());
    Manifest->SetNumberField(TEXT("BlueprintCount"), BlueprintCount);
    Manifest->SetNumberField(TEXT("DataTableCount"), DataTableCount);
    Manifest->SetNumberField(TEXT("UserDefinedEnumCount"), EnumCount);
    Manifest->SetNumberField(TEXT("UserDefinedStructCount"), StructCount);
    WriteJsonFile(FPaths::Combine(OutputRoot, TEXT("ProjectManifest.json")), Manifest);

    ExportTextTrees(OutputRoot);

    const FString Readme =
        TEXT("Reditus Project Dump\n")
        TEXT("====================\n\n")
        TEXT("Generated by the ReditusProjectExporter editor plugin.\n")
        TEXT("This exporter reads project data and writes text/JSON only.\n\n")
        TEXT("Important files:\n")
        TEXT("- ProjectManifest.json\n")
        TEXT("- Assets.json\n")
        TEXT("- Dependencies.json\n")
        TEXT("- Blueprints/*.json\n")
        TEXT("- DataTables/*.csv\n")
        TEXT("- Enums/*.json\n")
        TEXT("- Structs/*.json\n")
        TEXT("- ProjectText/Source\n")
        TEXT("- ProjectText/Config\n")
        TEXT("- ProjectText/Plugins\n\n")
        TEXT("Zip this entire ReditusProjectDump folder and send it for analysis.\n");

    WriteTextFile(FPaths::Combine(OutputRoot, TEXT("README.txt")), Readme);

    FPlatformProcess::ExploreFolder(*OutputRoot);
}

void FReditusProjectExporterModule::ExportBlueprint(
    UBlueprint* Blueprint,
    const FString& OutputRoot
) const
{
    if (!Blueprint)
    {
        return;
    }

    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();

    Root->SetStringField(TEXT("Name"), Blueprint->GetName());
    Root->SetStringField(TEXT("Path"), Blueprint->GetPathName());
    Root->SetStringField(
        TEXT("ParentClass"),
        Blueprint->ParentClass ? Blueprint->ParentClass->GetPathName() : TEXT("")
    );
    Root->SetStringField(
        TEXT("GeneratedClass"),
        Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetPathName() : TEXT("")
    );

    TArray<TSharedPtr<FJsonValue>> Variables;

    for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
    {
        TSharedRef<FJsonObject> VarJson = MakeShared<FJsonObject>();
        VarJson->SetStringField(TEXT("Name"), Variable.VarName.ToString());
        VarJson->SetStringField(TEXT("Guid"), Variable.VarGuid.ToString());
        VarJson->SetStringField(TEXT("Category"), Variable.Category.ToString());
        VarJson->SetStringField(TEXT("FriendlyName"), Variable.FriendlyName);
        VarJson->SetStringField(TEXT("DefaultValue"), Variable.DefaultValue);
        VarJson->SetStringField(TEXT("PinCategory"), Variable.VarType.PinCategory.ToString());
        VarJson->SetStringField(TEXT("PinSubCategory"), Variable.VarType.PinSubCategory.ToString());

        if (Variable.VarType.PinSubCategoryObject.IsValid())
        {
            VarJson->SetStringField(
                TEXT("PinSubCategoryObject"),
                Variable.VarType.PinSubCategoryObject.Get()->GetPathName()
            );
        }
        else
        {
            VarJson->SetStringField(TEXT("PinSubCategoryObject"), TEXT(""));
        }

        Variables.Add(MakeShared<FJsonValueObject>(VarJson));
    }

    Root->SetArrayField(TEXT("Variables"), Variables);

    TArray<TSharedPtr<FJsonValue>> Components;
    if (Blueprint->SimpleConstructionScript)
    {
        const TArray<USCS_Node*>& SCSNodes = Blueprint->SimpleConstructionScript->GetAllNodes();
        for (USCS_Node* SCSNode : SCSNodes)
        {
            if (!SCSNode)
            {
                continue;
            }

            TSharedRef<FJsonObject> ComponentJson = MakeShared<FJsonObject>();
            ComponentJson->SetStringField(TEXT("VariableName"), SCSNode->GetVariableName().ToString());
            ComponentJson->SetStringField(TEXT("SCSNodeName"), SCSNode->GetName());
            Components.Add(MakeShared<FJsonValueObject>(ComponentJson));
        }
    }
    Root->SetArrayField(TEXT("Components"), Components);

    TArray<TSharedPtr<FJsonValue>> Graphs;

    auto AddGraphs = [this, &Graphs](const TArray<UEdGraph*>& InGraphs, const FString& GraphKind)
    {
        for (UEdGraph* Graph : InGraphs)
        {
            if (!Graph)
            {
                continue;
            }

            TSharedRef<FJsonObject> GraphJson = MakeShared<FJsonObject>();
            GraphJson->SetStringField(TEXT("Kind"), GraphKind);
            ExportGraph(Graph, GraphJson);
            Graphs.Add(MakeShared<FJsonValueObject>(GraphJson));
        }
    };

    AddGraphs(Blueprint->UbergraphPages, TEXT("Ubergraph"));
    AddGraphs(Blueprint->FunctionGraphs, TEXT("Function"));
    AddGraphs(Blueprint->MacroGraphs, TEXT("Macro"));
    AddGraphs(Blueprint->DelegateSignatureGraphs, TEXT("DelegateSignature"));

    Root->SetArrayField(TEXT("Graphs"), Graphs);

    const FString Filename = FPaths::Combine(
        OutputRoot,
        TEXT("Blueprints"),
        SafeAssetFilename(Blueprint->GetPathName()) + TEXT(".json")
    );

    WriteJsonFile(Filename, Root);
}

void FReditusProjectExporterModule::ExportGraph(
    UEdGraph* Graph,
    TSharedRef<FJsonObject> GraphJson
) const
{
    GraphJson->SetStringField(TEXT("Name"), Graph->GetName());
    GraphJson->SetStringField(TEXT("Path"), Graph->GetPathName());

    TArray<TSharedPtr<FJsonValue>> Nodes;

    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (!Node)
        {
            continue;
        }

        TSharedRef<FJsonObject> NodeJson = MakeShared<FJsonObject>();
        NodeJson->SetStringField(TEXT("Name"), Node->GetName());
        NodeJson->SetStringField(TEXT("Class"), Node->GetClass()->GetPathName());
        NodeJson->SetStringField(TEXT("Title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
        NodeJson->SetStringField(TEXT("Guid"), Node->NodeGuid.ToString());
        NodeJson->SetNumberField(TEXT("PosX"), Node->NodePosX);
        NodeJson->SetNumberField(TEXT("PosY"), Node->NodePosY);

        TArray<TSharedPtr<FJsonValue>> Pins;

        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin)
            {
                continue;
            }

            TSharedRef<FJsonObject> PinJson = MakeShared<FJsonObject>();
            PinJson->SetStringField(TEXT("Name"), Pin->PinName.ToString());
            PinJson->SetStringField(TEXT("Direction"), ReditusProjectExporter::PinDirectionToString(Pin->Direction));
            PinJson->SetStringField(TEXT("Category"), Pin->PinType.PinCategory.ToString());
            PinJson->SetStringField(TEXT("SubCategory"), Pin->PinType.PinSubCategory.ToString());
            PinJson->SetStringField(TEXT("DefaultValue"), Pin->DefaultValue);
            PinJson->SetStringField(
                TEXT("DefaultObject"),
                Pin->DefaultObject ? Pin->DefaultObject->GetPathName() : TEXT("")
            );

            TArray<TSharedPtr<FJsonValue>> LinkedTo;
            for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
            {
                if (!LinkedPin)
                {
                    continue;
                }

                UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
                const FString Link =
                    (LinkedNode ? LinkedNode->GetName() : TEXT("<NoNode>")) +
                    TEXT(".") +
                    LinkedPin->PinName.ToString();

                LinkedTo.Add(ReditusProjectExporter::StringValue(Link));
            }

            PinJson->SetArrayField(TEXT("LinkedTo"), LinkedTo);
            Pins.Add(MakeShared<FJsonValueObject>(PinJson));
        }

        NodeJson->SetArrayField(TEXT("Pins"), Pins);
        Nodes.Add(MakeShared<FJsonValueObject>(NodeJson));
    }

    GraphJson->SetArrayField(TEXT("Nodes"), Nodes);
}

void FReditusProjectExporterModule::ExportDataTable(
    UObject* Asset,
    const FString& PackageName,
    const FString& OutputRoot
) const
{
    if (UDataTable* DataTable = Cast<UDataTable>(Asset))
    {
        WriteTextFile(
            FPaths::Combine(
                OutputRoot,
                TEXT("DataTables"),
                SafeAssetFilename(PackageName) + TEXT(".csv")
            ),
            DataTable->GetTableAsCSV()
        );
    }
}

void FReditusProjectExporterModule::ExportUserDefinedEnum(
    UObject* Asset,
    const FString& PackageName,
    const FString& OutputRoot
) const
{
    UUserDefinedEnum* Enum = Cast<UUserDefinedEnum>(Asset);
    if (!Enum)
    {
        return;
    }

    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("Name"), Enum->GetName());
    Root->SetStringField(TEXT("Path"), Enum->GetPathName());

    TArray<TSharedPtr<FJsonValue>> Entries;
    for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
    {
        TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("Name"), Enum->GetNameStringByIndex(Index));
        Entry->SetStringField(TEXT("DisplayName"), Enum->GetDisplayNameTextByIndex(Index).ToString());
        Entry->SetNumberField(TEXT("Value"), Enum->GetValueByIndex(Index));
        Entries.Add(MakeShared<FJsonValueObject>(Entry));
    }

    Root->SetArrayField(TEXT("Entries"), Entries);

    WriteJsonFile(
        FPaths::Combine(OutputRoot, TEXT("Enums"), SafeAssetFilename(PackageName) + TEXT(".json")),
        Root
    );
}

void FReditusProjectExporterModule::ExportUserDefinedStruct(
    UObject* Asset,
    const FString& PackageName,
    const FString& OutputRoot
) const
{
    UUserDefinedStruct* Struct = Cast<UUserDefinedStruct>(Asset);
    if (!Struct)
    {
        return;
    }

    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("Name"), Struct->GetName());
    Root->SetStringField(TEXT("Path"), Struct->GetPathName());

    TArray<TSharedPtr<FJsonValue>> Fields;
    for (TFieldIterator<FProperty> It(Struct); It; ++It)
    {
        FProperty* Property = *It;
        if (!Property)
        {
            continue;
        }

        TSharedRef<FJsonObject> Field = MakeShared<FJsonObject>();
        Field->SetStringField(TEXT("Name"), Property->GetName());
        Field->SetStringField(TEXT("Type"), Property->GetClass()->GetName());
        Field->SetNumberField(TEXT("ArrayDim"), Property->ArrayDim);
        Fields.Add(MakeShared<FJsonValueObject>(Field));
    }

    Root->SetArrayField(TEXT("Fields"), Fields);

    WriteJsonFile(
        FPaths::Combine(OutputRoot, TEXT("Structs"), SafeAssetFilename(PackageName) + TEXT(".json")),
        Root
    );
}

void FReditusProjectExporterModule::ExportTextTrees(const FString& OutputRoot) const
{
    const FString ProjectTextRoot = FPaths::Combine(OutputRoot, TEXT("ProjectText"));
    IFileManager::Get().MakeDirectory(*ProjectTextRoot, true);

    CopyTextTree(
        FPaths::Combine(FPaths::ProjectDir(), TEXT("Source")),
        FPaths::Combine(ProjectTextRoot, TEXT("Source"))
    );

    CopyTextTree(
        FPaths::Combine(FPaths::ProjectDir(), TEXT("Config")),
        FPaths::Combine(ProjectTextRoot, TEXT("Config"))
    );

    CopyTextTree(
        FPaths::Combine(FPaths::ProjectDir(), TEXT("Plugins")),
        FPaths::Combine(ProjectTextRoot, TEXT("Plugins"))
    );

    const FString ProjectFile = FPaths::GetProjectFilePath();
    if (!ProjectFile.IsEmpty() && FPaths::FileExists(ProjectFile))
    {
        const FString DestProjectFile = FPaths::Combine(
            ProjectTextRoot,
            FPaths::GetCleanFilename(ProjectFile)
        );
        IFileManager::Get().Copy(*DestProjectFile, *ProjectFile);
    }
}

void FReditusProjectExporterModule::CopyTextTree(
    const FString& SourceRoot,
    const FString& DestRoot
) const
{
    if (!FPaths::DirectoryExists(SourceRoot))
    {
        return;
    }

    TArray<FString> Files;
    IFileManager::Get().FindFilesRecursive(
        Files,
        *SourceRoot,
        TEXT("*.*"),
        true,
        false,
        false
    );

    const TSet<FString> AllowedExtensions = {
        TEXT("h"),
        TEXT("hpp"),
        TEXT("cpp"),
        TEXT("c"),
        TEXT("cs"),
        TEXT("ini"),
        TEXT("json"),
        TEXT("uplugin"),
        TEXT("uproject"),
        TEXT("txt"),
        TEXT("xml")
    };

    for (const FString& SourceFile : Files)
    {
        const FString Extension = FPaths::GetExtension(SourceFile, false).ToLower();
        if (!AllowedExtensions.Contains(Extension))
        {
            continue;
        }

        FString RelativePath = SourceFile;
        if (!FPaths::MakePathRelativeTo(RelativePath, *SourceRoot))
        {
            continue;
        }

        const FString DestFile = FPaths::Combine(DestRoot, RelativePath);
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(DestFile), true);
        IFileManager::Get().Copy(*DestFile, *SourceFile);
    }
}

bool FReditusProjectExporterModule::WriteJsonFile(
    const FString& Filename,
    const TSharedRef<FJsonObject>& JsonObject
) const
{
    FString Output;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);

    if (!FJsonSerializer::Serialize(JsonObject, Writer))
    {
        return false;
    }

    return WriteTextFile(Filename, Output);
}

bool FReditusProjectExporterModule::WriteTextFile(
    const FString& Filename,
    const FString& Text
) const
{
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);

    return FFileHelper::SaveStringToFile(
        Text,
        *Filename,
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM
    );
}

FString FReditusProjectExporterModule::SafeAssetFilename(const FString& InPath) const
{
    FString Result = InPath;
    Result.ReplaceInline(TEXT("/"), TEXT("__"));
    Result.ReplaceInline(TEXT("\\"), TEXT("__"));
    Result.ReplaceInline(TEXT(":"), TEXT("_"));
    Result.ReplaceInline(TEXT("."), TEXT("_"));

    while (Result.StartsWith(TEXT("__")))
    {
        Result.RightChopInline(2);
    }

    return Result;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FReditusProjectExporterModule, ReditusProjectExporter)
