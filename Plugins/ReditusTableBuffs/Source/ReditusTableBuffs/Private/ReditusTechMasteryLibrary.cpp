#include "ReditusTechMasteryLibrary.h"
#include "ReditusTechMasteryRules.h"
#include "ReditusVictoryPresentation.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ActorComponent.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
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
    const TCHAR* EquipmentTablePath = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_Equipment.DT_Equipment");
    const TCHAR* GameFunctionsPath = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Controllers/BP_JRPGFunctionLibrary.BP_JRPGFunctionLibrary_C");

    template <typename Field>
    const Field* FindMasteryProperty(const UStruct* Type, FName Name)
    {
        if (!Type) return nullptr;
        if (const auto* Exact = FindFProperty<Field>(Type, Name)) return Exact;
        const FString AuthoredName = Name.ToString();
        const FString Prefix = AuthoredName + TEXT("_");
        for (TFieldIterator<Field> It(Type); It; ++It)
        {
            if (It->GetName().StartsWith(Prefix) ||
                Type->GetAuthoredNameForField(*It).Equals(AuthoredName, ESearchCase::IgnoreCase)) return *It;
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

    int32 IntField(const UObject* Owner, FName Name)
    {
        const auto* Field = IsValid(Owner) ? FindFProperty<FIntProperty>(Owner->GetClass(), Name) : nullptr;
        return Field ? Field->GetPropertyValue_InContainer(Owner) : 0;
    }

    bool BoolField(const UObject* Owner, FName Name)
    {
        const auto* Field = IsValid(Owner) ? FindFProperty<FBoolProperty>(Owner->GetClass(), Name) : nullptr;
        return Field && Field->GetPropertyValue_InContainer(Owner);
    }

    TArray<AActor*> ActorArray(UObject* Owner, FName Name)
    {
        TArray<AActor*> Actors;
        const auto* Array = IsValid(Owner) ? FindFProperty<FArrayProperty>(Owner->GetClass(), Name) : nullptr;
        const auto* Element = Array ? CastField<FObjectPropertyBase>(Array->Inner) : nullptr;
        if (!Element) return Actors;

        FScriptArrayHelper Values(Array, Array->ContainerPtrToValuePtr<void>(Owner));
        for (int32 Index = 0; Index < Values.Num(); ++Index)
        {
            auto* Actor = Cast<AActor>(Element->GetObjectPropertyValue(Values.GetRawPtr(Index)));
            if (IsValid(Actor)) Actors.Add(Actor);
        }
        return Actors;
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
        const auto* Field = FindMasteryProperty<FTextProperty>(Row.Type, TEXT("DisplayName"));
        if (!Row.Data || !Field) return FText::FromString(ID.ToString());
        const FText Value = Field->GetPropertyValue_InContainer(Row.Data);
        return Value.IsEmpty() ? FText::FromString(ID.ToString()) : Value;
    }

    int32 Requirement(const FRow& Row)
    {
        const auto* Field = FindMasteryProperty<FIntProperty>(Row.Type, TEXT("MasteryPointsRequired"));
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

    APlayerController* LocalPlayerController(const UObject* Context)
    {
        return Context ? UGameplayStatics::GetPlayerController(Context, 0) : nullptr;
    }

    AActor* BattleControllerFor(const UObject* Context)
    {
        if (auto* Controller = Cast<AActor>(ObjectField(Context, TEXT("battleController")))) return Controller;
        if (UObject* Unit = ObjectField(Context, TEXT("PlayerUnitBase")))
        {
            if (auto* Controller = Cast<AActor>(ObjectField(Unit, TEXT("battleController")))) return Controller;
        }
        return Cast<AActor>(ObjectField(LocalPlayerController(Context), TEXT("battleController")));
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

    TArray<FName> NamedMoves(const UObject* Owner, FName Name)
    {
        TArray<FName> Moves;
        const auto* Array = IsValid(Owner) ? FindFProperty<FArrayProperty>(Owner->GetClass(), Name) : nullptr;
        ReadNameArray(Array, Owner, Moves);
        return Moves;
    }

    void AddMoves(const FArrayProperty* Array, void* Owner, const TArray<FName>& Moves, bool bReplace = false)
    {
        const auto* Element = Array ? CastField<FNameProperty>(Array->Inner) : nullptr;
        if (!Element || !Owner) return;
        FScriptArrayHelper Target(Array, Array->ContainerPtrToValuePtr<void>(Owner));
        TArray<FName> Existing;
        if (bReplace) Target.EmptyValues();
        else ReadNameArray(Array, Owner, Existing);
        for (FName Move : Moves)
        {
            if (!Move.IsNone() && !Existing.Contains(Move))
            {
                Element->SetPropertyValue(Target.GetRawPtr(Target.AddValue()), Move);
                Existing.Add(Move);
            }
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

    void ReadEquipmentTechs(FName EquipmentID, TArray<FName>& Out, bool bOverdrive = false)
    {
        const FRow Equipment = FindRow(EquipmentTablePath, EquipmentID);
        const FName Field(bOverdrive ? TEXT("OverDriveSkillIDs") : TEXT("BattleSkillIDs"));
        ReadNameArray(FindMasteryProperty<FArrayProperty>(Equipment.Type, Field), Equipment.Data, Out);
    }

    void ReadEquippedTechs(const void* Unit, const UScriptStruct* UnitType, TArray<FName>& Out, bool bOverdrive = false)
    {
        if (!Unit || !UnitType) return;
        static const FName Slots[] = {
            TEXT("Weapon"), TEXT("Armor"), TEXT("Helm"), TEXT("Boots"), TEXT("Accessory"), TEXT("Accessory1")
        };
        for (FName Slot : Slots)
        {
            if (const auto* ItemID = FindMasteryProperty<FNameProperty>(UnitType, Slot))
            {
                ReadEquipmentTechs(ItemID->GetPropertyValue_InContainer(Unit), Out, bOverdrive);
            }
        }
    }

    void ReadCharacterEquipment(UObject* Context, UObject* Controller, FName CharacterID, TArray<FName>& Out,
        bool bOverdrive = false)
    {
        const UScriptStruct* UnitType = nullptr;
        const void* Data = FindUnitData(Controller, CharacterID, UnitType);
        if (!Data) Data = FindUnitData(Instance(Context), CharacterID, UnitType);
        ReadEquippedTechs(Data, UnitType, Out, bOverdrive);
    }

    void CaptureExperienceBeforeAward(const UObject* Controller, TMap<FName, int32>& Experience)
    {
        const auto* Roster = IsValid(Controller) ? FindFProperty<FMapProperty>(Controller->GetClass(), TEXT("PlayerUnitDataID")) : nullptr;
        const auto* Character = Roster ? CastField<FNameProperty>(Roster->KeyProp) : nullptr;
        const auto* Data = Roster ? CastField<FStructProperty>(Roster->ValueProp) : nullptr;
        const auto* Exp = Data ? FindMasteryProperty<FIntProperty>(Data->Struct, TEXT("Exp")) : nullptr;
        if (!Character || !Exp) return;

        FScriptMapHelper Units(Roster, Roster->ContainerPtrToValuePtr<void>(Controller));
        for (int32 Index = 0; Index < Units.GetMaxIndex(); ++Index)
        {
            if (!Units.IsValidIndex(Index)) continue;
            const FName ID = Character->GetPropertyValue(Units.GetKeyPtr(Index));
            if (!ID.IsNone()) Experience.Add(ID, Exp->GetPropertyValue_InContainer(Units.GetValuePtr(Index)));
        }
    }

    FReditusLevelUpChanges ReadLevelUpChanges(UUserWidget* Widget, FName ID, int32 OldExperience)
    {
        FReditusLevelUpChanges Changes;
        // This is the same function and roster data used by BP_LevelUpDialogue to print the new stats.
        auto* Library = FindObject<UClass>(nullptr, GameFunctionsPath);
        auto* Evaluate = Library ? Library->FindFunctionByName(TEXT("GetPlayerUnitStatsFromID")) : nullptr;
        if (!Evaluate || !Evaluate->HasAnyFunctionFlags(FUNC_Static)) return Changes;

        const auto* Character = FindFProperty<FNameProperty>(Evaluate, TEXT("CharacterID"));
        const auto* UnitData = FindFProperty<FStructProperty>(Evaluate, TEXT("playerUnitData"));
        const auto* Stats = FindFProperty<FStructProperty>(Evaluate, TEXT("playerUnitStats"));
        const auto* Found = FindFProperty<FBoolProperty>(Evaluate, TEXT("Found"));
        const auto* Level = FindFProperty<FIntProperty>(Evaluate, TEXT("level"));
        const auto* WorldContext = FindFProperty<FObjectPropertyBase>(Evaluate, TEXT("__WorldContext"));
        const auto* Source = FindFProperty<FStructProperty>(Widget->GetClass(), TEXT("playerUnitData"));
        if (!Character || !UnitData || !Stats || !Found || !Level || !Source || UnitData->Struct != Source->Struct) return Changes;

        const auto* Exp = FindMasteryProperty<FIntProperty>(UnitData->Struct, TEXT("Exp"));
        const void* Data = Source->ContainerPtrToValuePtr<void>(Widget);
        if (!Exp || OldExperience >= Exp->GetPropertyValue_InContainer(Data)) return Changes;

        FStructOnScope Before(Evaluate);
        FStructOnScope After(Evaluate);
        auto* Default = Library->GetDefaultObject();
        const auto Calculate = [&](FStructOnScope& Args, int32 Experience)
        {
            void* Params = Args.GetStructMemory();
            void* Unit = UnitData->ContainerPtrToValuePtr<void>(Params);
            UnitData->CopyCompleteValue(Unit, Data);
            Exp->SetPropertyValue_InContainer(Unit, Experience);
            Character->SetPropertyValue_InContainer(Params, ID);
            if (WorldContext) WorldContext->SetObjectPropertyValue_InContainer(Params, Widget);
            Default->ProcessEvent(Evaluate, Params);
            return Found->GetPropertyValue_InContainer(Params);
        };
        if (!Calculate(Before, OldExperience) || !Calculate(After, Exp->GetPropertyValue_InContainer(Data)) ||
            Level->GetPropertyValue_InContainer(Before.GetStructMemory()) >= Level->GetPropertyValue_InContainer(After.GetStructMemory()))
        {
            return Changes;
        }

        const void* OldStats = Stats->ContainerPtrToValuePtr<void>(Before.GetStructMemory());
        const void* NewStats = Stats->ContainerPtrToValuePtr<void>(After.GetStructMemory());
        static const FName ShownStats[] = {
            TEXT("maxHP"), TEXT("maxMP"), TEXT("Attack"), TEXT("defense"),
            TEXT("MagicalAttack"), TEXT("magicDefense"), TEXT("hit"), TEXT("speed")
        };
        for (FName Name : ShownStats)
        {
            if (const auto* Stat = FindMasteryProperty<FIntProperty>(Stats->Struct, Name))
            {
                const int64 Change = int64(Stat->GetPropertyValue_InContainer(NewStats)) - Stat->GetPropertyValue_InContainer(OldStats);
                if (Change != 0) Changes.Stats.Add(Name, Change);
            }
        }
        if (const auto* Time = FindMasteryProperty<FFloatProperty>(Stats->Struct, TEXT("actionTime")))
        {
            const float Change = Time->GetPropertyValue_InContainer(NewStats) - Time->GetPropertyValue_InContainer(OldStats);
            if (FMath::IsFinite(Change)) Changes.ActionTime = Change;
        }
        return Changes;
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
            if (const auto* Field = FindMasteryProperty<FIntProperty>(Row.Type, TEXT("MasteryRewardOverride")))
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

    UTextBlock* TextBlock(UObject* Menu, FName Name)
    {
        if (auto* Label = Cast<UTextBlock>(ObjectField(Menu, Name))) return Label;
        const auto* Widget = Cast<UUserWidget>(Menu);
        return Widget && Widget->WidgetTree ? Cast<UTextBlock>(Widget->WidgetTree->FindWidget(Name)) : nullptr;
    }

    bool IsMastered(const FReditusCharacterMastery* Mastery, FName SkillID, bool bOverdrive)
    {
        return Mastery && (bOverdrive ? Mastery->LearnedOverdrives : Mastery->Learned).Contains(SkillID);
    }

    FText StageText(const FReditusCharacterMastery* Mastery, FName SkillID, int32 Required, bool bOverdrive)
    {
        if (IsMastered(Mastery, SkillID, bOverdrive)) return NSLOCTEXT("ReditusMastery", "Learned", "Learned");
        if (Required <= 0) return FText::GetEmpty();
        const int32 Progress = Mastery ? (bOverdrive ? Mastery->OverdriveProgress : Mastery->Progress).FindRef(SkillID) : 0;
        return FText::Format(NSLOCTEXT("ReditusMastery", "Progress", "Mastery {0}/{1}"),
            FText::AsNumber(FMath::Clamp(Progress, 0, Required)), FText::AsNumber(Required));
    }

    FText WithDetail(const FText& Text, const FText& Detail)
    {
        if (Detail.IsEmpty()) return Text;
        if (Text.IsEmpty()) return Detail;
        return FText::Format(NSLOCTEXT("ReditusMastery", "Line", "{0}\n{1}"), Text, Detail);
    }

    FText MenuTechStage(UObject* Menu, FName SkillID, bool bOverdrive = false)
    {
        const int32 Needed = Requirement(FindRow(SkillTablePath, SkillID));
        FName CharacterID = NameField(Menu, TEXT("CastingUnit"));
        if (CharacterID.IsNone()) CharacterID = NameField(Menu, TEXT("CharacterID"));
        if (CharacterID.IsNone())
        {
            if (Needed <= 0) return FText::GetEmpty();
            return FText::Format(NSLOCTEXT("ReditusMastery", "AmountRequiredTemplate", "Requires {0} Mastery"),
                FText::AsNumber(Needed));
        }

        const auto* Mastery = CharacterProgress(Menu, CharacterID);
        if (NameField(Menu, TEXT("CharacterID")).IsNone() && !BoolField(Menu, TEXT("isEquipmentOnly")) &&
            !IsMastered(Mastery, SkillID, bOverdrive))
        {
            const auto* Unit = FindFProperty<FStructProperty>(Menu->GetClass(), TEXT("playerUnitData"));
            TArray<FName> Skills;
            if (Unit) ReadEquippedTechs(Unit->ContainerPtrToValuePtr<void>(Menu), Unit->Struct, Skills, bOverdrive);
            if (!Skills.Contains(SkillID))
                ReadCharacterEquipment(Menu, ObjectField(Menu, TEXT("jRPGPlayerController")), CharacterID, Skills, bOverdrive);
            if (!Skills.Contains(SkillID)) return FText::GetEmpty();
        }
        return StageText(Mastery, SkillID, Needed, bOverdrive);
    }

    FText DescribeMoveMastery(UObject* Unit, FName SkillID, bool bOverdrive)
    {
        const int32 Required = Requirement(FindRow(SkillTablePath, SkillID));
        const FName CharacterID = NameField(Unit, TEXT("CharacterID"));
        if (CharacterID.IsNone()) return FText::GetEmpty();
        const auto* Mastery = CharacterProgress(Unit, CharacterID);
        if (!IsMastered(Mastery, SkillID, bOverdrive))
        {
            if (Required <= 0) return FText::GetEmpty();
            TArray<FName> Skills;
            ReadCharacterEquipment(Unit, LocalPlayerController(Unit), CharacterID, Skills, bOverdrive);
            if (!Skills.Contains(SkillID)) return FText::GetEmpty();
        }
        return StageText(Mastery, SkillID, Required, bOverdrive);
    }

    AActor* OverdriveCaster(UObject* Unit)
    {
        if (auto* Actor = Cast<AActor>(Unit)) return Actor;
        return Cast<AActor>(ObjectField(Unit, TEXT("Player Unit Base")));
    }

    void RevealBattleResults(UUserWidget* Widget, UWidget* Panel, AActor* Controller, AActor* Battle,
        TSharedPtr<FReditusResultsPresentation>& Active)
    {
        auto* Camera = Cast<AActor>(ObjectField(Battle, TEXT("staticCamera")));
        auto* Player = Cast<APlayerController>(ObjectField(Controller, TEXT("jRPGPlayerController")));
        if (!Player) Player = LocalPlayerController(Widget);
        ReditusVictoryPresentation::Reveal(Widget, Panel, Player, Camera, ActorArray(Battle, TEXT("playerUnits")), Active);
    }
}

void UReditusTechMasteryLibrary::BeginMasteryEncounter(AActor* Battle)
{
    using namespace ReditusTechMasteryRuntime;
    if (auto* System = Subsystem(Battle))
    {
        ReditusVictoryPresentation::Finish(System->ResultsPresentation);
        System->LastVictory.Reset();
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
    ResolveVictoryMastery(BattleController, true);
}

void UReditusTechMasteryLibrary::ResolveVictoryMastery(AActor* BattleController, bool bBeforeExperienceAward)
{
    using namespace ReditusTechMasteryRuntime;
    auto* Battle = Cast<AActor>(ObjectField(BattleController, TEXT("currentBattle")));
    auto* System = Subsystem(Battle);
    if (!System) return;
    auto& Encounter = System->Encounters.FindOrAdd(Battle);
    if (Encounter.bAwarded) return;

    auto* Controller = ObjectField(BattleController, TEXT("jRPGPlayerController"));
    if (!IsValid(Controller)) Controller = LocalPlayerController(BattleController);
    if (bBeforeExperienceAward && Encounter.PreAwardExperience.Num() == 0)
    {
        CaptureExperienceBeforeAward(Controller, Encounter.PreAwardExperience);
    }
    auto* Save = SaveData(Battle, true);
    const TArray<AActor*> Participants = ActorArray(Battle, TEXT("playerUnits"));
    if (!Save || Participants.Num() == 0)
    {
        UE_LOG(LogReditusTechMastery, Warning, TEXT("Mastery could not read the battle participants or active save data."));
        return;
    }

    TSet<FName> Counted;
    for (AActor* Unit : Participants)
    {
        const FName CharacterID = NameField(Unit, TEXT("CharacterID"));
        if (CharacterID.IsNone() || Counted.Contains(CharacterID)) continue;
        Counted.Add(CharacterID);

        for (bool bOverdrive : {false, true})
        {
            TArray<FName> Skills;
            ReadCharacterEquipment(BattleController, Controller, CharacterID, Skills, bOverdrive);
            for (FName SkillID : Skills)
            {
                const FRow Row = FindRow(SkillTablePath, SkillID);
                const int32 Required = Requirement(Row);
                if (Required <= 0) continue;
                auto& Character = Save->Characters.FindOrAdd(CharacterID);
                auto& Learned = bOverdrive ? Character.LearnedOverdrives : Character.Learned;
                if (Learned.Contains(SkillID)) continue;

                int32& Progress = (bOverdrive ? Character.OverdriveProgress : Character.Progress).FindOrAdd(SkillID);
                Progress = ReditusTechMasteryRules::Advance(Progress, Encounter.Reward, Required);
                Encounter.bAnyProgress = true;
                const FText CharacterName = DisplayName(FindRow(PartyTablePath, CharacterID), CharacterID);
                const FText SkillName = DisplayName(Row, SkillID);
                if (Progress >= Required)
                {
                    Learned.Add(SkillID);
                    Encounter.Learned.Add(FText::Format(NSLOCTEXT("ReditusMastery", "MasteredTech", "{0} learned {1}"),
                        CharacterName, SkillName));
                }
                else
                {
                    Encounter.Progress.Add({CharacterName, SkillName, Progress, Required});
                }
            }
        }
    }
    Encounter.bAwarded = true;
    System->LastVictory = Battle;
    UE_LOG(LogReditusTechMastery, Log, TEXT("Victory mastery for %s: +%d, %d progressing, %d learned."),
        *GetNameSafe(Battle), Encounter.bAnyProgress ? Encounter.Reward : 0, Encounter.Progress.Num(), Encounter.Learned.Num());
}

void UReditusTechMasteryLibrary::AppendLearnedTechs(UObject* SkillOwner)
{
    using namespace ReditusTechMasteryRuntime;
    FName CharacterID = NameField(SkillOwner, TEXT("CharacterID"));
    if (CharacterID.IsNone()) CharacterID = NameField(SkillOwner, TEXT("CastingUnit"));
    const auto* Mastery = CharacterProgress(SkillOwner, CharacterID);
    if (!Mastery || Mastery->Learned.Num() == 0) return;

    const auto* Skills = FindFProperty<FArrayProperty>(SkillOwner->GetClass(), TEXT("availableBattleSkills"));
    AddMoves(Skills, SkillOwner, Mastery->Learned);
}

void UReditusTechMasteryLibrary::AppendEquipmentAndLearnedOverdrives(UObject* SkillOwner)
{
    using namespace ReditusTechMasteryRuntime;
    if (!IsValid(SkillOwner)) return;
    const bool bBattleUnit = SkillOwner->IsA<AActor>();
    const auto* Moves = FindFProperty<FArrayProperty>(SkillOwner->GetClass(),
        bBattleUnit ? TEXT("availableOverdriveTechs") : TEXT("availableOverDriveSkills"));
    if (!Moves) return;
    const FName Character = NameField(SkillOwner, bBattleUnit ? TEXT("CharacterID") : TEXT("CastingUnit"));
    TArray<FName> Available = NamedMoves(SkillOwner, Moves->GetFName());
    if (const auto* Unit = FindFProperty<FStructProperty>(SkillOwner->GetClass(), TEXT("playerUnitData")))
    {
        ReadEquippedTechs(Unit->ContainerPtrToValuePtr<void>(SkillOwner), Unit->Struct, Available, true);
    }
    else
    {
        ReadCharacterEquipment(SkillOwner, LocalPlayerController(SkillOwner), Character, Available, true);
    }
    if (const auto* Mastery = CharacterProgress(SkillOwner, Character))
    {
        Available.Append(Mastery->LearnedOverdrives);
    }
    AddMoves(Moves, SkillOwner, Available, true);
}

void UReditusTechMasteryLibrary::ShowEquipmentSkillPages(UObject* Menu)
{
    using namespace ReditusTechMasteryRuntime;
    if (!IsValid(Menu)) return;
    const auto Techs = NamedMoves(Menu, TEXT("availableBattleSkills"));
    const auto Overdrives = NamedMoves(Menu, TEXT("AvailableOverdrive_Skills"));
    const auto* Target = FindFProperty<FArrayProperty>(Menu->GetClass(), TEXT("availableOverDriveSkills"));
    AddMoves(Target, Menu, Overdrives, true);
    const bool bOverdrive = Techs.Num() == 0 && Overdrives.Num() > 0;
    if (const auto* Mode = FindFProperty<FBoolProperty>(Menu->GetClass(), TEXT("Was overdrive tech")))
    {
        Mode->SetPropertyValue_InContainer(Menu, bOverdrive);
    }
    if (auto* Title = TextBlock(Menu, TEXT("SkillsDialogueTitle")))
    {
        Title->SetText(bOverdrive ? NSLOCTEXT("ReditusMastery", "PreviewOverdrives", "Overdrives") :
            NSLOCTEXT("ReditusMastery", "PreviewTechs", "Techs"));
    }
    auto* Show = Menu->FindFunction(TEXT("ShowSkills"));
    const auto* Skills = Show ? FindFProperty<FArrayProperty>(Show, TEXT("skills")) : nullptr;
    if (!Skills) return;
    FStructOnScope Args(Show);
    AddMoves(Skills, Args.GetStructMemory(), bOverdrive ? Overdrives : Techs);
    Menu->ProcessEvent(Show, Args.GetStructMemory());
}

FText UReditusTechMasteryLibrary::FormatVictoryMastery(UObject* VictoryWidget, const FText& RewardText)
{
    using namespace ReditusTechMasteryRuntime;
    auto* System = Subsystem(VictoryWidget);
    if (!System) return RewardText;
    auto* Battle = System->LastVictory.Get();
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
    // Older installs also connected this node in BP_SkillDetails.
    if (TextBlock(Menu, TEXT("SkillDescription"))) return;
    const FText Stage = MenuTechStage(Menu, SkillID);
    if (Stage.IsEmpty()) return;
    if (auto* Label = TextBlock(SkillButton, TEXT("SkillName")))
    {
        Label->SetText(FText::Format(NSLOCTEXT("ReditusMastery", "TechName", "{0}   {1}"),
            DisplayName(FindRow(SkillTablePath, SkillID), SkillID), Stage));
    }
}

FText UReditusTechMasteryLibrary::FormatSkillMasteryDescription(UObject*, FName, const FText& Description)
{
    // Keep saved Blueprint calls compatible with the dedicated Mastery label.
    return Description;
}

void UReditusTechMasteryLibrary::UpdateSelectedTechMastery(UObject* Menu, FName SkillID)
{
    using namespace ReditusTechMasteryRuntime;
    auto* Widget = Cast<UUserWidget>(Menu);
    auto* Description = TextBlock(Menu, TEXT("SkillDescription"));
    if (!Widget || !Description) return;
    const bool bOverdrive = NamedMoves(Menu, TEXT("availableOverDriveSkills")).Contains(SkillID) &&
        (BoolField(Menu, TEXT("Was overdrive tech")) || !NamedMoves(Menu, TEXT("availableBattleSkills")).Contains(SkillID));
    const FText Status = MenuTechStage(Menu, SkillID, bOverdrive);
    if (!ReditusVictoryPresentation::UpdateTechMastery(Widget, Description, Status))
    {
        Description->SetText(WithDetail(Description->GetText(), Status));
    }
}

void UReditusTechMasteryLibrary::ShowEquipmentMastery(UObject* Menu, UObject* Details, FName EquipmentID)
{
    using namespace ReditusTechMasteryRuntime;
    auto* Description = TextBlock(Details, TEXT("EquipmentDescription"));
    if (!Description) return;
    FText Text = Description->GetText();
    for (bool bOverdrive : {false, true})
    {
        TArray<FName> Skills;
        ReadEquipmentTechs(EquipmentID, Skills, bOverdrive);
        for (FName SkillID : Skills)
        {
            const FText Stage = MenuTechStage(Menu, SkillID, bOverdrive);
            const FRow Row = FindRow(SkillTablePath, SkillID);
            if (!Row.Data || (Stage.IsEmpty() && !bOverdrive)) continue;
            FText Name = DisplayName(Row, SkillID);
            if (bOverdrive) Name = FText::Format(NSLOCTEXT("ReditusMastery", "EquipmentOverdrive", "Overdrive  {0}"), Name);
            Text = WithDetail(Text, Stage.IsEmpty() ? Name :
                FText::Format(NSLOCTEXT("ReditusMastery", "EquipmentProgress", "{0}   {1}"), Name, Stage));
        }
    }
    Description->SetText(Text);
}

bool UReditusTechMasteryLibrary::IsTechMastered(UObject* Unit, FName SkillID)
{
    using namespace ReditusTechMasteryRuntime;
    return IsMastered(CharacterProgress(Unit, NameField(Unit, TEXT("CharacterID"))), SkillID, false);
}

FText UReditusTechMasteryLibrary::DescribeEquippedTech(UObject* Unit, FName SkillID)
{
    using namespace ReditusTechMasteryRuntime;
    return DescribeMoveMastery(Unit, SkillID, false);
}

bool UReditusTechMasteryLibrary::IsOverdriveMastered(UObject* Unit, FName SkillID)
{
    using namespace ReditusTechMasteryRuntime;
    return IsMastered(CharacterProgress(Unit, NameField(Unit, TEXT("CharacterID"))), SkillID, true);
}

FText UReditusTechMasteryLibrary::DescribeEquippedOverdrive(UObject* Unit, FName SkillID)
{
    using namespace ReditusTechMasteryRuntime;
    return DescribeMoveMastery(Unit, SkillID, true);
}

FText UReditusTechMasteryLibrary::GetOverdrivePartnerMessage(UObject* Unit, FName SkillID)
{
    using namespace ReditusTechMasteryRuntime;
    const auto Row = FindRow(SkillTablePath, SkillID);
    TArray<FName> Required;
    ReadNameArray(FindMasteryProperty<FArrayProperty>(Row.Type, TEXT("RequiredUnits")), Row.Data, Required);
    if (Required.Num() == 0) return FText::GetEmpty();

    auto* Caster = OverdriveCaster(Unit);
    const FName CasterID = NameField(Caster, TEXT("CharacterID"));
    auto* Battle = ObjectField(ObjectField(Caster, TEXT("battleController")), TEXT("currentBattle"));
    const auto Allies = ActorArray(Battle, TEXT("playerUnits"));
    FText OutOfParty;
    FText Waiting;
    const auto AddName = [](FText& Names, FName ID)
    {
        const FText Name = DisplayName(FindRow(PartyTablePath, ID), ID);
        Names = Names.IsEmpty() ? Name :
            FText::Format(NSLOCTEXT("ReditusMastery", "PartnerList", "{0}, {1}"), Names, Name);
    };
    for (FName ID : Required)
    {
        if (ID == CasterID) continue;
        AActor* Partner = nullptr;
        for (AActor* Ally : Allies)
        {
            if (NameField(Ally, TEXT("CharacterID")) == ID)
            {
                Partner = Ally;
                break;
            }
        }
        if (!Partner || IntField(Partner, TEXT("currentHP")) <= 0)
            AddName(OutOfParty, ID);
        else if (!BoolField(Partner, TEXT("OverdriveEntered")))
            AddName(Waiting, ID);
    }
    const FText Party = OutOfParty.IsEmpty() ? FText::GetEmpty() :
        FText::Format(NSLOCTEXT("ReditusMastery", "RequiredParty", "{0} must be alive and in the active party."),
            OutOfParty);
    const FText Overdrive = Waiting.IsEmpty() ? FText::GetEmpty() :
        FText::Format(NSLOCTEXT("ReditusMastery", "RequiredOverdrive", "{0} must enter Overdrive."),
            Waiting);
    return WithDetail(Party, Overdrive);
}

bool UReditusTechMasteryLibrary::CanConfirmOverdrive(UObject* Menu, FName SkillID)
{
    using namespace ReditusTechMasteryRuntime;
    auto* Caster = OverdriveCaster(Menu);
    return IntField(Caster, TEXT("currentHP")) > 0 && BoolField(Caster, TEXT("OverdriveEntered")) &&
        FindRow(SkillTablePath, SkillID).Data &&
        GetOverdrivePartnerMessage(Caster, SkillID).IsEmpty();
}

void UReditusTechMasteryLibrary::UpdateOverdriveDescription(UObject* Menu, FName SkillID)
{
    using namespace ReditusTechMasteryRuntime;
    if (auto* Description = TextBlock(Menu, TEXT("SkillDescriptionText")))
    {
        const auto Row = FindRow(SkillTablePath, SkillID);
        const auto* Short = FindMasteryProperty<FTextProperty>(Row.Type, TEXT("ShortDescription"));
        const FText Summary = Row.Data && Short ? Short->GetPropertyValue_InContainer(Row.Data) : FText::GetEmpty();
        Description->SetText(WithDetail(GetOverdrivePartnerMessage(Menu, SkillID),
            WithDetail(Summary, DescribeEquippedOverdrive(OverdriveCaster(Menu), SkillID))));
    }
}

void UReditusTechMasteryLibrary::PresentVictory(UObject* VictoryWidget)
{
    using namespace ReditusTechMasteryRuntime;
    auto* Widget = Cast<UUserWidget>(VictoryWidget);
    if (!Widget) return;
    auto* Controller = BattleControllerFor(VictoryWidget);
    if (Controller) ResolveVictoryMastery(Controller, false);
    auto* System = Subsystem(Widget);
    if (!System) return;
    auto* Battle = System->LastVictory.Get();
    const auto* Result = System->Encounters.Find(Battle);
    auto* Panel = ReditusVictoryPresentation::Build(Widget, Cast<UPanelWidget>(ObjectField(Widget, TEXT("ItemList"))),
        IntField(Widget, TEXT("expReward")), IntField(Widget, TEXT("goldReward")),
        TextBlock(Widget, TEXT("ExpRewardText")), Result);
    if (!Panel) return;

    if (!Battle) Battle = Cast<AActor>(ObjectField(Controller, TEXT("currentBattle")));
    RevealBattleResults(Widget, Panel, Controller, Battle, System->ResultsPresentation);
}

void UReditusTechMasteryLibrary::PresentLevelUp(UObject* LevelUpWidget)
{
    using namespace ReditusTechMasteryRuntime;
    auto* Widget = Cast<UUserWidget>(LevelUpWidget);
    auto* System = Subsystem(Widget);
    if (!System) return;
    auto* Controller = BattleControllerFor(Widget);
    auto* Battle = Cast<AActor>(ObjectField(Controller, TEXT("currentBattle")));
    const FName Character = NameField(Widget, TEXT("playerUnit"));
    const auto* Encounter = Battle ? System->Encounters.Find(Battle) : nullptr;
    const int32* Experience = Encounter ? Encounter->PreAwardExperience.Find(Character) : nullptr;
    const auto Changes = Experience ? ReadLevelUpChanges(Widget, Character, *Experience) : FReditusLevelUpChanges{};
    auto* Panel = ReditusVictoryPresentation::BuildLevelUp(Widget, Changes);
    RevealBattleResults(Widget, Panel, Controller, Battle, System->ResultsPresentation);
}

void UReditusTechMasteryLibrary::HoldBattleUnitsForHUD(AActor* Battle)
{
    using namespace ReditusTechMasteryRuntime;
    auto* System = Subsystem(Battle);
    auto* Controller = Cast<AActor>(ObjectField(Battle, TEXT("battleController")));
    auto* PresenterClass = FindObject<UClass>(nullptr, TEXT("/Script/Chapter1Reditus.ReditusBattleHUDPresenterComponent"));
    if (!System || !Controller || !PresenterClass) return;
    UActorComponent* Presenter = Controller->GetComponentByClass(PresenterClass);
    auto* HUD = Cast<UUserWidget>(ObjectField(Presenter, TEXT("HUD")));
    if (!IsValid(Presenter) || (IsValid(HUD) && HUD->IsInViewport())) return;

    for (auto It = System->HiddenIntroUnits.CreateIterator(); It; ++It)
    {
        if (!It.Key().IsValid()) It.RemoveCurrent();
    }

    auto& Hidden = System->HiddenIntroUnits.FindOrAdd(Battle);
    for (FName Roster : { FName(TEXT("playerUnits")), FName(TEXT("enemyUnits")) })
    {
        for (auto* Unit : ActorArray(Battle, Roster))
        {
            if (Unit->IsHidden()) continue;
            Hidden.AddUnique(Unit);
            Unit->SetActorHiddenInGame(true);
        }
    }
}

void UReditusTechMasteryLibrary::ReleaseBattleIntro(AActor* BattleController)
{
    using namespace ReditusTechMasteryRuntime;
    auto* System = Subsystem(BattleController);
    if (!System) return;
    auto* Battle = Cast<AActor>(ObjectField(BattleController, TEXT("currentBattle")));
    TArray<TWeakObjectPtr<AActor>> Hidden;
    if (!Battle || !System->HiddenIntroUnits.RemoveAndCopyValue(Battle, Hidden)) return;
    for (const auto& Unit : Hidden)
    {
        if (auto* Actor = Unit.Get()) Actor->SetActorHiddenInGame(false);
    }
}
