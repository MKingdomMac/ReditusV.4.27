#include "ReditusEnemyDeathVisuals.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CallParentFunction.h"
#include "K2Node_Event.h"
#include "K2Node_Self.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompiler.h"
#include "MaterialExpressionSpriteTextureSampler.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "UObject/MetaData.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogReditusEnemyDeathInstall, Log, All);
namespace ReditusEnemyDeathEditor
{
    const TCHAR* MaterialPackage = TEXT("/Game/Reditus/EnemyDeath/M_CrimsonDeath");
    const TCHAR* MaterialPath = TEXT("/Game/Reditus/EnemyDeath/M_CrimsonDeath.M_CrimsonDeath");
    const TCHAR* EnemyPath = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Units/EnemyUnits/BP_EnemyUnitBase.BP_EnemyUnitBase");
    const TCHAR* TablePath = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_Enemies.DT_Enemies");

    template<class T> T* Expression(UMaterial* M, int32 X, int32 Y)
    {
        T* E = NewObject<T>(M); E->Material = M; E->MaterialExpressionEditorX = X; E->MaterialExpressionEditorY = Y;
        M->Expressions.Add(E); return E;
    }
    void Input(UMaterialExpressionCustom* E, FName Name, UMaterialExpression* Source, int32 Output = 0)
    {
        FCustomInput I; I.InputName = Name; I.Input.Connect(Output, Source); E->Inputs.Add(I);
    }
    const TCHAR* LegacyShaderCode()
    {
        return TEXT(R"RDSHADER(
float2 uv = saturate((TextureUV - UVBounds.rg) / max(UVBounds.ba, float2(0.000001, 0.000001)));
uint cols = (uint)clamp(Columns, 2.0, 6.0);
uint rows = (uint)clamp(Rows, 2.0, 6.0);
uint2 cell = min((uint2)floor(uv * float2(cols, rows)), uint2(cols - 1u, rows - 1u));
uint h = cell.x * 1973u + cell.y * 9277u + (uint)Seed * 26699u;
h = (h ^ 61u) ^ (h >> 16); h *= 9u; h ^= h >> 4; h *= 0x27d4eb2du; h ^= h >> 15;
float start = 0.08 + ((float)(h & 65535u) / 65535.0) * 0.65;
float fade = saturate(1.0 - (Progress - start) / 0.16);
float cut = 1.0;
float red = saturate(Progress * 1.8 + Flash * 0.65);
if (TileIndex >= 0.0)
{
    uint index = (uint)TileIndex;
    cut = (cell.x == index % cols && cell.y == index / cols) ? 1.0 : 0.0;
    fade = 1.0;
    red = 0.88;
}
float3 rgb = lerp(Tex.rgb * SourceTint.rgb, CrimsonTint.rgb, red);
rgb += CrimsonTint.rgb * max(0.0, Flash) * 1.5;
return float4(rgb, saturate(Tex.a * SourceTint.a * cut * fade * Opacity));
)RDSHADER");
    }
    const TCHAR* DirectionalShaderCode()
    {
        return TEXT(R"RDSHADER(
float2 uv = saturate((TextureUV - UVBounds.rg) / max(UVBounds.ba, float2(0.000001, 0.000001)));
uint2 pixel = (uint2)floor(uv * max(PixelGrid.rg, float2(1.0, 1.0)));
uint h = pixel.x * 1973u + pixel.y * 9277u + (uint)Seed * 26699u;
h = (h ^ 61u) ^ (h >> 16); h *= 9u; h ^= h >> 4; h *= 0x27d4eb2du; h ^= h >> 15;
float noise = (float)(h & 65535u) / 65535.0;
float fromFront = FrontOnRight >= 0.5 ? 1.0 - uv.x : uv.x;
float distance = fromFront + (noise - 0.5) * 0.045 - (saturate(Progress) * 1.12 - 0.06);
float fade = smoothstep(-0.018, 0.018, distance);
float edge = 1.0 - smoothstep(0.0, 0.045, abs(distance));
float red = saturate(edge * 0.88 + Flash * 0.65 + Progress * 0.16);
if (TileIndex >= 0.0)
{
    float2 q = (uv - ParticlePatch.rg) / max(ParticlePatch.ba, float2(0.000001, 0.000001));
    fade = saturate((1.0 - dot(q, q)) * 2.0) * (0.3 + noise * 0.7);
    red = 1.0;
}
float3 rgb = lerp(Tex.rgb * SourceTint.rgb, CrimsonTint.rgb, red);
rgb += CrimsonTint.rgb * max(0.0, Flash) * 1.5;
return float4(rgb, saturate(Tex.a * SourceTint.a * fade * Opacity));
)RDSHADER");
    }
    void ConfigureDirectionalShader(UMaterial* M, UMaterialExpressionCustom* Code)
    {
        auto Vector = [&](const TCHAR* Name, FLinearColor Value)
        {
            auto* E = Expression<UMaterialExpressionVectorParameter>(M, -1000, 40 + Code->Inputs.Num()*75);
            E->ParameterName = Name; E->DefaultValue = Value;
            auto* Four = Expression<UMaterialExpressionAppendVector>(M, -720, E->MaterialExpressionEditorY);
            Four->A.Connect(0, E); Four->B.Connect(4, E); Input(Code, Name, Four);
        };
        Vector(TEXT("PixelGrid"), FLinearColor(128, 128, 0, 0));
        Vector(TEXT("ParticlePatch"), FLinearColor(.5f, .5f, .02f, .04f));
        auto* Front = Expression<UMaterialExpressionScalarParameter>(M, -1000, 40 + Code->Inputs.Num()*75);
        Front->ParameterName = TEXT("FrontOnRight"); Front->DefaultValue = 1.f;
        Input(Code, TEXT("FrontOnRight"), Front);
        Code->Code = DirectionalShaderCode();
        Code->Desc = TEXT("Reditus crimson sprite death v2 front to back");
    }
    UMaterial* MakeMaterial()
    {
        UObject* Existing = LoadObject<UObject>(nullptr, MaterialPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (Existing)
        {
            auto* M = Cast<UMaterial>(Existing);
            const FString Version = M ? FString(M->GetOutermost()->GetMetaData()->GetValue(M, TEXT("ReditusEnemyDeathVersion"))) : FString();
            if (!M || (Version != TEXT("1") && Version != TEXT("2")))
            {
                UE_LOG(LogReditusEnemyDeathInstall, Error, TEXT("M_CrimsonDeath path is occupied by an unrelated asset. Nothing overwritten."));
                return nullptr;
            }
            if (Version == TEXT("2")) return M; // Repeat updates preserve authored edits.
            UMaterialExpressionCustom* Code = nullptr;
            for (auto* E : M->Expressions)
                if (auto* Candidate = Cast<UMaterialExpressionCustom>(E))
                    if (Candidate->Desc == TEXT("Reditus crimson sprite death v1"))
                    {
                        if (Code) return nullptr;
                        Code = Candidate;
                    }
            if (!Code || Code->Code != LegacyShaderCode() || Code->Inputs.Num() != 12)
            {
                UE_LOG(LogReditusEnemyDeathInstall, Error, TEXT("Generated v1 death shader has custom edits; automatic visual upgrade skipped. No material or Blueprint changes made."));
                return nullptr;
            }
            // Update only the owned shader and its new inputs; preserve other material edits.
            M->Modify(); Code->Modify();
            ConfigureDirectionalShader(M, Code);
            M->GetOutermost()->GetMetaData()->SetValue(M, TEXT("ReditusEnemyDeathVersion"), TEXT("2"));
            M->PostEditChange(); M->MarkPackageDirty();
            return M;
        }
        if (FPackageName::DoesPackageExist(MaterialPackage)) return nullptr;
        auto* Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
        if (!Texture) return nullptr;
        auto* Package = CreatePackage(MaterialPackage);
        auto* M = NewObject<UMaterial>(Package, TEXT("M_CrimsonDeath"), RF_Public | RF_Standalone | RF_Transactional);
        M->BlendMode = BLEND_Translucent; M->TwoSided = true; M->SetShadingModel(MSM_Unlit);
        auto* Tex = Expression<UMaterialExpressionSpriteTextureSampler>(M, -1000, -500);
        Tex->Texture = Texture; Tex->SamplerType = SAMPLERTYPE_Color;
        auto* RGBA = Expression<UMaterialExpressionAppendVector>(M, -750, -500);
        RGBA->A.Connect(0, Tex); RGBA->B.Connect(4, Tex);
        auto* UV = Expression<UMaterialExpressionTextureCoordinate>(M, -1000, -250);
        auto* Code = Expression<UMaterialExpressionCustom>(M, -250, -150);
        Code->Desc = TEXT("Reditus crimson sprite death v1"); Code->OutputType = CMOT_Float4; Code->Inputs.Reset();
        Input(Code, TEXT("Tex"), RGBA); Input(Code, TEXT("TextureUV"), UV);
        auto Scalar = [&](const TCHAR* Name, float Value)
        {
            auto* E = Expression<UMaterialExpressionScalarParameter>(M, -1000, 40 + Code->Inputs.Num()*75);
            E->ParameterName = Name; E->DefaultValue = Value; Input(Code, Name, E);
        };
        auto Vector = [&](const TCHAR* Name, FLinearColor Value)
        {
            auto* E = Expression<UMaterialExpressionVectorParameter>(M, -1000, 40 + Code->Inputs.Num()*75);
            E->ParameterName = Name; E->DefaultValue = Value;
            auto* Four = Expression<UMaterialExpressionAppendVector>(M, -720, E->MaterialExpressionEditorY);
            Four->A.Connect(0, E); Four->B.Connect(4, E); Input(Code, Name, Four);
        };
        Vector(TEXT("UVBounds"), FLinearColor(0, 0, 1, 1));
        Vector(TEXT("CrimsonTint"), FLinearColor(.48f, .006f, .018f, 1));
        Vector(TEXT("SourceTint"), FLinearColor::White);
        Scalar(TEXT("Columns"), 4); Scalar(TEXT("Rows"), 5); Scalar(TEXT("Seed"), 1);
        Scalar(TEXT("TileIndex"), -1); Scalar(TEXT("Progress"), 0); Scalar(TEXT("Opacity"), 1); Scalar(TEXT("Flash"), 0);
        ConfigureDirectionalShader(M, Code);
        auto* RGB = Expression<UMaterialExpressionComponentMask>(M, 150, -150);
        RGB->Input.Connect(0, Code); RGB->R = RGB->G = RGB->B = true; RGB->A = false;
        auto* Alpha = Expression<UMaterialExpressionComponentMask>(M, 150, 100);
        Alpha->Input.Connect(0, Code); Alpha->R = Alpha->G = Alpha->B = false; Alpha->A = true;
        M->EmissiveColor.Connect(0, RGB); M->Opacity.Connect(0, Alpha);
        Package->GetMetaData()->SetValue(M, TEXT("ReditusEnemyDeathVersion"), TEXT("2"));
        M->PostEditChange(); M->MarkPackageDirty(); FAssetRegistryModule::AssetCreated(M);
        return M;
    }

    UEdGraphPin* Pin(UEdGraphNode* Node, FName Name)
    { auto* P = Node ? Node->FindPin(Name) : nullptr; return P && !P->bOrphanedPin ? P : nullptr; }
    bool Link(UEdGraph* Graph, UEdGraphPin* A, UEdGraphPin* B)
    { return A && B && Graph->GetSchema()->TryCreateConnection(A, B); }
    bool Compile(UBlueprint* BP)
    {
        FCompilerResultsLog Log;
        FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::SkipGarbageCollection, &Log);
        return Log.NumErrors == 0 && BP->Status != BS_Error;
    }
    struct FDeathSite
    {
        UEdGraph* Graph = nullptr; UEdGraphPin* Event = nullptr; UEdGraphPin* Parent = nullptr;
        UK2Node_CallFunction* Installed = nullptr;
    };
    bool FindSite(UBlueprint* BP, FDeathSite& S)
    {
        UK2Node_Event* Event = nullptr; UK2Node_CallParentFunction* Parent = nullptr;
        for (auto* Graph : BP->UbergraphPages)
            for (auto* Node : Graph->Nodes)
            {
                if (Node->NodeGuid == FGuid(0x986AAC7C, 0x46A69EB9, 0xAD1ACA83, 0x6A2CD22E)) Event = Cast<UK2Node_Event>(Node);
                if (Node->NodeGuid == FGuid(0xB33973B0, 0x41902C16, 0x206A3DBB, 0x55E35DEB)) Parent = Cast<UK2Node_CallParentFunction>(Node);
            }
        if (!Event || !Parent || Event->GetGraph() != Parent->GetGraph()) return false;
        S.Graph = Event->GetGraph(); S.Event = Pin(Event, UEdGraphSchema_K2::PN_Then); S.Parent = Pin(Parent, UEdGraphSchema_K2::PN_Execute);
        if (!S.Event || !S.Parent || S.Event->LinkedTo.Num() != 1 || S.Parent->LinkedTo.Num() != 1) return false;
        if (S.Event->LinkedTo[0] == S.Parent) return true;
        auto* Call = Cast<UK2Node_CallFunction>(S.Event->LinkedTo[0]->GetOwningNode());
        if (!Call || Call->FunctionReference.GetMemberName() != GET_FUNCTION_NAME_CHECKED(UReditusEnemyDeathLibrary, TryStartEnemyDeathVisual)) return false;
        auto* In = Pin(Call, UEdGraphSchema_K2::PN_Execute); auto* Out = Pin(Call, UEdGraphSchema_K2::PN_Then);
        if (!In || !Out || S.Event->LinkedTo[0] != In || In->LinkedTo.Num()!=1 || Out->LinkedTo.Num()!=1 || Out->LinkedTo[0]!=S.Parent) return false;
        S.Installed = Call; return true;
    }

    const FName DeathComponentName(TEXT("EnemyDeathVisual"));

    bool FindComponentTemplate(UBlueprint* BP, UReditusEnemyDeathComponent*& Out)
    {
        Out = nullptr;
        if (!BP || !BP->SimpleConstructionScript) return false;
        for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
        {
            if (!Node) continue;
            auto* Component = Cast<UReditusEnemyDeathComponent>(Node->ComponentTemplate);
            if (Component)
            {
                if (Out) return false; // Multiple death components are ambiguous.
                Out = Component;
            }
            else if (Node->GetVariableName() == DeathComponentName) return false;
        }
        // Do not shadow an inherited/non-component member with the generated name.
        return Out || !BP->GeneratedClass || !FindFProperty<FProperty>(BP->GeneratedClass, DeathComponentName);
    }

    bool EnsureComponent(UBlueprint* BP, UDataTable* Table, UMaterial* Material)
    {
        UReditusEnemyDeathComponent* Component = nullptr;
        if (!FindComponentTemplate(BP, Component)) return false;
        if (!Component)
        {
            BP->SimpleConstructionScript->Modify();
            auto* Node = BP->SimpleConstructionScript->CreateNode(UReditusEnemyDeathComponent::StaticClass(), DeathComponentName);
            Component = Node ? Cast<UReditusEnemyDeathComponent>(Node->ComponentTemplate) : nullptr;
            if (!Component) return false;
            BP->SimpleConstructionScript->AddNode(Node);
        }
        Component->Modify();
        // Preserve class-authored style, timing, source property and resource overrides.
        if (!Component->EnemyTable) Component->EnemyTable = Table;
        if (!Component->DissolveMaterial) Component->DissolveMaterial = Material;
        return true;
    }

    bool WireHook(UBlueprint* BP, const FDeathSite& Site, UDataTable* Table, UMaterial* Material)
    {
        BP->Modify();
        UK2Node_CallFunction* Call = Site.Installed;
        if (!Call)
        {
            Call = NewObject<UK2Node_CallFunction>(Site.Graph, NAME_None, RF_Transactional);
            Site.Graph->AddNode(Call, false, false); Call->CreateNewGuid();
            Call->SetFromFunction(UReditusEnemyDeathLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReditusEnemyDeathLibrary, TryStartEnemyDeathVisual)));
            Call->AllocateDefaultPins(); Call->NodePosX = Site.Event->GetOwningNode()->NodePosX + 260; Call->NodePosY = Site.Event->GetOwningNode()->NodePosY;
            auto* Self = NewObject<UK2Node_Self>(Site.Graph, NAME_None, RF_Transactional);
            Site.Graph->AddNode(Self, false, false); Self->CreateNewGuid(); Self->AllocateDefaultPins(); Self->NodePosX = Call->NodePosX-160; Self->NodePosY = Call->NodePosY+160;
            Site.Event->GetOwningNode()->Modify(); Site.Parent->GetOwningNode()->Modify();
            if (!Link(Site.Graph, Pin(Self, UEdGraphSchema_K2::PN_Self), Pin(Call, TEXT("Enemy")))) return false;
            Site.Event->BreakLinkTo(Site.Parent);
            if (!Link(Site.Graph, Site.Event, Pin(Call, UEdGraphSchema_K2::PN_Execute)) ||
                !Link(Site.Graph, Pin(Call, UEdGraphSchema_K2::PN_Then), Site.Parent)) return false;
        }
        Call->Modify();
        auto* TablePin = Pin(Call, TEXT("EnemyTable")); auto* MaterialPin = Pin(Call, TEXT("DissolveMaterial"));
        if (TablePin && MaterialPin && TablePin->LinkedTo.Num()==0 && MaterialPin->LinkedTo.Num()==0)
        {
            Site.Graph->GetSchema()->TrySetDefaultObject(*TablePin, Table);
            Site.Graph->GetSchema()->TrySetDefaultObject(*MaterialPin, Material);
            return true;
        }
        return false;
    }

    void Install()
    {
        if (!GEditor || GEditor->PlayWorld)
        { UE_LOG(LogReditusEnemyDeathInstall, Error, TEXT("Stop Play before Reditus.InstallEnemyDeathVisuals.")); return; }
        auto* Table = LoadObject<UDataTable>(nullptr, TablePath, nullptr, LOAD_NoWarn | LOAD_Quiet);
        const auto* Field = Table && Table->GetRowStruct() ? FindFProperty<FStructProperty>(Table->GetRowStruct(), TEXT("BossDeath")) : nullptr;
        if (!Field || Field->Struct != FReditusEnemyBossDeathOptions::StaticStruct())
            UE_LOG(LogReditusEnemyDeathInstall, Display, TEXT("No compatible BossDeath row field: shared defaults and class Boss Death options work. Run the source setup script to add table boss options."));
        auto* BP = LoadObject<UBlueprint>(nullptr, EnemyPath);
        if (!BP || !Compile(BP))
        { UE_LOG(LogReditusEnemyDeathInstall, Error, TEXT("BP_EnemyUnitBase must compile before installation. No graph changes made.")); return; }
        FDeathSite Site;
        if (!FindSite(BP, Site))
        { UE_LOG(LogReditusEnemyDeathInstall, Error, TEXT("Unrecognized Event Die -> Parent:Die wiring. No graph changes made. Do not replace your graph; provide that small death section for review.")); return; }
        UReditusEnemyDeathComponent* Component = nullptr;
        if (!FindComponentTemplate(BP, Component))
        { UE_LOG(LogReditusEnemyDeathInstall, Error, TEXT("Conflicting EnemyDeathVisual member or multiple death components. No graph changes made.")); return; }
        auto* Material = MakeMaterial();
        if (!Material)
        { UE_LOG(LogReditusEnemyDeathInstall, Error, TEXT("Could not create/reuse M_CrimsonDeath. Death graph is unchanged.")); return; }
        if (Site.Installed)
        {
            auto* T = Pin(Site.Installed, TEXT("EnemyTable")); auto* M = Pin(Site.Installed, TEXT("DissolveMaterial"));
            auto* E = Pin(Site.Installed, TEXT("Enemy"));
            const bool bSelf = E && E->LinkedTo.Num()==1 && E->LinkedTo[0]->GetOwningNode()->IsA<UK2Node_Self>();
            if (!bSelf || !T || !M || T->LinkedTo.Num()!=0 || M->LinkedTo.Num()!=0 || T->DefaultObject!=Table || M->DefaultObject!=Material)
            { UE_LOG(LogReditusEnemyDeathInstall, Error, TEXT("Installed death hook has custom inputs; no graph changes made.")); return; }
        }
        bool bSucceeded = false;
        {
            const FScopedTransaction Tx(NSLOCTEXT("ReditusEnemyDeath", "Install", "Install reusable enemy death visual"));
            if (!Tx.IsOutstanding()) return;
            BP->Modify(); Site.Graph->Modify();
            if (WireHook(BP, Site, Table, Material) && EnsureComponent(BP, Table, Material))
            {
                FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
                bSucceeded = Compile(BP);
            }
        }
        if (!bSucceeded)
        {
            const bool Undone = GEditor->UndoTransaction(false);
            if (Undone) Compile(BP);
            UE_LOG(LogReditusEnemyDeathInstall, Error, TEXT("Death hook could not compile. Undo: %s. Do not save on failure. Generated material may remain unsaved."), Undone ? TEXT("OK") : TEXT("FAILED"));
            return;
        }
        UE_LOG(LogReditusEnemyDeathInstall, Display, TEXT("Enemy Death component/hook installed. Save BP_EnemyUnitBase and M_CrimsonDeath. Set shared Settings on the base component; use Boss Death for class enemies or DT_Enemies boss rows. Existing working On Unit Die hookups do not need this installer."));
    }
    void UpdateMaterial()
    {
        if (!GEditor || GEditor->PlayWorld)
        { UE_LOG(LogReditusEnemyDeathInstall, Error, TEXT("Stop Play before Reditus.UpdateEnemyDeathMaterial.")); return; }
        const FScopedTransaction Tx(NSLOCTEXT("ReditusEnemyDeath", "UpdateMaterial", "Update front-to-back enemy death material"));
        if (!Tx.IsOutstanding()) return;
        auto* Material = MakeMaterial();
        if (!Material)
        { UE_LOG(LogReditusEnemyDeathInstall, Error, TEXT("Enemy death material update failed. Keep the existing working death hookup.")); return; }
        UE_LOG(LogReditusEnemyDeathInstall, Display, TEXT("Front-to-back death material is ready. Save M_CrimsonDeath, wait for shader compilation and test. Existing On Unit Die / death event connections were not changed."));
    }
    FAutoConsoleCommand MaterialCommand(TEXT("Reditus.UpdateEnemyDeathMaterial"),
        TEXT("Upgrade only the generated crimson death material. Preserves Blueprint death events and component/table settings. Stop Play first."),
        FConsoleCommandDelegate::CreateStatic(&UpdateMaterial));
    FAutoConsoleCommand Command(TEXT("Reditus.InstallEnemyDeathVisuals"),
        TEXT("Create crimson material, inherited Enemy Death component and non-blocking death hook. Stop Play first. Native table field is optional for class enemies."),
        FConsoleCommandDelegate::CreateStatic(&Install));
}
IMPLEMENT_MODULE(FDefaultModuleImpl, ReditusEnemyDeathEditor)
