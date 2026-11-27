#include "ReditusTechMasteryLibrary.h"
#include "ReditusTechMasteryRules.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogReditusTechMastery, Log, All);

namespace ReditusTechMasteryRuntime
{
    const TCHAR* SkillTablePath = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_Skills.DT_Skills");
    const TCHAR* EnemyTablePath = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_Enemies.DT_Enemies");
    const TCHAR* PartyTablePath = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_PartyMembers.DT_PartyMembers");
    const TCHAR* FunctionLibraryPath = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Controllers/BP_JRPGFunctionLibrary.BP_JRPGFunctionLibrary_C");

    template <typename Field>
    const Field* FindField(const UStruct* Type, FName Name)
    {
        if (!Type) return nullptr;
        if (const auto* Exact = FindFProperty<Field>(Type, Name)) return Exact;
        const FString Prefix = Name.ToString() + TEXT("_");
        for (TFieldIterator<Field> It(Type); It; ++It)
        {
            if (It->GetName().StartsWith(Prefix)) return *It;
        }
        return nullptr;
    }

    UObject* ObjectField(const UObject* Owner, FName Name)
    {
        const auto* Field = IsValid(Owner) ? FindFProperty<FObjectPropertyBase>(Owner->GetClass(), Name) : nullptr;
        return Field ? Field->GetObjectPropertyValue_InContainer(Owner) : nullptr;
    }

    FName NameField(const UObject* Owner, FName Name)
    {
        const auto* Field = IsValid(Owner) ? FindFProperty<FNameProperty>(Owner->GetClass(), Name) : nullptr;
        return Field ? Field->GetPropertyValue_InContainer(Owner) : NAME_None;
    }

    struct FRow
    {
        const UScriptStruct* Type = nullptr;
        const uint8* Data = nullptr;
    };

    FRow FindRow(const TCHAR* TablePath, FName ID)
    {
        if (ID.IsNone()) return {};
        auto* Table = LoadObject<UDataTable>(nullptr, TablePath, nullptr, LOAD_NoWarn | LOAD_Quiet);
        return IsValid(Table) ? FRow{Table->GetRowStruct(), Table->GetRowMap().FindRef(ID)} : FRow{};
    }

    FText DisplayName(const FRow& Row, FName ID)
    {
        const auto* Field = FindField<FTextProperty>(Row.Type, TEXT("DisplayName"));
        if (!Row.Data || !Field) return FText::FromString(ID.ToString());
        const FText Value = Field->GetPropertyValue_InContainer(Row.Data);
        return Value.IsEmpty() ? FText::FromString(ID.ToString()) : Value;
    }

    int32 Requirement(const FRow& Row)
    {
        const auto* Field = FindField<FIntProperty>(Row.Type, TEXT("MasteryPointsRequired"));
        return Row.Data && Field ? Field->GetPropertyValue_InContainer(Row.Data) : 0;
    }

    UGameInstance* Instance(const UObject* Context)
    {
        UWorld* World = IsValid(Context) ? Context->GetWorld() : nullptr;
        return World ? World->GetGameInstance() : nullptr;
    }

    UReditusTechMasterySubsystem* Subsystem(const UObject* Context)
    {
        UGameInstance* Game = Instance(Context);
        return Game ? Game->GetSubsystem<UReditusTechMasterySubsystem>() : nullptr;
    }

    FReditusTechMasterySaveData* SaveData(const UObject* Context, bool bCreateSave = false)
    {
        UGameInstance* Game = Instance(Context);
        if (!Game) return nullptr;
        const auto* Slot = FindFProperty<FObjectProperty>(Game->GetClass(), TEXT("jRPGSaveGame"));
        if (!Slot || !Slot->PropertyClass->IsChildOf(USaveGame::StaticClass())) return nullptr;
        UObject* Save = Slot->GetObjectPropertyValue_InContainer(Game);
        if (!IsValid(Save) && bCreateSave)
        {
            Save = UGameplayStatics::CreateSaveGameObject(Slot->PropertyClass);
            Slot->SetObjectPropertyValue_InContainer(Game, Save);
        }
        const auto* Field = IsValid(Save) ? FindFProperty<FStructProperty>(Save->GetClass(), TEXT("TechMastery")) : nullptr;
        return Field && Field->Struct == FReditusTechMasterySaveData::StaticStruct()
            ? Field->ContainerPtrToValuePtr<FReditusTechMasterySaveData>(Save) : nullptr;
    }

    const FReditusCharacterMastery* CharacterProgress(const UObject* Context, FName CharacterID)
    {
        const auto* Save = SaveData(Context);
        return Save && !CharacterID.IsNone() ? Save->Characters.Find(CharacterID) : nullptr;
    }

    void ReadNameArray(const FArrayProperty* Array, const void* Owner, TArray<FName>& Out)
    {
        const auto* Element = Array ? CastField<FNameProperty>(Array->Inner) : nullptr;
        if (!Element || !Owner) return;
        FScriptArrayHelper Values(Array, Array->ContainerPtrToValuePtr<void>(Owner));
        for (int32 Index = 0; Index < Values.Num(); ++Index)
        {
            FName Name = Element->GetPropertyValue(Values.GetRawPtr(Index));
            if (!Name.IsNone()) Out.AddUnique(Name);
        }
    }

    const void* FindUnitData(const UObject* Controller, FName CharacterID, const UScriptStruct*& OutType)
    {
        if (!IsValid(Controller) || CharacterID.IsNone()) return nullptr;
        const auto* Map = FindFProperty<FMapProperty>(Controller->GetClass(), TEXT("PlayerUnitDataID"));
        const auto* Key = Map ? CastField<FNameProperty>(Map->KeyProp) : nullptr;
        const auto* Value = Map ? CastField<FStructProperty>(Map->ValueProp) : nullptr;
        if (!Key || !Value) return nullptr;
        FScriptMapHelper Units(Map, Map->ContainerPtrToValuePtr<void>(Controller));
        for (int32 Index = 0; Index < Units.GetMaxIndex(); ++Index)
        {
            if (Units.IsValidIndex(Index) && Key->GetPropertyValue(Units.GetKeyPtr(Index)) == CharacterID)
            {
                OutType = Value->Struct;
                return Units.GetValuePtr(Index);
            }
        }
        return nullptr;
    }

    void ReadEquippedTechs(UObject* Context, const void* Unit, const UScriptStruct* UnitType, TArray<FName>& Out)
    {
        if (!Unit || !UnitType) return;
        auto* Class = LoadClass<UObject>(nullptr, FunctionLibraryPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
        auto* GetSkills = Class ? Class->FindFunctionByName(TEXT("GetEquipmentSkills")) : nullptr;
        if (!GetSkills) return;
        const auto* Input = FindFProperty<FStructProperty>(GetSkills, TEXT("unitData"));
        const auto* Output = FindFProperty<FArrayProperty>(GetSkills, TEXT("equipmentBattleSkills"));
        if (!Input || Input->Struct != UnitType || !Output || !CastField<FNameProperty>(Output->Inner)) return;
        FStructOnScope Args(GetSkills);
        Input->CopyCompleteValue(Input->ContainerPtrToValuePtr<void>(Args.GetStructMemory()), Unit);
        if (const auto* World = FindFProperty<FObjectPropertyBase>(GetSkills, TEXT("__WorldContext")))
        {
            World->SetObjectPropertyValue_InContainer(Args.GetStructMemory(), Context);
        }
        Class->GetDefaultObject()->ProcessEvent(GetSkills, Args.GetStructMemory());
        ReadNameArray(Output, Args.GetStructMemory(), Out);
    }

    bool ReadBossFlag(const UStruct* Type, const void* Value, bool& bIsBoss)
    {
        const auto* Options = Value && Type ? FindFProperty<FStructProperty>(Type, TEXT("BossDeath")) : nullptr;
        const auto* Flag = Options ? FindFProperty<FBoolProperty>(Options->Struct, TEXT("bIsBoss")) : nullptr;
        if (!Flag) return false;
        bIsBoss = Flag->GetPropertyValue_InContainer(Options->ContainerPtrToValuePtr<void>(Value));
        return true;
    }

    bool IsBoss(AActor* Enemy, const FRow& Row)
    {
        bool bIsBoss = false;
        if (ReadBossFlag(Row.Type, Row.Data, bIsBoss)) return bIsBoss;
        UObject* Component = ObjectField(Enemy, TEXT("EnemyDeathVisual"));
        if (IsValid(Component) && ReadBossFlag(Component->GetClass(), Component, bIsBoss)) return bIsBoss;
        UFunction* GetStyle = IsValid(Component) ? Component->FindFunction(TEXT("GetDeathStyle")) : nullptr;
        if (!GetStyle || GetStyle->NumParms != 1) return false;
        const auto* Return = GetStyle->GetReturnProperty();
        const auto* Enum = CastField<FEnumProperty>(Return);
        const auto* Byte = CastField<FByteProperty>(Return);
        if (!Enum && !(Byte && Byte->Enum)) return false;

        FStructOnScope Args(GetStyle);
        Component->ProcessEvent(GetStyle, Args.GetStructMemory());
        const int64 Value = Enum
            ? Enum->GetUnderlyingProperty()->GetSignedIntPropertyValue(Enum->ContainerPtrToValuePtr<void>(Args.GetStructMemory()))
            : Byte->GetPropertyValue_InContainer(Args.GetStructMemory());
        const UEnum* Style = Enum ? Enum->GetEnum() : Byte->Enum;
        const FString Name = Style->GetNameStringByValue(Value);
        return Name == TEXT("BossDissolve") || Name == TEXT("BossCameraShake");
    }

    int32 ResolveMasteryReward(AActor* Enemy, bool bTableEnemy)
    {
        const FRow Row = bTableEnemy ? FindRow(EnemyTablePath, NameField(Enemy, TEXT("EnemyID"))) : FRow{};
        int32 Override = 0;
        if (Row.Data)
        {
            if (const auto* Field = FindField<FIntProperty>(Row.Type, TEXT("MasteryRewardOverride")))
            {
                Override = Field->GetPropertyValue_InContainer(Row.Data);
            }
        }
        else if (const auto* Field = FindFProperty<FIntProperty>(Enemy->GetClass(), TEXT("MasteryRewardOverride")))
        {
            Override = Field->GetPropertyValue_InContainer(Enemy);
        }
        return ReditusTechMasteryRules::ResolveEnemyReward(Override, Override <= 0 && IsBoss(Enemy, Row));
    }

    void SetText(UObject* TextWidget, const FText& Text)
    {
        UFunction* Setter = IsValid(TextWidget) ? TextWidget->FindFunction(TEXT("SetText")) : nullptr;
        const auto* Input = Setter ? FindFProperty<FTextProperty>(Setter, TEXT("InText")) : nullptr;
        if (!Input || Setter->NumParms != 1) return;
        FStructOnScope Args(Setter);
        Input->SetPropertyValue_InContainer(Args.GetStructMemory(), Text);
        TextWidget->ProcessEvent(Setter, Args.GetStructMemory());
    }

    FText StageText(const FReditusCharacterMastery* Mastery, FName SkillID, int32 Required)
    {
        if (Mastery && Mastery->Learned.Contains(SkillID)) return NSLOCTEXT("ReditusMastery", "Learned", "Learned");
        const int32 Progress = Mastery ? Mastery->Progress.FindRef(SkillID) : 0;
        return FText::Format(NSLOCTEXT("ReditusMastery", "Progress", "Mastery {0}/{1}"),
            FText::AsNumber(FMath::Clamp(Progress, 0, Required)), FText::AsNumber(Required));
    }
}

void UReditusTechMasteryLibrary::BeginMasteryEncounter(AActor* Battle)
{
    using namespace ReditusTechMasteryRuntime;
    if (auto* System = Subsystem(Battle))
    {
        for (auto It = System->Encounters.CreateIterator(); It; ++It)
        {
            if (!It.Key().IsValid()) It.RemoveCurrent();
        }
        System->Encounters.Add(Battle, FReditusEncounterMastery());
    }
}

void UReditusTechMasteryLibrary::IncludeEnemyMasteryReward(AActor* Battle, AActor* Enemy, bool bTableEnemy)
{
    using namespace ReditusTechMasteryRuntime;
    auto* System = Subsystem(Battle);
    if (!System || !IsValid(Enemy)) return;
    auto& Encounter = System->Encounters.FindOrAdd(Battle);
    if (!Encounter.bAwarded)
    {
        Encounter.Reward = ReditusTechMasteryRules::IncludeEnemyReward(Encounter.Reward, ResolveMasteryReward(Enemy, bTableEnemy));
    }
}

void UReditusTechMasteryLibrary::AwardVictoryMastery(AActor* BattleController)
{
    using namespace ReditusTechMasteryRuntime;
    auto* Battle = Cast<AActor>(ObjectField(BattleController, TEXT("currentBattle")));
    auto* System = Subsystem(Battle);
    if (!System) return;
    auto& Encounter = System->Encounters.FindOrAdd(Battle);
    if (Encounter.bAwarded) return;

    auto* Controller = ObjectField(BattleController, TEXT("jRPGPlayerController"));
    auto* Save = SaveData(Battle, true);
    auto* Units = FindFProperty<FArrayProperty>(Battle->GetClass(), TEXT("playerUnits"));
    auto* UnitElement = Units ? CastField<FObjectPropertyBase>(Units->Inner) : nullptr;
    if (!Save || !IsValid(Controller) || !UnitElement)
    {
        UE_LOG(LogReditusTechMastery, Warning, TEXT("Mastery could not read the battle participants or active save data."));
        return;
    }

    FScriptArrayHelper Participants(Units, Units->ContainerPtrToValuePtr<void>(Battle));
    TSet<FName> Counted;
    for (int32 Index = 0; Index < Participants.Num(); ++Index)
    {
        UObject* Unit = UnitElement->GetObjectPropertyValue(Participants.GetRawPtr(Index));
        const FName CharacterID = NameField(Unit, TEXT("CharacterID"));
        if (CharacterID.IsNone() || Counted.Contains(CharacterID)) continue;
        Counted.Add(CharacterID);

        const UScriptStruct* UnitType = nullptr;
        const void* UnitData = FindUnitData(Controller, CharacterID, UnitType);
        TArray<FName> Skills;
        ReadEquippedTechs(BattleController, UnitData, UnitType, Skills);
        for (FName SkillID : Skills)
        {
            const FRow Row = FindRow(SkillTablePath, SkillID);
            const int32 Required = Requirement(Row);
            if (Required <= 0) continue;
            auto& Character = Save->Characters.FindOrAdd(CharacterID);
            if (Character.Learned.Contains(SkillID)) continue;

            int32& Progress = Character.Progress.FindOrAdd(SkillID);
            Progress = ReditusTechMasteryRules::Advance(Progress, Encounter.Reward, Required);
            Encounter.bAnyProgress = true;
            if (Progress >= Required)
            {
                Character.Learned.Add(SkillID);
                Encounter.Learned.Add(FText::Format(NSLOCTEXT("ReditusMastery", "MasteredTech", "{0} learned {1}"),
                    DisplayName(FindRow(PartyTablePath, CharacterID), CharacterID), DisplayName(Row, SkillID)));
            }
        }
    }
    Encounter.bAwarded = true;
}

void UReditusTechMasteryLibrary::AppendLearnedTechs(UObject* SkillOwner)
{
    using namespace ReditusTechMasteryRuntime;
    FName CharacterID = NameField(SkillOwner, TEXT("CharacterID"));
    if (CharacterID.IsNone()) CharacterID = NameField(SkillOwner, TEXT("CastingUnit"));
    const auto* Mastery = CharacterProgress(SkillOwner, CharacterID);
    if (!Mastery || Mastery->Learned.Num() == 0) return;

    const auto* Skills = FindFProperty<FArrayProperty>(SkillOwner->GetClass(), TEXT("availableBattleSkills"));
    const auto* Element = Skills ? CastField<FNameProperty>(Skills->Inner) : nullptr;
    if (!Element) return;
    TArray<FName> Existing;
    ReadNameArray(Skills, SkillOwner, Existing);
    FScriptArrayHelper Target(Skills, Skills->ContainerPtrToValuePtr<void>(SkillOwner));
    for (FName Skill : Mastery->Learned)
    {
        if (!Skill.IsNone() && !Existing.Contains(Skill))
        {
            Element->SetPropertyValue(Target.GetRawPtr(Target.AddValue()), Skill);
            Existing.Add(Skill);
        }
    }
}

FText UReditusTechMasteryLibrary::FormatVictoryMastery(UObject* VictoryWidget, const FText& RewardText)
{
    using namespace ReditusTechMasteryRuntime;
    auto* System = Subsystem(VictoryWidget);
    if (!System) return RewardText;
    UObject* Unit = ObjectField(VictoryWidget, TEXT("PlayerUnitBase"));
    UObject* Controller = ObjectField(Unit, TEXT("battleController"));
    if (!IsValid(Controller))
    {
        Controller = ObjectField(UGameplayStatics::GetPlayerController(VictoryWidget, 0), TEXT("battleController"));
    }
    auto* Battle = Cast<AActor>(ObjectField(Controller, TEXT("currentBattle")));
    const auto* Encounter = System->Encounters.Find(Battle);
    if (!Encounter || !Encounter->bAnyProgress) return RewardText;

    FText Text = FText::Format(NSLOCTEXT("ReditusMastery", "VictoryReward", "{0}   Mastery +{1}"),
        RewardText, FText::AsNumber(Encounter->Reward));
    for (const FText& Learned : Encounter->Learned)
    {
        Text = FText::Format(NSLOCTEXT("ReditusMastery", "Line", "{0}\n{1}"), Text, Learned);
    }
    return Text;
}

void UReditusTechMasteryLibrary::ShowMasteryOnSkillButton(UObject* Menu, UObject* SkillButton, FName SkillID)
{
    using namespace ReditusTechMasteryRuntime;
    const FRow Row = FindRow(SkillTablePath, SkillID);
    const int32 Needed = Requirement(Row);
    if (Needed <= 0) return;
    FName CharacterID = NameField(Menu, TEXT("CastingUnit"));
    const bool bEquipmentPreview = CharacterID.IsNone();
    if (bEquipmentPreview) CharacterID = NameField(Menu, TEXT("CharacterID"));
    if (CharacterID.IsNone()) return;

    const auto* Mastery = CharacterProgress(Menu, CharacterID);
    if (!bEquipmentPreview && !(Mastery && Mastery->Learned.Contains(SkillID)))
    {
        const auto* Unit = FindFProperty<FStructProperty>(Menu->GetClass(), TEXT("playerUnitData"));
        TArray<FName> Skills;
        if (Unit) ReadEquippedTechs(Menu, Unit->ContainerPtrToValuePtr<void>(Menu), Unit->Struct, Skills);
        if (!Skills.Contains(SkillID)) return;
    }
    SetText(ObjectField(SkillButton, TEXT("SkillName")),
        FText::Format(NSLOCTEXT("ReditusMastery", "TechName", "{0}   {1}"),
            DisplayName(Row, SkillID), StageText(Mastery, SkillID, Needed)));
}
