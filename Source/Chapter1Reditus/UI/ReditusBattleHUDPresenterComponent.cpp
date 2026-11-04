#include "ReditusBattleHUDPresenterComponent.h"
#include "ReditusBattleHUDWidget.h"
#include "ReditusBattleHUDPolicy.h"
#include "ReditusBattleHUDInputPolicy.h"
#if __has_include("../ATB/ReditusATBComponent.h")
#include "../ATB/ReditusATBComponent.h"
#define REDITUS_HUD_HAS_ATB 1
#else
#define REDITUS_HUD_HAS_ATB 0
#endif
#include "Animation/WidgetAnimation.h"
#include "Components/InputComponent.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "PaperSprite.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogReditusBattleHUD, Log, All);

namespace
{
    FProperty* Property(UObject* Object, FName Name)
    {
        return IsValid(Object) ? FindFProperty<FProperty>(Object->GetClass(),Name) : nullptr;
    }

    UObject* ObjectValue(UObject* Object, FName Name)
    {
        FObjectPropertyBase* Field=CastField<FObjectPropertyBase>(Property(Object,Name));
        return Field ? Field->GetObjectPropertyValue_InContainer(Object) : nullptr;
    }

#if REDITUS_HUD_HAS_ATB
    const FName MenuSlowdownSource(TEXT("Reditus.HUD.EnemyMenuSlowdown"));

    UReditusATBComponent* RunningATB(AActor* Controller)
    {
        auto* ATB=IsValid(Controller) ? Controller->FindComponentByClass<UReditusATBComponent>() : nullptr;
        return ATB && ATB->IsATBRunning() ? ATB : nullptr;
    }
#endif

    AActor* CommandOwner(AActor* Controller, bool* bUsingATB = nullptr)
    {
        if (bUsingATB) *bUsingATB=false;
#if REDITUS_HUD_HAS_ATB
        if (const auto* ATB=RunningATB(Controller))
        {
            if (bUsingATB) *bUsingATB=true;
            AActor* Unit=Cast<AActor>(ObjectValue(Controller,TEXT("ATBCommandUnit")));
            return IsValid(Unit) && ATB->GetUnitState(Unit).Phase==EReditusATBPhase::Ready ? Unit : nullptr;
        }
#endif
        return Cast<AActor>(ObjectValue(Controller,TEXT("currentAttackingUnit")));
    }

    bool SetObjectValue(UObject* Object,FName Name,UObject* Value)
    {
        FObjectPropertyBase* Field=CastField<FObjectPropertyBase>(Property(Object,Name));
        if (!Field || (Value && !Value->IsA(Field->PropertyClass))) return false;
        Field->SetObjectPropertyValue_InContainer(Object,Value);
        return true;
    }

    bool SetSelectionValue(UObject* Object,int32 Value)
    {
        FProperty* Field=Property(Object,TEXT("SelectType"));
        if (FEnumProperty* Enum=CastField<FEnumProperty>(Field))
        {
            if (!Enum->GetEnum() || Enum->GetEnum()->GetFName()!=TEXT("E_SelectType")) return false;
            Enum->GetUnderlyingProperty()->SetIntPropertyValue(Enum->ContainerPtrToValuePtr<void>(Object),static_cast<int64>(Value));
            return true;
        }
        if (FByteProperty* Byte=CastField<FByteProperty>(Field))
        {
            if (!Byte->Enum || Byte->Enum->GetFName()!=TEXT("E_SelectType")) return false;
            Byte->SetPropertyValue_InContainer(Object,static_cast<uint8>(Value));
            return true;
        }
        return false;
    }

    double Number(UObject* Object, FName Name, double Default=0.0)
    {
        FProperty* Field=Property(Object,Name);
        if (!Field) return Default;
        const void* Address=Field->ContainerPtrToValuePtr<void>(Object);
        if (FEnumProperty* Enum=CastField<FEnumProperty>(Field))
            return static_cast<double>(Enum->GetUnderlyingProperty()->GetSignedIntPropertyValue(Address));
        FNumericProperty* Numeric=CastField<FNumericProperty>(Field);
        if (!Numeric) return Default;
        const double Value=Numeric->IsFloatingPoint() ? Numeric->GetFloatingPointPropertyValue(Address) :
            static_cast<double>(Numeric->GetSignedIntPropertyValue(Address));
        return FMath::IsFinite(Value) ? Value : Default;
    }

    bool Boolean(UObject* Object, FName Name, bool Default=false)
    {
        FBoolProperty* Field=CastField<FBoolProperty>(Property(Object,Name));
        return Field ? Field->GetPropertyValue_InContainer(Object) : Default;
    }

    FName NameValue(UObject* Object, FName Name)
    {
        FNameProperty* Field=CastField<FNameProperty>(Property(Object,Name));
        return Field ? Field->GetPropertyValue_InContainer(Object) : NAME_None;
    }

    FText Title(UObject* Object)
    {
        FTextProperty* Field=CastField<FTextProperty>(Property(Object,TEXT("title")));
        return Field ? Field->GetPropertyValue_InContainer(Object) : FText::GetEmpty();
    }

    TArray<AActor*> Actors(UObject* Object,FName Name)
    {
        TArray<AActor*> Result;
        FArrayProperty* Array=CastField<FArrayProperty>(Property(Object,Name));
        FObjectPropertyBase* Inner=Array ? CastField<FObjectPropertyBase>(Array->Inner) : nullptr;
        if (!Inner) return Result;
        FScriptArrayHelper Helper(Array,Array->ContainerPtrToValuePtr<void>(Object));
        for (int32 I=0; I<Helper.Num(); ++I)
            if (AActor* Actor=Cast<AActor>(Inner->GetObjectPropertyValue(Helper.GetRawPtr(I))))
                if (IsValid(Actor)) Result.AddUnique(Actor);
        return Result;
    }

    void ReadEnemyEffects(AActor* Unit,TArray<FReditusEnemyHUDEffect>& Effects)
    {
        using E=EReditusEnemyEffect;
        constexpr int32 Count=static_cast<int32>(E::Count);
        bool Active[Count]={};
        int32 Turns[Count];
        for (int32& Value:Turns) Value=INDEX_NONE;
        const auto Record=[&](E Kind,int32 Remaining=INDEX_NONE)
        {
            const int32 Index=static_cast<int32>(Kind);
            Active[Index]=true;
            if (Remaining>0) Turns[Index]=FMath::Max(Turns[Index],Remaining);
        };
        struct FFlag { const TCHAR* PropertyName; E Kind; };
        static const FFlag Flags[]={
            {TEXT("Staggered"),E::Staggered},{TEXT("Is DOT"),E::Poison},{TEXT("isStun"),E::Stun},
            {TEXT("Is Blind?"),E::Blind},{TEXT("IsSleeping"),E::Sleep},{TEXT("Is Confused"),E::Confusion},
            {TEXT("NewConfused"),E::Confusion},{TEXT("IsCharmed??"),E::Charm},{TEXT("IsBerserk??"),E::Berserk},
            {TEXT("IsTaunted"),E::Taunt},{TEXT("IsExhausted"),E::Exhausted},{TEXT("Swallowed"),E::Swallowed},
            {TEXT("IsRegen"),E::Regen},{TEXT("IsDefending"),E::GuardUp},{TEXT("IsProtected"),E::Protected},
            {TEXT("HitIncrease"),E::AccuracyUp}
        };
        for (const auto& Flag:Flags) if (Boolean(Unit,Flag.PropertyName)) Record(Flag.Kind);

        auto* Map=CastField<FMapProperty>(Property(Unit,TEXT("activeBuffs")));
        auto* Key=Map ? CastField<FClassProperty>(Map->KeyProp) : nullptr;
        auto* Value=Map ? CastField<FObjectPropertyBase>(Map->ValueProp) : nullptr;
        if (Key && Value)
        {
            FScriptMapHelper Buffs(Map,Map->ContainerPtrToValuePtr<void>(Unit));
            for (int32 I=0; I<Buffs.GetMaxIndex(); ++I)
            {
                if (!Buffs.IsValidIndex(I)) continue;
                UObject* Instance=Value->GetObjectPropertyValue(Buffs.GetValuePtr(I));
                if (!IsValid(Instance) || Instance->IsA(UClass::StaticClass())) continue;
                UClass* Class=Cast<UClass>(Key->GetObjectPropertyValue(Buffs.GetKeyPtr(I)));
                if (!IsValid(Class)) continue;
                const FString Name=Class->GetName();
                E Kind=E::Count;
                if (Name.Contains(TEXT("Stagger"))) Kind=E::Staggered;
                else if (Name.Contains(TEXT("Poison"))) Kind=E::Poison;
                else if (Name.Contains(TEXT("Stun"))) Kind=E::Stun;
                else if (Name.Contains(TEXT("Blind"))) Kind=E::Blind;
                else if (Name.Contains(TEXT("Sleep"))) Kind=E::Sleep;
                else if (Name.Contains(TEXT("Confus"))) Kind=E::Confusion;
                else if (Name.Contains(TEXT("Charm"))) Kind=E::Charm;
                else if (Name.Contains(TEXT("Berserk"))) Kind=E::Berserk;
                else if (Name.Contains(TEXT("Taunt"))) Kind=E::Taunt;
                else if (Name.Contains(TEXT("Exhaust"))) Kind=E::Exhausted;
                else if (Name.Contains(TEXT("Swallow"))) Kind=E::Swallowed;
                else if (Name.Contains(TEXT("Regen"))) Kind=E::Regen;
                else if (Name.Contains(TEXT("DefenseIncrease"))) Kind=E::GuardUp;
                else if (Name.Contains(TEXT("Protect"))) Kind=E::Protected;
                else if (Name.Contains(TEXT("HitIncrease"))) Kind=E::AccuracyUp;
                else if (Name.Contains(TEXT("CritIncrease"))) Kind=E::CriticalUp;
                else if (Name.Contains(TEXT("HitCounter"))) Kind=E::Counter;
                if (Kind!=E::Count)
                    Record(Kind,Boolean(Instance,TEXT("Permenant State")) ? INDEX_NONE :
                        static_cast<int32>(Number(Instance,TEXT("turnLeft"),INDEX_NONE)));
            }
        }
        for (int32 Index=0; Index<Count; ++Index)
            if (Active[Index])
            {
                FReditusEnemyHUDEffect& Effect=Effects.AddDefaulted_GetRef();
                Effect.Kind=static_cast<uint8>(Index);
                Effect.Turns=Turns[Index];
            }
    }

    TSet<FName> DeadCharacterIDs(UObject* Battle)
    {
        TSet<FName> Result;
        FMapProperty* Map=CastField<FMapProperty>(Property(Battle,TEXT("DeadCharactersByID")));
        FNameProperty* Key=Map ? CastField<FNameProperty>(Map->KeyProp) : nullptr;
        if (!Key) return Result;
        // The map stores spawn transforms as values, not actor references.
        FScriptMapHelper Helper(Map,Map->ContainerPtrToValuePtr<void>(Battle));
        for (int32 I=0; I<Helper.GetMaxIndex(); ++I)
            if (Helper.IsValidIndex(I)) Result.Add(Key->GetPropertyValue(Helper.GetKeyPtr(I)));
        return Result;
    }

    TArray<FName> Names(UObject* Object,FName Name)
    {
        TArray<FName> Result;
        FArrayProperty* Array=CastField<FArrayProperty>(Property(Object,Name));
        FNameProperty* Inner=Array ? CastField<FNameProperty>(Array->Inner) : nullptr;
        if (!Inner) return Result;
        FScriptArrayHelper Helper(Array,Array->ContainerPtrToValuePtr<void>(Object));
        for (int32 I=0; I<Helper.Num(); ++I) Result.AddUnique(Inner->GetPropertyValue(Helper.GetRawPtr(I)));
        return Result;
    }

    UObject* Thumbnail(UObject* Unit,FName StructName)
    {
        FStructProperty* Field=CastField<FStructProperty>(Property(Unit,StructName));
        if (!Field || !Field->Struct) return nullptr;
        const void* Data=Field->ContainerPtrToValuePtr<void>(Unit);
        UObject* Texture=nullptr;
        UObject* Sprite=nullptr;
        for (TFieldIterator<FProperty> It(Field->Struct); It; ++It)
        {
            FObjectPropertyBase* Asset=CastField<FObjectPropertyBase>(*It);
            if (!Asset) continue;
            UObject* Resource=Asset->GetObjectPropertyValue_InContainer(Data);
            const FString FieldName=Asset->GetName();
            if (FieldName.StartsWith(TEXT("Spritethumbnail_")) && Cast<UPaperSprite>(Resource)) Sprite=Resource;
            else if (FieldName.StartsWith(TEXT("thumbnail_")) && Cast<UTexture2D>(Resource)) Texture=Resource;
        }
        return Sprite ? Sprite : Texture;
    }

    bool CallNoArgumentEvent(UObject* Actor,FName Name)
    {
        if (!IsValid(Actor)) return false;
        UFunction* Function=Actor->FindFunction(Name);
        if (!Function || Function->NumParms != 0) return false;
        Actor->ProcessEvent(Function,nullptr);
        return true;
    }

    bool CallUnitArgumentEvent(UObject* Object,FName Name,AActor* Unit)
    {
        UFunction* Function=IsValid(Object) ? Object->FindFunction(Name) : nullptr;
        if (!Function || Function->NumParms!=1 || !IsValid(Unit)) return false;
        FObjectPropertyBase* Argument=nullptr;
        for (TFieldIterator<FProperty> It(Function); It; ++It)
            if (It->HasAnyPropertyFlags(CPF_Parm) && !It->HasAnyPropertyFlags(CPF_OutParm|CPF_ReturnParm))
                Argument=CastField<FObjectPropertyBase>(*It);
        if (!Argument || !Unit->IsA(Argument->PropertyClass)) return false;
        TArray<uint8> Parameters;
        Parameters.SetNumZeroed(Function->ParmsSize);
        Argument->SetObjectPropertyValue_InContainer(Parameters.GetData(),Unit);
        Object->ProcessEvent(Function,Parameters.GetData());
        return true;
    }

    void SetBoolValue(UObject* Object,FName Name,bool Value)
    {
        if (FBoolProperty* Field=CastField<FBoolProperty>(Property(Object,Name)))
            Field->SetPropertyValue_InContainer(Object,Value);
    }

    void SetTargetIcon(AActor* Unit,bool Visible)
    {
        UFunction* Function=IsValid(Unit) ? Unit->FindFunction(TEXT("SetTargetIconVisibility")) : nullptr;
        if (!Function || Function->NumParms!=1) return;
        FBoolProperty* Field=FindFProperty<FBoolProperty>(Function,TEXT("visibility"));
        if (!Field || !Field->HasAnyPropertyFlags(CPF_Parm) || Field->HasAnyPropertyFlags(CPF_OutParm|CPF_ReturnParm)) return;
        TArray<uint8> Parameters;
        Parameters.SetNumZeroed(Function->ParmsSize);
        Field->SetPropertyValue_InContainer(Parameters.GetData(),Visible);
        Unit->ProcessEvent(Function,Parameters.GetData());
    }

    void RestoreATBListCommands(AActor* Unit)
    {
        SetSelectionValue(Unit,3); // E_SelectType::ActionsUI
        SetBoolValue(Unit,TEXT("Confirmed"),false);
        SetBoolValue(Unit,TEXT("TargetSet"),false);
        SetBoolValue(Unit,TEXT("isInputBlocked"),false);
        CallNoArgumentEvent(Unit,TEXT("Reset Input"));
        if (auto* FX=Cast<USceneComponent>(ObjectValue(Unit,TEXT("PS_GPS_Selection_02")))) FX->SetVisibility(true);
    }

    TArray<UUserWidget*> WidgetArray(UObject* Object,FName Name)
    {
        TArray<UUserWidget*> Result;
        FArrayProperty* Array=CastField<FArrayProperty>(Property(Object,Name));
        FObjectPropertyBase* Inner=Array ? CastField<FObjectPropertyBase>(Array->Inner) : nullptr;
        if (!Inner) return Result;
        FScriptArrayHelper Helper(Array,Array->ContainerPtrToValuePtr<void>(Object));
        for (int32 I=0; I<Helper.Num(); ++I)
            Result.Add(Cast<UUserWidget>(Inner->GetObjectPropertyValue(Helper.GetRawPtr(I))));
        return Result;
    }

    UWidget* NamedWidget(UObject* Object,FName Name)
    {
        if (UWidget* Found=Cast<UWidget>(ObjectValue(Object,Name))) return Found;
        UUserWidget* OwnerWidget=Cast<UUserWidget>(Object);
        return OwnerWidget && OwnerWidget->WidgetTree ? OwnerWidget->WidgetTree->FindWidget(Name) : nullptr;
    }

    FText WidgetText(UObject* Object,FName Name)
    {
        UTextBlock* Label=Cast<UTextBlock>(NamedWidget(Object,Name));
        return Label ? Label->GetText() : FText::GetEmpty();
    }

    UUserWidget* ChildMenu(UObject* Root,FName Name)
    {
        if (UUserWidget* Result=Cast<UUserWidget>(ObjectValue(Root,Name))) return Result;
        UUserWidget* RootWidget=Cast<UUserWidget>(Root);
        return RootWidget && RootWidget->WidgetTree ? Cast<UUserWidget>(RootWidget->WidgetTree->FindWidget(Name)) : nullptr;
    }

    FText TableText(UDataTable* Table,FName ID,const FString& FieldName)
    {
        if (!Table || !Table->GetRowStruct()) return FText::GetEmpty();
        uint8* const* Row=Table->GetRowMap().Find(ID);
        if (!Row) return FText::GetEmpty();
        for (TFieldIterator<FProperty> It(Table->GetRowStruct()); It; ++It)
            if (FTextProperty* Field=CastField<FTextProperty>(*It))
                if (Field->GetName()==FieldName || Field->GetName().StartsWith(FieldName+TEXT("_")))
                    return Field->GetPropertyValue_InContainer(*Row);
        return FText::GetEmpty();
    }

    bool HasZeroMPCost(UDataTable* Table,FName ID)
    {
        if (!Table || !Table->GetRowStruct()) return false;
        uint8* const* Row=Table->GetRowMap().Find(ID);
        if (!Row) return false;
        for (TFieldIterator<FProperty> It(Table->GetRowStruct()); It; ++It)
            if (FIntProperty* Cost=CastField<FIntProperty>(*It))
                if (Cost->GetName()==TEXT("MPCost") || Cost->GetName().StartsWith(TEXT("MPCost_")))
                    return Cost->GetPropertyValue_InContainer(*Row)==0;
        return false;
    }

    // UE4 user-defined structs append a generated suffix to field names.
    FProperty* GeneratedStructField(const UStruct* Struct,const TCHAR* Name)
    {
        if (!Struct) return nullptr;
        const FString Prefix=FString(Name)+TEXT("_");
        const FName FieldName(Name);
        for (TFieldIterator<FProperty> It(Struct); It; ++It)
            if (It->GetFName()==FieldName || It->GetName().StartsWith(Prefix)) return *It;
        return nullptr;
    }

    struct FHUDTableRow
    {
        UDataTable* Table;
        const uint8* Data;
        FHUDTableRow(UDataTable* InTable,FName ID) : Table(InTable), Data(nullptr)
        {
            if (IsValid(Table))
                if (uint8* const* Row=Table->GetRowMap().Find(ID)) Data=*Row;
        }
        FProperty* Find(const TCHAR* Name) const
        {
            return Data ? GeneratedStructField(Table->GetRowStruct(),Name) : nullptr;
        }
        int32 Int(const TCHAR* Name,int32 Default=INDEX_NONE) const
        {
            FProperty* Field=Find(Name);
            if (auto* Enum=CastField<FEnumProperty>(Field))
                return static_cast<int32>(Enum->GetUnderlyingProperty()->GetSignedIntPropertyValue(Enum->ContainerPtrToValuePtr<void>(Data)));
            if (auto* Byte=CastField<FByteProperty>(Field)) return Byte->GetPropertyValue_InContainer(Data);
            if (auto* Integer=CastField<FIntProperty>(Field)) return Integer->GetPropertyValue_InContainer(Data);
            return Default;
        }
        UClass* Class(const TCHAR* Name) const
        {
            if (auto* Field=CastField<FClassProperty>(Find(Name)))
                return Cast<UClass>(Field->GetObjectPropertyValue_InContainer(Data));
            return nullptr;
        }
    };

    struct FHUDSkillAccess
    {
        TMap<FName,int32> Requirements;
        TSet<FName> EquippedMoves;
        int32 Level=INDEX_NONE;

        bool Allows(FName MoveID) const
        {
            if (MoveID.IsNone() || Level<0) return false;
            if (const int32* Required=Requirements.Find(MoveID)) return Level>=*Required;
            return EquippedMoves.Contains(MoveID);
        }
    };

    FHUDSkillAccess ReadSkillAccess(AActor* Unit,UObject* RosterOwner,UDataTable* PartyTable,UDataTable* EquipmentTable,bool bOverdrive)
    {
        FHUDSkillAccess Access;
        const FName CharacterID=NameValue(Unit,TEXT("CharacterID"));
        const auto* Level=CastField<FIntProperty>(Property(Unit,TEXT("level")));
        if (!Level || CharacterID.IsNone()) return Access;

        const FHUDTableRow Character(PartyTable,CharacterID);
        auto* Unlocks=CastField<FMapProperty>(Character.Find(bOverdrive ? TEXT("Overdriveskills") : TEXT("Battleskills")));
        const auto* SkillKey=Unlocks ? CastField<FNameProperty>(Unlocks->KeyProp) : nullptr;
        const auto* RequiredLevel=Unlocks ? CastField<FIntProperty>(Unlocks->ValueProp) : nullptr;
        if (!SkillKey || !RequiredLevel) return Access;
        Access.Level=Level->GetPropertyValue_InContainer(Unit);
        FScriptMapHelper Learnset(Unlocks,Unlocks->ContainerPtrToValuePtr<void>(Character.Data));
        for (int32 Index=0; Index<Learnset.GetMaxIndex(); ++Index)
            if (Learnset.IsValidIndex(Index))
                Access.Requirements.Add(SkillKey->GetPropertyValue(Learnset.GetKeyPtr(Index)),
                    RequiredLevel->GetPropertyValue(Learnset.GetValuePtr(Index)));

        // Only current equipment can grant a move absent from this character's learnset.
        auto* Roster=CastField<FMapProperty>(Property(RosterOwner,TEXT("PlayerUnitDataID")));
        const auto* CharacterKey=Roster ? CastField<FNameProperty>(Roster->KeyProp) : nullptr;
        auto* UnitData=Roster ? CastField<FStructProperty>(Roster->ValueProp) : nullptr;
        if (!CharacterKey || !UnitData || !UnitData->Struct) return Access;
        FScriptMapHelper RosterRows(Roster,Roster->ContainerPtrToValuePtr<void>(RosterOwner));
        for (int32 Index=0; Index<RosterRows.GetMaxIndex(); ++Index)
        {
            if (!RosterRows.IsValidIndex(Index) || CharacterKey->GetPropertyValue(RosterRows.GetKeyPtr(Index))!=CharacterID) continue;
            const void* Data=RosterRows.GetValuePtr(Index);
            static const TCHAR* Slots[]={TEXT("Weapon"),TEXT("Armor"),TEXT("Accessory"),TEXT("Helm"),TEXT("Boots"),TEXT("Accessory1")};
            for (const TCHAR* Slot:Slots)
            {
                const auto* ItemID=CastField<FNameProperty>(GeneratedStructField(UnitData->Struct,Slot));
                if (!ItemID) continue;
                const FName EquippedID=ItemID->GetPropertyValue_InContainer(Data);
                if (EquippedID.IsNone()) continue;
                const FHUDTableRow Equipment(EquipmentTable,EquippedID);
                auto* Skills=CastField<FArrayProperty>(Equipment.Find(bOverdrive ? TEXT("OverDriveSkillIDs") : TEXT("BattleSkillIDs")));
                const auto* SkillName=Skills ? CastField<FNameProperty>(Skills->Inner) : nullptr;
                if (!SkillName) continue;
                FScriptArrayHelper Grants(Skills,Skills->ContainerPtrToValuePtr<void>(Equipment.Data));
                for (int32 I=0; I<Grants.Num(); ++I)
                    Access.EquippedMoves.Add(SkillName->GetPropertyValue(Grants.GetRawPtr(I)));
            }
            break;
        }
        return Access;
    }

#if REDITUS_HUD_HAS_ATB
    struct FHUDActionTargets
    {
        TArray<AActor*> Units;
        bool bGroup=false;
    };

    FHUDActionTargets ListTargets(AActor* Controller,AActor* Owner,EReditusHUDCommand Command,const FHUDTableRow& Row,
        const TArray<FReditusBattleHUDUnit>& Cards)
    {
        FHUDActionTargets Result;
        UObject* Battle=ObjectValue(Controller,TEXT("currentBattle"));
        TArray<AActor*> Allies=Actors(Battle,TEXT("playerUnits"));
        const TArray<AActor*> Enemies=Actors(Battle,TEXT("enemyUnits"));
        for (const auto& Card:Cards) if (IsValid(Card.Unit)) Allies.AddUnique(Card.Unit);
        const bool bItem=Command==EReditusHUDCommand::Items;
        // Enum values from E_UnitFilter, E_TargetScope, and E_SkillType in the project dump.
        int32 Scope=Row.Int(TEXT("TargetScope"));
        int32 Filter=Row.Int(TEXT("UnitFilter"));
        int32 SkillType=Row.Int(TEXT("SkillType"));
        if (bItem)
        {
            UClass* ItemClass=Row.Class(TEXT("UsableItemLegacyLaunch"));
            if (!ItemClass) return Result; // The Blueprint executor needs a usable item class.
            UObject* Defaults=ItemClass->GetDefaultObject();
            const int32 LegacyFilter=static_cast<int32>(Number(Defaults,TEXT("unitFilter"),INDEX_NONE));
            // The existing executor checks the legacy filter on the spawned item.
            if (LegacyFilter==2 || Scope<=0) Filter=LegacyFilter;
            SkillType=static_cast<int32>(Number(Defaults,TEXT("skillType"),INDEX_NONE));
            if (Filter==2 && Scope<=0) Scope=3; // Revive picks a KO ally.
        }
        if (!bItem || Scope<=0)
        {
            switch (SkillType)
            {
            case 0: Scope=2; break;
            case 1: Scope=3; break;
            case 2: Scope=1; break;
            case 3: Scope=4; break;
            case 4: Scope=5; break;
            default: return Result;
            }
            if (bItem && Filter==3 && Scope!=1 && Scope!=2) Scope=2;
        }
        Result.bGroup=Scope==1 || Scope==4 || Scope==5;
        const auto Append=[&](const TArray<AActor*>& Units,bool bEnemies)
        {
            for (AActor* Unit:Units)
            {
                if (!IsValid(Unit) || !Property(Unit,TEXT("currentHP"))) continue;
                const bool bDead=Number(Unit,TEXT("currentHP"))<=0.0 || Boolean(Unit,TEXT("IsDead?"));
                if (!bItem && bDead) continue;
                if (bItem && (Filter==2 ? (bEnemies || !bDead) : (bDead && (bEnemies || Filter!=0)))) continue;
                Result.Units.AddUnique(Unit);
            }
        };
        if (Scope==1 || Scope==2 || Scope==5) Append(Enemies,true);
        if (Scope==3 || Scope==4 || Scope==5)
        {
            // Also works when only the ready player's actor is available in the HUD.
            Allies.AddUnique(Owner);
            Append(Allies,false);
        }
        return Result;
    }

    bool CanUseATBListEntry(AActor* Unit,EReditusHUDCommand Command,const FHUDTableRow& Row)
    {
        if (!Row.Data) return false;
        if (Command==EReditusHUDCommand::Items)
        {
            const FBoolProperty* ExploreOnly=CastField<FBoolProperty>(Row.Find(TEXT("ExploreOnly")));
            return !ExploreOnly || !ExploreOnly->GetPropertyValue_InContainer(Row.Data);
        }
        if (Command==EReditusHUDCommand::Overdrive &&
            (!Boolean(Unit,TEXT("CanEnterOverdrive")) || !Boolean(Unit,TEXT("OverdriveEntered")))) return false;
        const int32 Cost=Row.Int(TEXT("MPCost"));
        return Cost>=0 && Number(Unit,TEXT("currentMP"),-1)>=Cost;
    }

    int32 LiveItemCount(UObject* PlayerController,FName ID,int32 Fallback)
    {
        FMapProperty* Map=CastField<FMapProperty>(Property(PlayerController,TEXT("usableItems")));
        const auto* Key=Map ? CastField<FNameProperty>(Map->KeyProp) : nullptr;
        const auto* Quantity=Map ? CastField<FIntProperty>(Map->ValueProp) : nullptr;
        if (!Key || !Quantity) return Fallback;
        FScriptMapHelper Items(Map,Map->ContainerPtrToValuePtr<void>(PlayerController));
        for (int32 Index=0; Index<Items.GetMaxIndex(); ++Index)
            if (Items.IsValidIndex(Index) && Key->GetPropertyValue(Items.GetKeyPtr(Index))==ID)
                return FMath::Max(0,Quantity->GetPropertyValue(Items.GetValuePtr(Index)));
        return 0;
    }
#endif

    UObject* TableThumbnail(UDataTable* Table,FName ID)
    {
        if (!IsValid(Table) || !Table->GetRowStruct()) return nullptr;
        uint8* const* Row=Table->GetRowMap().Find(ID);
        if (!Row) return nullptr;
        for (TFieldIterator<FProperty> It(Table->GetRowStruct()); It; ++It)
        {
            FStructProperty* ImageField=CastField<FStructProperty>(*It);
            if (!ImageField || !ImageField->Struct ||
                !(ImageField->GetName()==TEXT("Thumbnail") || ImageField->GetName().StartsWith(TEXT("Thumbnail_")))) continue;
            const void* ImageData=ImageField->ContainerPtrToValuePtr<void>(*Row);
            UObject* Texture=nullptr;
            UObject* Sprite=nullptr;
            for (TFieldIterator<FProperty> AssetIt(ImageField->Struct); AssetIt; ++AssetIt)
            {
                FObjectPropertyBase* Asset=CastField<FObjectPropertyBase>(*AssetIt);
                if (!Asset) continue;
                UObject* Resource=Asset->GetObjectPropertyValue_InContainer(ImageData);
                if (!IsValid(Resource)) continue;
                const FString FieldName=Asset->GetName();
                if ((FieldName==TEXT("thumbnail") || FieldName.StartsWith(TEXT("thumbnail_"))) && Cast<UTexture2D>(Resource)) Texture=Resource;
                else if ((FieldName==TEXT("Spritethumbnail") || FieldName.StartsWith(TEXT("Spritethumbnail_"))) && Cast<UPaperSprite>(Resource)) Sprite=Resource;
            }
            // Match the legacy skill-row preference: texture when supplied, then sprite.
            return Texture ? Texture : Sprite;
        }
        return nullptr;
    }

    FSlateBrush ThumbnailBrush(UObject* Resource)
    {
        FSlateBrush Result;
        if (IsValid(Resource))
        {
            Result.DrawAs=ESlateBrushDrawType::Image;
            Result.ImageSize=FVector2D(26.f,26.f);
            Result.SetResourceObject(Resource);
        }
        return Result;
    }

    bool ExecuteInputAction(AActor* Actor,FName Action,FKey Key,bool bSingleBinding=false)
    {
        if (!IsValid(Actor)) return false;
        TArray<UInputComponent*> Components;
        Actor->GetComponents<UInputComponent>(Components);
        // The attacker's own binding stays usable after another actor takes
        // the player-controller input stack. Prefer it for a single press.
        if (bSingleBinding)
            if (auto* Primary=Cast<UInputComponent>(ObjectValue(Actor,TEXT("InputComponent"))))
            {
                Components.Remove(Primary);
                Components.Insert(Primary,0);
            }
        // Copy delegates first: a Blueprint callback can change input components.
        TArray<FInputActionUnifiedDelegate> Delegates;
        for (UInputComponent* Input : Components)
        {
            if (!IsValid(Input)) continue;
            for (int32 I=0; I<Input->GetNumActionBindings(); ++I)
            {
                const FInputActionBinding& Binding=Input->GetActionBinding(I);
                if (Binding.GetActionName()==Action && Binding.KeyEvent==IE_Pressed && Binding.ActionDelegate.IsBound())
                {
                    Delegates.Add(Binding.ActionDelegate);
                    if (bSingleBinding) break;
                }
            }
            if (bSingleBinding && Delegates.Num()>0) break;
        }
        for (FInputActionUnifiedDelegate& Delegate : Delegates) Delegate.Execute(Key);
        return Delegates.Num()>0;
    }
}

UReditusBattleHUDPresenterComponent::UReditusBattleHUDPresenterComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickInterval=0.05f;
    WidgetClass=UReditusBattleHUDWidget::StaticClass();
    // Description tables contain gameplay Blueprint class references. Do not
    // load them from the native class default object during module startup.
    // BeginNativeMenu resolves the relevant table during an actual battle.
}

UReditusBattleHUDWidget* UReditusBattleHUDPresenterComponent::CreateBattleHUD(APlayerController* PlayerController,int32 ZOrder)
{
    if (IsValid(HUD)) return HUD;
    if (!IsValid(PlayerController) || !PlayerController->IsLocalController() || !WidgetClass) return nullptr;
    Player=PlayerController;
    HUD=CreateWidget<UReditusBattleHUDWidget>(Player,WidgetClass);
    if (!HUD) return nullptr;
    HUD->CommandRouter.BindUObject(this,&UReditusBattleHUDPresenterComponent::HandleCommand);
    HUD->GameplayInputRouter.BindUObject(this,&UReditusBattleHUDPresenterComponent::ForwardGameplayInput);
    HUD->OnSelectedUnitChanged.AddUniqueDynamic(this,&UReditusBattleHUDPresenterComponent::HandleSelection);
    HUD->OnInputDeviceChanged.AddUniqueDynamic(this,&UReditusBattleHUDPresenterComponent::HandleDevice);
    HUD->OnMenuEntryChosen.AddUniqueDynamic(this,&UReditusBattleHUDPresenterComponent::HandleNativeMenuChosen);
    HUD->OnMenuCancelled.AddUniqueDynamic(this,&UReditusBattleHUDPresenterComponent::HandleNativeMenuCancelled);
    HUD->SetInputDevice(Boolean(Player,TEXT("IsUsingKeyboard"),true) ? EReditusHUDInputDevice::KeyboardMouse : EReditusHUDInputDevice::Xbox);
    if (!HUD->AddToPlayerScreen(ZOrder))
    {
        HUD=nullptr;
        return nullptr;
    }
    if (bSetGameAndUIInputMode)
    {
        FInputModeGameAndUI Mode;
        Mode.SetWidgetToFocus(HUD->TakeWidget());
        Mode.SetHideCursorDuringCapture(false);
        Player->SetInputMode(Mode);
    }
    ApplyCombatInputProfile();
    RefreshBattleHUD();
    return HUD;
}

void UReditusBattleHUDPresenterComponent::DestroyBattleHUD()
{
    RestoreCombatInputProfile();
    ClearATBListTargeting();
    ClearEnemyMenuSlowdown();
    OpenLegacyMenuWindows.Reset();
    RestoreLegacyEnemyComponents();
    ExplicitEnemyTarget.Reset();
    bShowingEnemyATBAnnouncement=false;
    ClearNativeMenu();
    for (int32 I=0; I<SuppressedMenuWidgets.Num(); ++I)
        if (IsValid(SuppressedMenuWidgets[I]) && SuppressedMenuVisibility.IsValidIndex(I))
        {
            SuppressedMenuWidgets[I]->SetVisibility(SuppressedMenuVisibility[I]);
            if (SuppressedMenuOpacity.IsValidIndex(I)) SuppressedMenuWidgets[I]->SetRenderOpacity(SuppressedMenuOpacity[I]);
        }
    SuppressedMenuWidgets.Reset();
    SuppressedMenuVisibility.Reset();
    SuppressedMenuOpacity.Reset();
    ReleaseLegacyMenuRoot();
    if (IsValid(HUD))
    {
        HUD->CommandRouter.Unbind();
        HUD->GameplayInputRouter.Unbind();
        HUD->RemoveFromParent();
    }
    HUD=nullptr;
    DisplaySlots.Reset();
    PreviousLegacyUnit.Reset();
    LastTurnCueCycles.Reset();
    for (int32 I=0; I<HiddenLegacyWidgets.Num(); ++I)
        if (IsValid(HiddenLegacyWidgets[I]) && PreviousLegacyVisibility.IsValidIndex(I))
            HiddenLegacyWidgets[I]->SetVisibility(PreviousLegacyVisibility[I]);
    HiddenLegacyWidgets.Reset();
    PreviousLegacyVisibility.Reset();
}

void UReditusBattleHUDPresenterComponent::ApplyCombatInputProfile()
{
    if (!bApplyCombatGamepadProfile || !HUD || !Player || !Player->PlayerInput || InputOverrides.Num()>0) return;
    UPlayerInput* Input=Player->PlayerInput;
    const auto Override=[this,Input](FName Action,FKey Key,bool AllDevices=false,FKey Keyboard=FKey(),FKey AlternateKeyboard=FKey()) {
        if (Action.IsNone()) return;
        FInputOverride Saved;
        Saved.Action=Action;
        Saved.Before=Input->GetKeysForAction(Action);
        Saved.Applied=Saved.Before;
        Saved.Applied.RemoveAll([AllDevices](const FInputActionKeyMapping& Mapping) { return AllDevices || Mapping.Key.IsGamepadKey(); });
        if (Key.IsValid()) Saved.Applied.AddUnique(FInputActionKeyMapping(Action,Key));
        if (Keyboard.IsValid()) Saved.Applied.AddUnique(FInputActionKeyMapping(Action,Keyboard));
        if (AlternateKeyboard.IsValid()) Saved.Applied.AddUnique(FInputActionKeyMapping(Action,AlternateKeyboard));
        for (const auto& Mapping : Saved.Before) Input->RemoveActionMapping(Mapping);
        for (const auto& Mapping : Saved.Applied) Input->AddActionMapping(Mapping);
        InputOverrides.Add(Saved);
    };
    Override(HUD->InputActions.Block,HUD->GamepadKeys.Block,true,HUD->KeyboardBlockKey);
    Override(HUD->InputActions.TimedAttack,HUD->GamepadKeys.Block,true,HUD->KeyboardBlockKey);
    Override(HUD->InputActions.DecreaseAmp,HUD->GamepadKeys.DecreaseAmp);
    Override(HUD->InputActions.IncreaseAmp,HUD->GamepadKeys.IncreaseAmp);
    // Chords are owned by the HUD. Remove the old unmodified Y party action.
    Override(HUD->InputActions.ChangeParty,FKey());
    Override(TEXT("MenuConfirm"),HUD->GamepadKeys.Bottom,true,HUD->KeyboardConfirmKey,HUD->KeyboardAlternateConfirmKey);
    Override(TEXT("MenuCancel"),HUD->GamepadBackKey,true,HUD->KeyboardBackKey);
    Input->ForceRebuildingKeyMaps(false);
}

void UReditusBattleHUDPresenterComponent::RestoreCombatInputProfile()
{
    if (Player && Player->PlayerInput)
    {
        UPlayerInput* Input=Player->PlayerInput;
        for (const FInputOverride& Saved : InputOverrides)
        {
            const TArray<FInputActionKeyMapping> Current=Input->GetKeysForAction(Saved.Action);
            bool Unchanged=Current.Num()==Saved.Applied.Num();
            for (const auto& Mapping : Current) Unchanged=Unchanged && Saved.Applied.Contains(Mapping);
            // A settings screen may have rebound the action during the battle.
            // Preserve that newer mapping rather than restoring an old snapshot.
            if (!Unchanged) continue;
            for (const auto& Mapping : Current) Input->RemoveActionMapping(Mapping);
            for (const auto& Mapping : Saved.Before) Input->AddActionMapping(Mapping);
        }
        Input->ForceRebuildingKeyMaps(false);
    }
    InputOverrides.Reset();
}

UUserWidget* UReditusBattleHUDPresenterComponent::FindLegacyMenu(AActor* Unit,EReditusHUDCommand Command) const
{
    const FName Field=Command==EReditusHUDCommand::Items ? TEXT("BP_ItemUseDialogue") :
        Command==EReditusHUDCommand::Overdrive ? TEXT("BP_OverdriveDialogue") : TEXT("BP_SkillUseDialogue");
    UObject* Roots[]={ObjectValue(GetOwner(),TEXT("battleUI")),ObjectValue(Unit,TEXT("battleUI"))};
    UUserWidget* First=nullptr;
    for (UObject* Root : Roots)
        if (UUserWidget* Found=ChildMenu(Root,Field))
        {
            if (ObjectValue(Found,TEXT("Player Unit Base"))==Unit) return Found;
            if (!First) First=Found;
        }
    return First;
}

bool UReditusBattleHUDPresenterComponent::EnsureLegacyMenuRoot(AActor* Unit)
{
    if (!Player || !GetWorld() || !GetWorld()->IsGameWorld()) return false;
    UUserWidget* Root=Cast<UUserWidget>(ObjectValue(GetOwner(),TEXT("battleUI")));
    if (!Root) Root=Cast<UUserWidget>(ObjectValue(Unit,TEXT("battleUI")));
    bool Created=false;
    if (!Root && bCreateMissingMenuSource && !bAttemptedLegacyRootCreation)
    {
        bAttemptedLegacyRootCreation=true;
        UClass* SourceClass=LoadClass<UUserWidget>(nullptr,TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/UI/BP_BattleUI.BP_BattleUI_C"));
        Root=SourceClass ? CreateWidget<UUserWidget>(Player,SourceClass) : nullptr;
        Created=Root!=nullptr;
    }
    if (!IsValid(Root)) return false;
    if (!AssignedLegacyRoot) AssignedLegacyRoot=Root;
    if (!ObjectValue(GetOwner(),TEXT("battleUI")))
    {
        if (!SetObjectValue(GetOwner(),TEXT("battleUI"),Root)) return false;
        bLinkedControllerRoot=true;
    }
    if (!ObjectValue(Root,TEXT("battleController"))) SetObjectValue(Root,TEXT("battleController"),GetOwner());
    if (!ObjectValue(Root,TEXT("currentBattle"))) SetObjectValue(Root,TEXT("currentBattle"),ObjectValue(GetOwner(),TEXT("currentBattle")));
    if (!ObjectValue(Root,TEXT("jRPGPlayerController"))) SetObjectValue(Root,TEXT("jRPGPlayerController"),Player);
    if (!ObjectValue(Unit,TEXT("battleUI")) && SetObjectValue(Unit,TEXT("battleUI"),Root)) LinkedLegacyRootUnits.AddUnique(Unit);
    const bool Hidden=Root->GetVisibility()==ESlateVisibility::Hidden || Root->GetVisibility()==ESlateVisibility::Collapsed;
    const bool Unattached=!Root->IsInViewport() && !Root->GetParent();
    if (!HostedLegacyRoot && (Created || Unattached || Hidden))
    {
        HostedLegacyRoot=Root;
        HostedRootVisibility=Root->GetVisibility();
        HostedRootOpacity=Root->GetRenderOpacity();
        bAddedLegacyRootToScreen=Unattached;
        Root->SetRenderOpacity(0.f);
        Root->SetVisibility(ESlateVisibility::HitTestInvisible);
        if (bAddedLegacyRootToScreen && !Root->AddToPlayerScreen(0)) { ReleaseLegacyMenuRoot(); return false; }
    }
    return true;
}

void UReditusBattleHUDPresenterComponent::ReleaseLegacyMenuRoot()
{
    if (IsValid(HostedLegacyRoot))
    {
        if (bAddedLegacyRootToScreen) HostedLegacyRoot->RemoveFromParent();
        HostedLegacyRoot->SetVisibility(HostedRootVisibility);
        HostedLegacyRoot->SetRenderOpacity(HostedRootOpacity);
    }
    if (bLinkedControllerRoot && ObjectValue(GetOwner(),TEXT("battleUI"))==AssignedLegacyRoot) SetObjectValue(GetOwner(),TEXT("battleUI"),nullptr);
    for (AActor* Unit : LinkedLegacyRootUnits)
        if (ObjectValue(Unit,TEXT("battleUI"))==AssignedLegacyRoot) SetObjectValue(Unit,TEXT("battleUI"),nullptr);
    HostedLegacyRoot=nullptr;
    AssignedLegacyRoot=nullptr;
    LinkedLegacyRootUnits.Reset();
    bAddedLegacyRootToScreen=false;
    bLinkedControllerRoot=false;
    bAttemptedLegacyRootCreation=false;
}

void UReditusBattleHUDPresenterComponent::RecoverMenuPreparation(AActor* Unit,EReditusHUDCommand Command)
{
    if (!bAwaitingNativeMenu || PendingMenuOwner!=Unit || PendingMenuCommand!=Command || !IsValid(Unit) || Boolean(Unit,TEXT("isInputBlocked"))) return;
    UUserWidget* Source=FindLegacyMenu(Unit,Command);
    const int32 Selection=static_cast<int32>(Number(Unit,TEXT("SelectType"),-1.0));
    if (Source && (ObjectValue(Source,TEXT("Player Unit Base"))!=Unit || Selection==3))
    {
        const FName Event=Command==EReditusHUDCommand::Items ? TEXT("ShowItemInventory") :
            Command==EReditusHUDCommand::Overdrive ? TEXT("ShowOverdrive") : TEXT("ShowSkills");
        if (CallUnitArgumentEvent(GetOwner(),Event,Unit))
        {
            UUserWidget* Prepared=FindLegacyMenu(Unit,Command);
            const int32 PreparedSelection=static_cast<int32>(Number(Unit,TEXT("SelectType"),-1.0));
            if (IsValid(Prepared) && ObjectValue(Prepared,TEXT("Player Unit Base"))==Unit &&
                CommandOwner(GetOwner())==Unit && !Boolean(Unit,TEXT("isInputBlocked")) &&
                (PreparedSelection==0 || PreparedSelection==3))
            {
                SetSelectionValue(Unit,0);
                CallNoArgumentEvent(Unit,TEXT("Reset Input"));
            }
        }
    }
}

void UReditusBattleHUDPresenterComponent::BeginNativeMenu(AActor* Unit,EReditusHUDCommand Command)
{
    if (!HUD || !IsValid(Unit) || !bUseNativeCommandLists || (Command!=EReditusHUDCommand::Tech && Command!=EReditusHUDCommand::Items && Command!=EReditusHUDCommand::Overdrive)) return;
    UWorld* BattleWorld=GetWorld();
    if (!BattleWorld || !BattleWorld->IsGameWorld()) return;
    PendingOriginalSelection=static_cast<int32>(Number(Unit,TEXT("SelectType"),-1.0));
    PendingMenuOwner=Unit;
    PendingMenuCommand=Command;
    bAwaitingNativeMenu=true;
    const FText OpeningTitle=FText::FromString(Command==EReditusHUDCommand::Items ? TEXT("ITEMS") : Command==EReditusHUDCommand::Overdrive ? TEXT("OVERDRIVE") : TEXT("SKILLS"));
    HUD->ShowListStatus(OpeningTitle,NSLOCTEXT("ReditusHUD","LoadingChoices","Loading available choices..."));
    EnsureLegacyMenuRoot(Unit);
    // Keep explicit table assignments; resolve the default only when opening
    // a game-time list, after the editor/plugin startup phases have completed.
    UDataTable*& DescriptionTable=Command==EReditusHUDCommand::Items ? ItemDescriptions : SkillDescriptions;
    if (!IsValid(DescriptionTable))
    {
        const TCHAR* DescriptionTablePath=Command==EReditusHUDCommand::Items ?
            TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_UsableItems.DT_UsableItems") :
            TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_Skills.DT_Skills");
        DescriptionTable=LoadObject<UDataTable>(nullptr,DescriptionTablePath);
        if (!DescriptionTable)
            UE_LOG(LogReditusBattleHUD,Warning,TEXT("HUD description table unavailable: %s. The list will keep its existing labels and command continuation."),DescriptionTablePath);
    }
    if (Command!=EReditusHUDCommand::Items)
    {
        if (!IsValid(PartyMemberSkillDefinitions))
            PartyMemberSkillDefinitions=LoadObject<UDataTable>(nullptr,TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_PartyMembers.DT_PartyMembers"));
        if (!IsValid(EquipmentSkillDefinitions))
            EquipmentSkillDefinitions=LoadObject<UDataTable>(nullptr,TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_Equipment.DT_Equipment"));
    }
    PendingMenuDeadline=BattleWorld->GetRealTimeSeconds()+1.0;
}

void UReditusBattleHUDPresenterComponent::TryCaptureNativeMenu()
{
    if (!bAwaitingNativeMenu || !HUD) return;
    if (!IsValid(PendingMenuOwner)) { ClearNativeMenu(); HUD->CloseListMenu(); return; }
    if (CommandOwner(GetOwner())!=PendingMenuOwner || Number(PendingMenuOwner,TEXT("currentHP"))<=0.0)
    {
        ClearNativeMenu();
        HUD->CloseListMenu();
        return;
    }
    const FName Field=PendingMenuCommand==EReditusHUDCommand::Items ? TEXT("BP_ItemUseDialogue") :
        PendingMenuCommand==EReditusHUDCommand::Overdrive ? TEXT("BP_OverdriveDialogue") : TEXT("BP_SkillUseDialogue");
    UObject* MenuRoots[]={ObjectValue(GetOwner(),TEXT("battleUI")),ObjectValue(PendingMenuOwner,TEXT("battleUI"))};
    UUserWidget* Source=nullptr;
    UUserWidget* Candidate=nullptr;
    for (UObject* MenuRoot : MenuRoots)
    {
        UUserWidget* Found=ChildMenu(MenuRoot,Field);
        if (!IsValid(Found)) continue;
        Candidate=Found;
        if (ObjectValue(Candidate,TEXT("Player Unit Base"))==PendingMenuOwner) { Source=Candidate; break; }
    }
    if (!Source)
    {
        if (GetWorld() && GetWorld()->GetRealTimeSeconds()>PendingMenuDeadline)
        {
            bAwaitingNativeMenu=false;
            LastRouteStatus=FString::Printf(TEXT("Native list source unavailable: %s; requested owner=%s; candidate=%s; candidate owner=%s; controller battleUI=%s; unit battleUI=%s"),
                *Field.ToString(),*GetNameSafe(PendingMenuOwner),*GetNameSafe(Candidate),*GetNameSafe(ObjectValue(Candidate,TEXT("Player Unit Base"))),
                *GetNameSafe(MenuRoots[0]),*GetNameSafe(MenuRoots[1]));
            UE_LOG(LogReditusBattleHUD,Warning,TEXT("%s. Existing dialogue remains authoritative."),*LastRouteStatus);
            HUD->ShowListStatus(FText::FromString(PendingMenuCommand==EReditusHUDCommand::Items ? TEXT("ITEMS") : PendingMenuCommand==EReditusHUDCommand::Overdrive ? TEXT("OVERDRIVE") : TEXT("SKILLS")),
                NSLOCTEXT("ReditusHUD","ChoicesUnavailable","Choices could not be prepared. Press Back to return."));
        }
        return;
    }
    NativeMenuSource=Source;
    NativeMenuOwner=PendingMenuOwner;
    NativeMenuCommand=PendingMenuCommand;
    PendingMenuOwner=nullptr;
    bAwaitingNativeMenu=false;
    const int32 Existing=SuppressedMenuWidgets.IndexOfByKey(Source);
    if (Existing==INDEX_NONE)
    {
        SuppressedMenuWidgets.Add(Source);
        SuppressedMenuVisibility.Add(Source->GetVisibility());
        SuppressedMenuOpacity.Add(Source->GetRenderOpacity());
    }
    Source->SetVisibility(ESlateVisibility::HitTestInvisible);
    Source->SetRenderOpacity(0.f);
    RefreshNativeMenuEntries(true);
}

void UReditusBattleHUDPresenterComponent::RefreshNativeMenuEntries(bool bOpen)
{
    if (!NativeMenuSource || !HUD) return;
    const bool Items=NativeMenuCommand==EReditusHUDCommand::Items;
    const bool Overdrive=NativeMenuCommand==EReditusHUDCommand::Overdrive;
    const FHUDSkillAccess SkillAccess=Items ? FHUDSkillAccess{} :
        ReadSkillAccess(NativeMenuOwner,Player,PartyMemberSkillDefinitions,EquipmentSkillDefinitions,Overdrive);
#if REDITUS_HUD_HAS_ATB
    const bool bUsingATB=RunningATB(GetOwner())!=nullptr;
#endif
    NativeMenuRows=WidgetArray(NativeMenuSource,Items ? TEXT("itemUIs") : TEXT("skillUIs"));
    NativeMenuIDs.Init(NAME_None,NativeMenuRows.Num());
    NativeMenuDataIDs.Init(NAME_None,NativeMenuRows.Num());
    TArray<FReditusBattleHUDMenuEntry> Entries;
    TSet<FName> Used;
    bool CopiedFonts=false;
    for (int32 I=0; I<NativeMenuRows.Num(); ++I)
    {
        UUserWidget* Row=NativeMenuRows[I];
        if (!IsValid(Row)) continue;
        if (!HUD->ListSelectionBrush.GetResourceObject())
            if (UImage* SelectedImage=Cast<UImage>(NamedWidget(Row,TEXT("SelectedImage"))))
                if (SelectedImage->Brush.GetResourceObject())
                {
                    HUD->ListSelectionBrush=SelectedImage->Brush;
                    HUD->ListSelectionBrush.DrawAs=ESlateBrushDrawType::Image;
                    HUD->ListSelectionBrush.TintColor=FSlateColor(FLinearColor::White);
                }
        const FName DataID=NameValue(Row,Items ? TEXT("item") : TEXT("skill"));
        if (DataID.IsNone() || (!Items && !SkillAccess.Allows(DataID))) continue;
        FReditusBattleHUDMenuEntry Entry;
        Entry.ID=DataID;
        if (Used.Contains(Entry.ID)) Entry.ID=FName(*FString::Printf(TEXT("HUDRow_%d"),I));
        Used.Add(Entry.ID);
        NativeMenuIDs[I]=Entry.ID;
        NativeMenuDataIDs[I]=DataID;
        Entry.Label=WidgetText(Row,Items ? TEXT("ItemName") : TEXT("SkillName"));
        UDataTable* DisplayTable=Items ? ItemDescriptions : SkillDescriptions;
        if (Entry.Label.IsEmpty()) Entry.Label=TableText(DisplayTable,DataID,TEXT("DisplayName"));
        if (Entry.Label.IsEmpty()) Entry.Label=FText::FromName(DataID);
        // Item quantity belongs to this row's item ID, never the inventory's entry count.
        int32 Quantity=Items ? FMath::Max(0,static_cast<int32>(Number(Row,TEXT("amount")))) : 0;
#if REDITUS_HUD_HAS_ATB
        if (bUsingATB && Items) Quantity=LiveItemCount(Player,DataID,Quantity);
#endif
        Entry.Detail=Items ? FText::Format(NSLOCTEXT("ReditusHUD","ItemQuantity","x{0}"),
            FText::AsNumber(Quantity)) : WidgetText(Row,TEXT("MPAmount"));
        if (!Items && HasZeroMPCost(DisplayTable,DataID)) Entry.Detail=FText::GetEmpty();
        Entry.Description=TableText(DisplayTable,DataID,TEXT("ShortDescription"));
        if (UObject* ConfiguredIcon=Items ? nullptr : TableThumbnail(DisplayTable,DataID)) Entry.Icon=ThumbnailBrush(ConfiguredIcon);
        else if (UImage* RowIcon=Cast<UImage>(NamedWidget(Row,Items ? TEXT("ItemImage") : TEXT("SkillImage"))))
            Entry.Icon=RowIcon->Brush;
        if (!CopiedFonts)
        {
            UTextBlock* NameText=Cast<UTextBlock>(NamedWidget(Row,Items ? TEXT("ItemName") : TEXT("SkillName")));
            if (NameText)
            {
                HUD->CopyLegacyListFonts(NameText,Cast<UTextBlock>(NamedWidget(Row,Items ? TEXT("AmountText") : TEXT("MPAmount"))),
                    Cast<UTextBlock>(NamedWidget(NativeMenuSource,Items ? TEXT("ItemDescriptionText") : TEXT("SkillDescriptionText"))));
                CopiedFonts=true;
            }
        }
        Entry.bEnabled=Row->GetIsEnabled() && (!Items || Quantity>0);
        if (UWidget* Inner=Cast<UWidget>(ObjectValue(Row,Items ? TEXT("ItemButton") : TEXT("SkillButton"))))
            Entry.bEnabled=Entry.bEnabled && Inner->GetIsEnabled();
#if REDITUS_HUD_HAS_ATB
        if (bUsingATB)
            Entry.bEnabled=Entry.bEnabled && CanUseATBListEntry(NativeMenuOwner,NativeMenuCommand,FHUDTableRow(DisplayTable,DataID));
#endif
        Entries.Add(Entry);
    }
    NativeMenuSource->SetVisibility(ESlateVisibility::HitTestInvisible);
    NativeMenuSource->SetRenderOpacity(0.f);
    if (bOpen)
    {
        LastRouteStatus=FString::Printf(TEXT("Opened %s for %s: %d generated rows, %d displayed entries"),
            Items ? TEXT("Items") : NativeMenuCommand==EReditusHUDCommand::Overdrive ? TEXT("Overdrive") : TEXT("Skills"),
            *GetNameSafe(NativeMenuOwner),NativeMenuRows.Num(),Entries.Num());
        if (bLogCommandRouting || Entries.Num()==0) UE_LOG(LogReditusBattleHUD,Log,TEXT("%s"),*LastRouteStatus);
        HUD->ShowListMenu(FText::FromString(Items ? TEXT("ITEMS") : NativeMenuCommand==EReditusHUDCommand::Overdrive ? TEXT("OVERDRIVE") : TEXT("SKILLS")),Entries);
    }
    else HUD->UpdateListEntries(Entries);
}

bool UReditusBattleHUDPresenterComponent::IsBattleMoveUnlocked(AActor* Unit,FName MoveID,bool bOverdrive) const
{
    return ReadSkillAccess(Unit,Player,PartyMemberSkillDefinitions,EquipmentSkillDefinitions,bOverdrive).Allows(MoveID);
}

void UReditusBattleHUDPresenterComponent::ClearNativeMenu()
{
    NativeMenuSource=nullptr;
    NativeMenuOwner=nullptr;
    PendingMenuOwner=nullptr;
    NativeMenuRows.Reset();
    NativeMenuIDs.Reset();
    NativeMenuDataIDs.Reset();
    bAwaitingNativeMenu=false;
    PendingOriginalSelection=-1;
}

void UReditusBattleHUDPresenterComponent::ClearATBListTargeting()
{
    TArray<TWeakObjectPtr<AActor>> Icons=MoveTemp(DraftIcons);
    DraftIcons.Reset();
    DraftOwner.Reset();
    DraftActionID=NAME_None;
    DraftCycle=INDEX_NONE;
    bDraftActive=false;
    bDraftGroup=false;
    for (const auto& Icon:Icons) SetTargetIcon(Icon.Get(),false);
}

bool UReditusBattleHUDPresenterComponent::BeginATBListTargeting(AActor* Unit,FName ID,EReditusHUDCommand Command)
{
#if REDITUS_HUD_HAS_ATB
    const auto* ATB=RunningATB(GetOwner());
    if (!ATB || CommandOwner(GetOwner())!=Unit || Boolean(Unit,TEXT("isInputBlocked"))) return false;
    const FHUDTableRow Row(Command==EReditusHUDCommand::Items ? ItemDescriptions : SkillDescriptions,ID);
    if (!CanUseATBListEntry(Unit,Command,Row)) return false;
    const auto Targets=ListTargets(GetOwner(),Unit,Command,Row,DisplaySlots);
    if (Targets.Units.Num()==0 || !SetObjectValue(Unit,TEXT("currentTarget"),Targets.Units[0]) ||
        !SetSelectionValue(Unit,1))
    {
        if (bLogCommandRouting) UE_LOG(LogReditusBattleHUD,Log,TEXT("No valid battle targets for %s"),*ID.ToString());
        return false;
    }

    ClearATBListTargeting();
    bDraftActive=true;
    bDraftGroup=Targets.bGroup;
    bExplicitTarget=false;
    DraftOwner=Unit;
    DraftCommand=Command;
    DraftActionID=ID;
    DraftCycle=ATB->GetUnitState(Unit).CycleToken;
    SetBoolValue(Unit,TEXT("TargetSet"),true);
    const int32 IconCount=Targets.bGroup ? Targets.Units.Num() : 1;
    for (int32 Index=0; Index<IconCount; ++Index)
    {
        DraftIcons.Emplace(Targets.Units[Index]);
        SetTargetIcon(Targets.Units[Index],true);
    }
    ClearNativeMenu();
    HUD->CloseListMenu();
    RefreshBattleHUD();
    return true;
#else
    return false;
#endif
}

bool UReditusBattleHUDPresenterComponent::RouteATBListTargetInput(FName ActionName)
{
#if REDITUS_HUD_HAS_ATB
    if (!bDraftActive) return false;
    const bool bCancel=ActionName==TEXT("MenuCancel");
    const bool bConfirm=ActionName==TEXT("MenuConfirm");
    const bool bBackward=ActionName==TEXT("MenuLeft") || ActionName==TEXT("MenuUp");
    const bool bForward=ActionName==TEXT("MenuRight") || ActionName==TEXT("MenuDown");
    if (!bCancel && !bConfirm && !bBackward && !bForward) return false;

    AActor* Unit=DraftOwner.Get();
    auto* ATB=RunningATB(GetOwner());
    if (!ATB || !Unit || CommandOwner(GetOwner())!=Unit || ATB->GetUnitState(Unit).CycleToken!=DraftCycle)
    {
        ClearATBListTargeting();
        return true;
    }
    if (bCancel)
    {
        ClearATBListTargeting();
        SetObjectValue(Unit,TEXT("currentTarget"),nullptr);
        RestoreATBListCommands(Unit);
        return true;
    }
    if (Boolean(Unit,TEXT("isInputBlocked"))) return true;

    const bool bItem=DraftCommand==EReditusHUDCommand::Items;
    const FHUDTableRow Row(bItem ? ItemDescriptions : SkillDescriptions,DraftActionID);
    const auto Targets=ListTargets(GetOwner(),Unit,DraftCommand,Row,DisplaySlots);
    if (Targets.Units.Num()==0)
    {
        ClearATBListTargeting();
        SetObjectValue(Unit,TEXT("currentTarget"),nullptr);
        RestoreATBListCommands(Unit);
        return true;
    }
    AActor* Target=Cast<AActor>(ObjectValue(Unit,TEXT("currentTarget")));
    const int32 OldIndex=Targets.Units.IndexOfByKey(Target);
    if (!Targets.bGroup && (!bConfirm || OldIndex==INDEX_NONE))
    {
        const int32 Next=OldIndex==INDEX_NONE ? 0 :
            (OldIndex+Targets.Units.Num()+(bBackward ? -1 : 1)) % Targets.Units.Num();
        AActor* NewTarget=Targets.Units[Next];
        if (DraftIcons.Num()!=1 || DraftIcons[0].Get()!=NewTarget)
        {
            for (const auto& Icon:DraftIcons) SetTargetIcon(Icon.Get(),false);
            DraftIcons.Reset();
            DraftIcons.Emplace(NewTarget);
            SetTargetIcon(NewTarget,true);
        }
        SetObjectValue(Unit,TEXT("currentTarget"),NewTarget);
        return true;
    }
    if (!bConfirm) return true;
    if (!CanUseATBListEntry(Unit,DraftCommand,Row) || (bItem && LiveItemCount(Player,DraftActionID,1)<=0) ||
        (!bItem && !IsBattleMoveUnlocked(Unit,DraftActionID,DraftCommand==EReditusHUDCommand::Overdrive)))
    {
        ClearATBListTargeting();
        SetObjectValue(Unit,TEXT("currentTarget"),nullptr);
        RestoreATBListCommands(Unit);
        return true;
    }
    FReditusATBAction Action;
    Action.Unit=Unit;
    Action.ActionID=DraftActionID;
    Action.Kind=bItem ? EReditusATBActionKind::Item :
        DraftCommand==EReditusHUDCommand::Overdrive ? EReditusATBActionKind::Overdrive : EReditusATBActionKind::Skill;
    Action.LegacyClass=Row.Class(bItem ? TEXT("UsableItemLegacyLaunch") : TEXT("LegacySkill"));
    if (Targets.bGroup) Action.Targets=Targets.Units;
    else Action.Targets.Add(Target);
    Action.AmpCommitted=FMath::Max(0,static_cast<int32>(Number(Unit,TEXT("CurrentAmpPointsInPlay"))));
    int32 ActionToken=INDEX_NONE;
    if (ATB->SubmitAction(Action,ActionToken))
    {
        ClearATBListTargeting();
        SetSelectionValue(Unit,0);
        SetBoolValue(Unit,TEXT("Confirmed"),true);
        SetBoolValue(Unit,TEXT("TargetSet"),false);
        if (auto* FX=Cast<USceneComponent>(ObjectValue(Unit,TEXT("PS_GPS_Selection_02")))) FX->SetVisibility(false);
        CallNoArgumentEvent(GetOwner(),TEXT("PickNextATBCommandUnit"));
        if (bLogCommandRouting) UE_LOG(LogReditusBattleHUD,Log,TEXT("Queued %s for %s (token %d)"),
            *Action.ActionID.ToString(),*GetNameSafe(Unit),ActionToken);
    }
    else
        UE_LOG(LogReditusBattleHUD,Warning,TEXT("ATB rejected %s for %s before queueing"),
            *Action.ActionID.ToString(),*GetNameSafe(Unit));
    return true;
#else
    return false;
#endif
}

void UReditusBattleHUDPresenterComponent::HandleNativeMenuChosen(AActor* Unit,FName EntryID)
{
    if (!NativeMenuSource || !HUD || Unit!=NativeMenuOwner || !IsValid(Unit) || Number(Unit,TEXT("currentHP"))<=0.0 ||
        CommandOwner(GetOwner())!=Unit) return;
    const int32 Index=NativeMenuIDs.IndexOfByKey(EntryID);
    if (!NativeMenuRows.IsValidIndex(Index) || !IsValid(NativeMenuRows[Index]) || !NativeMenuRows[Index]->GetIsEnabled()) return;
    const bool Items=NativeMenuCommand==EReditusHUDCommand::Items;
    const TArray<UUserWidget*> CurrentRows=WidgetArray(NativeMenuSource,Items ? TEXT("itemUIs") : TEXT("skillUIs"));
    const int32 CurrentIndex=CurrentRows.IndexOfByKey(NativeMenuRows[Index]);
    UUserWidget* Row=NativeMenuRows[Index];
    if (CurrentIndex==INDEX_NONE || !NativeMenuDataIDs.IsValidIndex(Index) ||
        NameValue(Row,Items ? TEXT("item") : TEXT("skill"))!=NativeMenuDataIDs[Index] ||
        (Items && Number(Row,TEXT("amount"))<=0.0) ||
        (!Items && !IsBattleMoveUnlocked(Unit,NativeMenuDataIDs[Index],NativeMenuCommand==EReditusHUDCommand::Overdrive)))
    {
        RefreshNativeMenuEntries(false);
        return;
    }
#if REDITUS_HUD_HAS_ATB
    if (RunningATB(GetOwner()))
    {
        if (!Items || LiveItemCount(Player,NativeMenuDataIDs[Index],1)>0)
            BeginATBListTargeting(Unit,NativeMenuDataIDs[Index],NativeMenuCommand);
        return;
    }
#endif
    FIntProperty* Selection=CastField<FIntProperty>(Property(NativeMenuSource,Items ? TEXT("selectedItemIndex") : TEXT("selectedSkillIndex")));
    if (!Selection) return;
    Selection->SetPropertyValue_InContainer(NativeMenuSource,CurrentIndex);
    if (!CallNoArgumentEvent(NativeMenuSource,Items ? TEXT("HighlightSelectedItem") : TEXT("HighlightSelectedSkill"))) return;
    UWidget* Confirm=Cast<UWidget>(ObjectValue(NativeMenuSource,TEXT("ConfirmButton")));
    if (Confirm && !Confirm->GetIsEnabled()) return;
    UUserWidget* Source=NativeMenuSource;
    UFunction* ConfirmEvent=Source->FindFunction(TEXT("Confirm"));
    if (!ConfirmEvent || ConfirmEvent->NumParms!=0) return;
    ClearNativeMenu();
    HUD->CloseListMenu();
    CallNoArgumentEvent(Source,TEXT("Confirm"));
    RefreshBattleHUD();
}

void UReditusBattleHUDPresenterComponent::HandleNativeMenuCancelled(AActor* Unit)
{
    if (!HUD) return;
#if REDITUS_HUD_HAS_ATB
    if (RunningATB(GetOwner()))
    {
        if (!Unit || (Unit!=NativeMenuOwner && Unit!=PendingMenuOwner)) return;
        const bool bRestore=CommandOwner(GetOwner())==Unit && Number(Unit,TEXT("currentHP"))>0.0;
        ClearNativeMenu();
        if (bRestore) RestoreATBListCommands(Unit);
        RefreshBattleHUD();
        return;
    }
#endif
    if (!NativeMenuSource)
    {
        if (!PendingMenuOwner || Unit!=PendingMenuOwner) return;
        const bool CanRestore=ReditusHUDInputPolicy::CanRollbackPendingMenu(CommandOwner(GetOwner())==Unit,
            Number(Unit,TEXT("currentHP"))>0.0,Boolean(Unit,TEXT("isInputBlocked")),PendingOriginalSelection,
            static_cast<int32>(Number(Unit,TEXT("SelectType"),-1.0)));
        const int32 PreviousSelection=PendingOriginalSelection;
        ClearNativeMenu();
        if (CanRestore && SetSelectionValue(Unit,PreviousSelection))
        {
            CallNoArgumentEvent(Unit,TEXT("ResetInput"));
            CallNoArgumentEvent(Unit,TEXT("AttackCancelled"));
        }
        RefreshBattleHUD();
        return;
    }
    if (Unit!=NativeMenuOwner) return;
    UUserWidget* Source=NativeMenuSource;
    const bool CanCancel=ReditusHUDInputPolicy::CanCancelOwnedMenu(CommandOwner(GetOwner())==Unit,
        IsValid(Unit) && Number(Unit,TEXT("currentHP"))>0.0,Boolean(Unit,TEXT("isInputBlocked")));
    ClearNativeMenu();
    if (CanCancel) CallNoArgumentEvent(Source,TEXT("Cancel"));
    RefreshBattleHUD();
}

void UReditusBattleHUDPresenterComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
    if (!IsValid(HUD))
    {
        ClearEnemyMenuSlowdown();
        OpenLegacyMenuWindows.Reset();
        return;
    }
    bool bRefresh=bReadLegacyBattle;
#if REDITUS_HUD_HAS_ATB
    bRefresh=bRefresh || RunningATB(GetOwner())!=nullptr;
#endif
    if (bRefresh) RefreshBattleHUD();
}

void UReditusBattleHUDPresenterComponent::ClearEnemyMenuSlowdown()
{
#if REDITUS_HUD_HAS_ATB
    if (MenuSlowedEnemies.Num()==0) return;
    if (auto* ATB=RunningATB(GetOwner()))
        for (const auto& Enemy:MenuSlowedEnemies)
            if (AActor* Unit=Enemy.Get()) ATB->RemoveTimingEffect(Unit,MenuSlowdownSource);
    MenuSlowedEnemies.Reset();
#endif
}

void UReditusBattleHUDPresenterComponent::UpdateEnemyMenuSlowdown(UObject* Battle,AActor* CommandUnit)
{
#if REDITUS_HUD_HAS_ATB
    auto* ATB=RunningATB(GetOwner());
    if (!ATB || !IsValid(HUD))
    {
        ClearEnemyMenuSlowdown();
        OpenLegacyMenuWindows.Reset();
        return;
    }

    // Legacy menus animate their content and can stay attached even after closing.
    // The native list's transparent backing widget never counts as a second window.
    UObject* Roots[]={ObjectValue(GetOwner(),TEXT("battleUI")),ObjectValue(CommandUnit,TEXT("battleUI"))};
    static const FName AnimatedMenus[]={TEXT("BP_SkillUseDialogue"),TEXT("BP_ItemUseDialogue"),
        TEXT("BP_OverdriveDialogue"),TEXT("BP_ChangeMemberDialogue")};
    TSet<TWeakObjectPtr<UUserWidget>> VisibleMenus;
    for (const FName MenuName:AnimatedMenus)
        for (UObject* Root:Roots)
        {
            UUserWidget* Menu=ChildMenu(Root,MenuName);
            if (!IsValid(Menu) || !Menu->IsVisible() || Menu->GetRenderOpacity()<=0.f) continue;
            auto* Animation=Cast<UWidgetAnimation>(ObjectValue(Menu,MenuName==TEXT("BP_ItemUseDialogue") ?
                TEXT("ShowItemInventory") : TEXT("ShowSkillsAnim")));
            const TWeakObjectPtr<UUserWidget> Key(Menu);
            const bool bOpen=Animation && Menu->IsAnimationPlaying(Animation) ?
                Menu->IsAnimationPlayingForward(Animation) : OpenLegacyMenuWindows.Contains(Key);
            if (bOpen) VisibleMenus.Add(Key);
        }
    OpenLegacyMenuWindows=MoveTemp(VisibleMenus);

    auto* FleeConfirmation=Cast<UUserWidget>(ObjectValue(CommandUnit,TEXT("BP_YesNoForBattle")));
    const bool bFleeConfirmationOpen=IsValid(FleeConfirmation) && FleeConfirmation->IsInViewport() && FleeConfirmation->IsVisible();
    // The list closes before targeting begins. Keep the same modifier through both states.
    if (!HUD->IsMenuOpen() && !HUD->IsTargetSelectionActive() &&
        OpenLegacyMenuWindows.Num()==0 && !bFleeConfirmationOpen)
    {
        ClearEnemyMenuSlowdown();
        return;
    }

    const float Percent=FMath::IsFinite(EnemyMenuSlowdownPercent) ?
        FMath::Clamp(EnemyMenuSlowdownPercent,50.f,100.f) : 50.f;
    const float Rate=1.f-Percent/100.f;
    FReditusATBTimingEffect Slowdown;
    Slowdown.SourceID=MenuSlowdownSource;
    Slowdown.GaugeMultiplier=Rate;
    Slowdown.PreparationMultiplier=Rate;
    TSet<TWeakObjectPtr<AActor>> Active;
    for (AActor* Enemy:Actors(Battle,TEXT("enemyUnits")))
    {
        if (ATB->GetUnitState(Enemy).Phase==EReditusATBPhase::Inactive) continue;
        const bool bAlreadyApplied=ATB->GetTimingEffects(Enemy).ContainsByPredicate([Rate](const FReditusATBTimingEffect& Effect)
        {
            return Effect.SourceID==MenuSlowdownSource && Effect.GaugeMultiplier==Rate && Effect.PreparationMultiplier==Rate;
        });
        if (bAlreadyApplied || ATB->AddOrUpdateTimingEffect(Enemy,Slowdown))
            Active.Add(TWeakObjectPtr<AActor>(Enemy));
    }
    for (const auto& Previous:MenuSlowedEnemies)
        if (!Active.Contains(Previous))
            if (AActor* Unit=Previous.Get()) ATB->RemoveTimingEffect(Unit,MenuSlowdownSource);
    MenuSlowedEnemies=MoveTemp(Active);
#endif
}

void UReditusBattleHUDPresenterComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DestroyBattleHUD();
    Super::EndPlay(EndPlayReason);
}

void UReditusBattleHUDPresenterComponent::SetPartyUnits(const TArray<AActor*>& Units)
{
    bExplicitParty=true;
    PartyUnits=Units;
    RefreshBattleHUD();
}

void UReditusBattleHUDPresenterComponent::SetUnitReadiness(AActor* Unit,float Percent,bool bReady,bool bCanAct,bool bQueued)
{
    if (!IsValid(Unit)) return;
    FReditusHUDReadinessOverride& Value=Readiness.FindOrAdd(Unit);
    Value.Percent=ReditusHUDPolicy::Ratio(Percent,1.f);
    Value.bReady=bReady;
    Value.bCanAct=bCanAct;
    Value.bQueued=bQueued;
    RefreshBattleHUD();
}

void UReditusBattleHUDPresenterComponent::ClearReadinessOverrides() { Readiness.Reset(); RefreshBattleHUD(); }
void UReditusBattleHUDPresenterComponent::SetCommandWindowEnabled(bool bEnabled)
{
    bExplicitCommandWindow=true;
    bCommandWindowEnabled=bEnabled;
    RefreshBattleHUD();
}
void UReditusBattleHUDPresenterComponent::FollowLegacyCommandWindow() { bExplicitCommandWindow=false; RefreshBattleHUD(); }

FReditusBattleHUDUnit UReditusBattleHUDPresenterComponent::ReadUnit(AActor* Unit,AActor* CurrentActor,bool bCommandWindow) const
{
    FReditusBattleHUDUnit Data;
    Data.Unit=Unit;
    if (!IsValid(Unit)) return Data;
    Data.CharacterID=NameValue(Unit,TEXT("CharacterID"));
    Data.DisplayName=Title(Unit);
    if (Data.DisplayName.IsEmpty()) Data.DisplayName=FText::FromName(Data.CharacterID);
    Data.Portrait=Thumbnail(Unit,TEXT("battleImage"));
    if (!Data.Portrait) Data.Portrait=Thumbnail(Unit,TEXT("thumbnail"));
    Data.HP=static_cast<float>(Number(Unit,TEXT("currentHP")));
    Data.MaxHP=static_cast<float>(Number(Unit,TEXT("maxHP")));
    Data.MP=static_cast<float>(Number(Unit,TEXT("currentMP")));
    Data.MaxMP=static_cast<float>(Number(Unit,TEXT("maxMP")));
    Data.Overdrive=static_cast<float>(Number(Unit,TEXT("CurrentOverDrivePoints")));
    Data.MaxOverdrive=static_cast<float>(Number(Unit,TEXT("MaxOverdrivePoints")));
    Data.bCanOverdrive=Boolean(Unit,TEXT("CanEnterOverdrive"));
    Data.bOverdriveEntered=Boolean(Unit,TEXT("OverdriveEntered"));
    Data.bCanHack=Data.CharacterID==HackCharacterID;
    Data.TotalAmp=static_cast<int32>(Number(Unit,TEXT("TotalAmpPoints")));
    Data.CommittedAmp=static_cast<int32>(Number(Unit,TEXT("CurrentAmpPointsInPlay")));
    Data.CurrentStamina=static_cast<int32>(Number(Unit,TEXT("CurrentStamina")));
#if REDITUS_HUD_HAS_ATB
    if (const auto* ATB=RunningATB(GetOwner()))
    {
        const FReditusATBUnitView State=ATB->GetUnitState(Unit);
        Data.ATBPercent=ReditusHUDPolicy::Ratio(State.Gauge,1.f);
        Data.bCanAct=Data.HP>0.f && State.Phase!=EReditusATBPhase::Inactive;
        Data.bReadyForCommands=Data.bCanAct && State.Phase==EReditusATBPhase::Ready &&
            Unit==CurrentActor && bCommandWindow;
        Data.bQueued=State.Phase==EReditusATBPhase::Preparing || State.Phase==EReditusATBPhase::Queued;
        return Data;
    }
#endif
    Data.ATBPercent=ReditusHUDPolicy::Ratio(static_cast<float>(Number(Unit,TEXT("currentActionTime"))),
        static_cast<float>(Number(Unit,LegacyATBThresholdProperty)));
    Data.bReadyForCommands=Unit==CurrentActor && bCommandWindow;
    Data.bCanAct=Data.HP>0.f;
    if (Data.bReadyForCommands) Data.ATBPercent=1.f;
    if (const FReditusHUDReadinessOverride* Override=Readiness.Find(Unit))
    {
        Data.ATBPercent=Override->Percent;
        Data.bReadyForCommands=Override->bReady;
        Data.bCanAct=Override->bCanAct && Data.HP>0.f;
        Data.bQueued=Override->bQueued;
    }
    return Data;
}

void UReditusBattleHUDPresenterComponent::UpdateDisplaySlots(UObject* Battle,AActor* CurrentActor,bool bCommandWindow)
{
    DisplaySlots.SetNum(ReditusHUDPolicy::MaxActiveUnits);
    const TSet<FName> DeadIDs=DeadCharacterIDs(Battle);
    UObject* RosterOwner=ObjectValue(Battle,TEXT("jRPGPlayerController"));
    if (!RosterOwner) RosterOwner=Player;
    const TArray<FName> ActiveIDs=bExplicitParty ? TArray<FName>() : Names(RosterOwner,TEXT("ActivePartyCharacterIDs"));
    TArray<FReditusBattleHUDUnit> Incoming;
    for (AActor* Unit : PartyUnits)
    {
        FReditusBattleHUDUnit Data=ReadUnit(Unit,CurrentActor,bCommandWindow);
        if (!IsValid(Unit) || Incoming.ContainsByPredicate([&Data](const FReditusBattleHUDUnit& Other) {
            return Other.Unit==Data.Unit || (!Data.CharacterID.IsNone() && Other.CharacterID==Data.CharacterID);
        })) continue;
        Incoming.Add(Data);
        if (Incoming.Num()==ReditusHUDPolicy::MaxActiveUnits) break;
    }
    const auto SameCharacter=[](const FReditusBattleHUDUnit& First,const FReditusBattleHUDUnit& Second) {
        return (!First.CharacterID.IsNone() && First.CharacterID==Second.CharacterID) ||
            (First.Unit!=nullptr && First.Unit==Second.Unit);
    };
    const bool HasNewMember=Incoming.ContainsByPredicate([this,&SameCharacter](const FReditusBattleHUDUnit& Data) {
        return !DisplaySlots.ContainsByPredicate([&Data,&SameCharacter](const FReditusBattleHUDUnit& Old) { return SameCharacter(Old,Data); });
    });
    bool Matches[ReditusHUDPolicy::MaxActiveUnits][ReditusHUDPolicy::MaxActiveUnits] = {};
    bool Occupied[ReditusHUDPolicy::MaxActiveUnits] = {};
    bool Defeated[ReditusHUDPolicy::MaxActiveUnits] = {};
    for (int32 I=0; I<DisplaySlots.Num(); ++I)
    {
        const FReditusBattleHUDUnit& Old=DisplaySlots[I];
        Occupied[I]=Old.HasCharacter();
        const bool KnownDefeated=Old.HP<=0.f || (!Old.CharacterID.IsNone() && DeadIDs.Contains(Old.CharacterID)) ||
            (IsValid(Old.Unit) && (Number(Old.Unit,TEXT("currentHP"))<=0.0 || Boolean(Old.Unit,TEXT("IsDead?"))));
        const bool RemovedFromRoster=!Old.CharacterID.IsNone() && ActiveIDs.Num()>0 && !ActiveIDs.Contains(Old.CharacterID);
        // Permanent-death paths can destroy/remove an actor between polls,
        // without using the revivable-death map. Keep its cached KO card too.
        // A real new roster member can still replace the outgoing position.
        Defeated[I]=ReditusHUDPolicy::KeepMissingCard(Occupied[I],KnownDefeated,IsValid(Old.Unit),HasNewMember,RemovedFromRoster);
        for (int32 J=0; J<Incoming.Num(); ++J)
            Matches[I][J]=Occupied[I] && SameCharacter(Old,Incoming[J]);
    }
    const ReditusHUDPolicy::PartySlotPlan Plan=ReditusHUDPolicy::PlanPartySlots(Incoming.Num(),Matches,Occupied,Defeated);
    for (int32 I=0; I<DisplaySlots.Num(); ++I)
    {
        if (Incoming.IsValidIndex(Plan.Incoming[I])) DisplaySlots[I]=Incoming[Plan.Incoming[I]];
        else if (Plan.KeepDefeated[I])
        {
            FReditusBattleHUDUnit& Data=DisplaySlots[I];
            if (IsValid(Data.Unit)) Data=ReadUnit(Data.Unit,CurrentActor,false);
            else Data.Unit=nullptr;
            Data.HP=0.f;
            Data.ATBPercent=0.f;
            Data.bReadyForCommands=false;
            Data.bCanAct=false;
            Data.bQueued=false;
            Data.bOverdriveEntered=false;
        }
        else DisplaySlots[I]=FReditusBattleHUDUnit();
    }
}

void UReditusBattleHUDPresenterComponent::RestoreLegacyEnemyComponents()
{
    for (const auto& Entry:HiddenLegacyEnemyComponents)
        if (USceneComponent* Component=Entry.Key.Get()) Component->SetHiddenInGame(Entry.Value);
    HiddenLegacyEnemyComponents.Reset();
}

void UReditusBattleHUDPresenterComponent::UpdateEnemyBadges(UObject* Battle,AActor* Target,bool bGroup)
{
    TArray<FReditusEnemyHUDView> Views;
    if (!bShowEnemyStatusBadges || !IsValid(Battle))
    {
        RestoreLegacyEnemyComponents();
        HUD->SetEnemyStatusViews(Views);
        return;
    }
    for (auto It=HiddenLegacyEnemyComponents.CreateIterator(); It; ++It)
        if (!It.Key().IsValid()) It.RemoveCurrent();
    for (AActor* Enemy:Actors(Battle,TEXT("enemyUnits")))
    {
        // Keep the old Blueprint widgets and selection events alive, but replace their visuals.
        static const FName LegacyFields[]={TEXT("EnemyUnitWidget"),TEXT("TargetIconWidget"),TEXT("ParticleSystem")};
        for (FName Field:LegacyFields)
            if (auto* Component=Cast<USceneComponent>(ObjectValue(Enemy,Field)))
            {
                const TWeakObjectPtr<USceneComponent> Key(Component);
                const bool bHidden=Boolean(Component,TEXT("bHiddenInGame"));
                if (!HiddenLegacyEnemyComponents.Contains(Key))
                    HiddenLegacyEnemyComponents.Add(Key,bHidden);
                if (!bHidden) Component->SetHiddenInGame(true);
            }
        if (Number(Enemy,TEXT("currentHP"))<=0 || Boolean(Enemy,TEXT("IsDead?"))) continue;
        FReditusEnemyHUDView& View=Views.AddDefaulted_GetRef();
        View.Unit=Enemy;
        View.Sprite=Cast<UPrimitiveComponent>(ObjectValue(Enemy,TEXT("Pixel2D")));
        const auto IsRenderVisible=[](UPrimitiveComponent* Component)
        {
            return IsValid(Component) && Component->IsVisible() && !Boolean(Component,TEXT("bHiddenInGame"));
        };
        if (!IsRenderVisible(View.Sprite))
            View.Sprite=Cast<UPrimitiveComponent>(ObjectValue(Enemy,TEXT("SkeletalMesh")));
        if (!IsRenderVisible(View.Sprite)) View.Sprite=nullptr;
        View.FallbackAnchor=Cast<USceneComponent>(ObjectValue(Enemy,TEXT("EnemyUnitWidget")));
        View.MaxStamina=FMath::Max(0,static_cast<int32>(Number(Enemy,TEXT("MaxStamina"))));
        View.Stamina=FMath::Clamp(static_cast<int32>(Number(Enemy,TEXT("CurrentStamina"))),0,View.MaxStamina);
        View.bTargeted=Enemy==Target || (bGroup && DraftIcons.Contains(TWeakObjectPtr<AActor>(Enemy)));
        ReadEnemyEffects(Enemy,View.Effects);
    }
    HUD->SetEnemyStatusViews(Views);
}

bool UReditusBattleHUDPresenterComponent::ShowExecutingEnemyAction(UObject* Battle)
{
#if REDITUS_HUD_HAS_ATB
    const auto* ATB=RunningATB(GetOwner());
    if (!ATB || !IsValid(HUD) || !IsValid(Battle)) return false;

    FReditusATBAction Action;
    AActor* Enemy=nullptr;
    for (AActor* Unit:Actors(Battle,TEXT("enemyUnits")))
    {
        if (ATB->GetUnitState(Unit).Phase!=EReditusATBPhase::Executing || !ATB->GetPendingAction(Unit,Action)) continue;
        Enemy=Unit;
        break;
    }
    if (!Enemy || (Action.Kind!=EReditusATBActionKind::Attack && Action.Kind!=EReditusATBActionKind::Skill)) return false;

    FText Label;
    UObject* Icon=nullptr;
    int32 Element=INDEX_NONE;
    if (Action.Kind==EReditusATBActionKind::Attack)
    {
        Label=NSLOCTEXT("ReditusHUD","EnemyRegularAttack","Attack");
        Element=static_cast<int32>(Number(Enemy,TEXT("AttackElement"),INDEX_NONE));
    }
    else
    {
        if (!Action.ActionID.IsNone())
        {
            if (!IsValid(SkillDescriptions) && GetWorld() && GetWorld()->IsGameWorld())
                SkillDescriptions=LoadObject<UDataTable>(nullptr,TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_Skills.DT_Skills"));
            Label=TableText(SkillDescriptions,Action.ActionID,TEXT("DisplayName"));
            Icon=TableThumbnail(SkillDescriptions,Action.ActionID);
            Element=FHUDTableRow(SkillDescriptions,Action.ActionID).Int(TEXT("AttackElement"));
        }
        if (UClass* SkillClass=Action.LegacyClass.Get())
        {
            UObject* Defaults=SkillClass->GetDefaultObject();
            if (Label.IsEmpty())
                if (const auto* Name=CastField<FTextProperty>(Property(Defaults,TEXT("name"))))
                    Label=Name->GetPropertyValue_InContainer(Defaults);
            if (Element==INDEX_NONE)
                Element=static_cast<int32>(Number(Defaults,TEXT("Attack Element"),INDEX_NONE));
        }
        if (Label.IsEmpty())
            Label=Action.ActionID.IsNone() ? NSLOCTEXT("ReditusHUD","EnemySkill","Skill") : FText::FromName(Action.ActionID);
    }

    // EReditusElementAffinity order and the original BP_ShowAttackElement textures.
    // "All" has no dedicated icon in the legacy HUD.
    if (!Icon && Element>=0 && Element<9)
    {
        static const TCHAR* ElementNames[]={TEXT("Phys"),TEXT("Water"),TEXT("Earth"),TEXT("Fire"),
            TEXT("Wind"),TEXT("Dark"),TEXT("Holy"),TEXT("Spirit"),TEXT("Lightning")};
        if (UTexture2D* const* Cached=ActionElementTextures.Find(Element)) Icon=*Cached;
        else
        {
            const FString Path=Element==0 ? TEXT("/Game/TurnBasedJRPGTemplate/Textures/T_PhysicalAttack.T_PhysicalAttack") :
                FString::Printf(TEXT("/Game/MapSpriteAssets/UI_ICONS/ElementalIcons/T_ElementalIcons_%s.T_ElementalIcons_%s"),
                    ElementNames[Element],ElementNames[Element]);
            UTexture2D* Texture=LoadObject<UTexture2D>(nullptr,*Path);
            ActionElementTextures.Add(Element,Texture);
            Icon=Texture;
        }
    }
    HUD->SetActionAnnouncement(Label,ThumbnailBrush(Icon),true);
    return true;
#else
    return false;
#endif
}

void UReditusBattleHUDPresenterComponent::RefreshBattleHUD()
{
    if (!IsValid(HUD) || bRefreshing) return;
    TGuardValue<bool> Guard(bRefreshing,true);
    AActor* Controller=GetOwner();
    if (Boolean(Controller,TEXT("isBattleOver"))) { DestroyBattleHUD(); return; }
    UObject* Battle=ObjectValue(Controller,TEXT("currentBattle"));
    // Enemy skill preparation also calls battleUI before any list has opened.
    // Keep the backing root available from HUD creation's first game-world refresh.
    if (bReadLegacyBattle && bRouteLegacyCommands && IsValid(Battle))
        EnsureLegacyMenuRoot(Cast<AActor>(ObjectValue(Controller,TEXT("currentAttackingUnit"))));
    if (!bExplicitParty)
    {
        PartyUnits=Actors(Battle,TEXT("playerUnits"));
    }
    PartyUnits.RemoveAll([](AActor* Actor){return !IsValid(Actor);});
    if (PartyUnits.Num()>ReditusHUDPolicy::MaxActiveUnits)
    {
        if (!bWarnedPartyOverflow)
            UE_LOG(LogReditusBattleHUD,Warning,TEXT("Battle HUD received %d active player units; the supported maximum is four. Pass active combatants, with reserves in the Change Party list."),PartyUnits.Num());
        bWarnedPartyOverflow=true;
        PartyUnits.SetNum(ReditusHUDPolicy::MaxActiveUnits);
    }
    else bWarnedPartyOverflow=false;
    for (auto It=Readiness.CreateIterator(); It; ++It) if (!IsValid(It.Key())) It.RemoveCurrent();
    AActor* ExecutingActor=Cast<AActor>(ObjectValue(Controller,TEXT("currentAttackingUnit")));
    bool bUsingATB=false;
    AActor* CurrentActor=CommandOwner(Controller,&bUsingATB);
#if REDITUS_HUD_HAS_ATB
    if (bDraftActive && (!bUsingATB || !IsValid(CurrentActor) || CurrentActor!=DraftOwner.Get() ||
        Number(CurrentActor,TEXT("currentHP"))<=0.0 ||
        RunningATB(Controller)->GetUnitState(CurrentActor).CycleToken!=DraftCycle))
        ClearATBListTargeting();
    // A ready ally can predate the previous owner's submission. Recover the
    // vacant slot through the same Blueprint that resets input and shows FX.
    if (bUsingATB && !PartyUnits.Contains(CurrentActor))
    {
        if (const auto* ATB=RunningATB(Controller))
        {
            const bool bHasReadyAlly=PartyUnits.ContainsByPredicate([ATB](AActor* Unit)
            {
                return IsValid(Unit) && Number(Unit,TEXT("currentHP"))>0.0 &&
                    ATB->GetUnitState(Unit).Phase==EReditusATBPhase::Ready;
            });
            if (bHasReadyAlly && CallNoArgumentEvent(Controller,TEXT("PickNextATBCommandUnit")))
            {
                if (!IsValid(HUD) || !IsValid(Controller)) return;
                CurrentActor=CommandOwner(Controller,&bUsingATB);
            }
        }
    }
#endif
    const bool IsPartyActor=PartyUnits.Contains(CurrentActor);
    const bool OwnsLegacyCommand=bUsingATB || !Property(Controller,TEXT("CurrentCommandUnit")) || ObjectValue(Controller,TEXT("CurrentCommandUnit"))==CurrentActor;
    const int32 LegacySelection=static_cast<int32>(Number(CurrentActor,TEXT("SelectType"),-1.0));
    const int32 BattleMode=static_cast<int32>(Number(Controller,TEXT("BattleInputMode"),-1.0));
    // Use the unit's finer selection state as well as the controller mode.
    // The legacy graph can remain in CommandMenu while selecting a target.
    HUD->SetReactionWindow(IsValid(ExecutingActor) && !PartyUnits.Contains(ExecutingActor));
    TryCaptureNativeMenu();
    if (NativeMenuSource && (!IsValid(NativeMenuOwner) || CurrentActor!=NativeMenuOwner || Number(NativeMenuOwner,TEXT("currentHP"))<=0.0))
    {
        // A stale list is presentation cleanup, not the player's Back action.
        // Its legacy Cancel handler can restart the new current unit's turn.
        ClearNativeMenu();
        HUD->CloseListMenu();
    }
    bool CommandWindow=false;
    if (bUsingATB)
        CommandWindow=IsPartyActor && Number(CurrentActor,TEXT("currentHP"))>0.0 &&
            !Boolean(CurrentActor,TEXT("isInputBlocked")) &&
            ReditusHUDInputPolicy::LegacyCommandSurface(true,LegacySelection);
    else if (bExplicitCommandWindow) CommandWindow=bCommandWindowEnabled;
    else
    {
        UObject* OldActions=ObjectValue(Controller,TEXT("BattleActionsUI"));
        const FProperty* Mode=Property(Controller,TEXT("BattleInputMode"));
        if (Mode) CommandWindow=Number(Controller,TEXT("BattleInputMode"))==CommandMenuModeValue;
        else
        {
            CommandWindow=Boolean(OldActions,TEXT("IsShown?"));
        }
        // Specialized Party/Hack/target menus block the original actions UI.
        // Keep their face-button input instead of reopening the native cross.
        CommandWindow=ReditusHUDInputPolicy::LegacyCommandSurface(CommandWindow,LegacySelection) &&
            IsPartyActor && !Boolean(OldActions,TEXT("isUIInputBlocked"));
    }
    if (NativeMenuSource && IsValid(NativeMenuOwner) && CurrentActor==NativeMenuOwner) CommandWindow=true;
    if (IsValid(PendingMenuOwner) && CurrentActor==PendingMenuOwner && HUD->IsNativeListOpen()) CommandWindow=true;
    if (bRouteLegacyCommands && !OwnsLegacyCommand) CommandWindow=false;
    const bool TargetWindow=(bUsingATB || bRouteLegacyCommands || !bExplicitCommandWindow) && !NativeMenuSource && !PendingMenuOwner && !HUD->IsMenuOpen() &&
        ReditusHUDInputPolicy::LegacyTargetSurface(IsPartyActor && OwnsLegacyCommand,Number(CurrentActor,TEXT("currentHP"))>0.0,
            Boolean(CurrentActor,TEXT("isInputBlocked")),LegacySelection,BattleMode);
    if (TargetWindow) CommandWindow=false;
    if (HUD->IsTargetSelectionActive() && !TargetWindow) { bExplicitTarget=false; ExplicitEnemyTarget.Reset(); }
    HUD->SetTargetSelectionActive(TargetWindow);
    UpdateDisplaySlots(Battle,CurrentActor,CommandWindow);
    HUD->SetPartySnapshot(DisplaySlots);
    if (bUsingATB || bFollowLegacyTurnSelection)
        ReditusHUDPolicy::FollowChangedTurn(TWeakObjectPtr<AActor>(CurrentActor),PreviousLegacyUnit,IsPartyActor,
            [this](TWeakObjectPtr<AActor> Candidate) { return HUD->SelectReadyUnit(Candidate.Get()); });
    else PreviousLegacyUnit.Reset();
    HUD->SetCommandsEnabled(CommandWindow);
#if REDITUS_HUD_HAS_ATB
    // Play when this unit actually owns command input, not when another
    // character becomes ready in the background or the player backs out of a list.
    if (bUsingATB && CommandWindow && IsPartyActor && IsValid(HUD) && !HUD->IsMenuOpen() &&
        !HUD->bInputSuspended && !UGameplayStatics::IsGamePaused(this) && HUD->GetSelectedUnit()==CurrentActor)
        if (const auto* ATB=RunningATB(Controller))
        {
            const FReditusATBUnitView State=ATB->GetUnitState(CurrentActor);
            if (State.Phase==EReditusATBPhase::Ready)
            {
                const TWeakObjectPtr<AActor> UnitKey(CurrentActor);
                const int32* LastCycle=LastTurnCueCycles.Find(UnitKey);
                if (!LastCycle || *LastCycle!=State.CycleToken)
                {
                    LastTurnCueCycles.Add(UnitKey,State.CycleToken);
                    HUD->PlayTurnReadyCue();
                }
            }
        }
#endif
    if (NativeMenuSource) RefreshNativeMenuEntries(false);
    UpdateEnemyMenuSlowdown(Battle,CurrentActor);
    AActor* Target=TargetWindow ? (bExplicitTarget ? ExplicitEnemyTarget.Get() :
        Cast<AActor>(ObjectValue(CurrentActor,TEXT("currentTarget")))) : nullptr;
    const bool bGroup=TargetWindow && bDraftActive && bDraftGroup;
    UpdateEnemyBadges(Battle,Target,bGroup);
    if (!bExplicitTarget)
    {
        if (bGroup)
            HUD->SetTargetLabel(NSLOCTEXT("ReditusHUD","GroupTarget","ALL TARGETS"));
        else if (IsValid(Target)) HUD->SetTargetedEnemy(Target);
        else HUD->ClearTargetLabel();
    }
    if (!bExplicitSpeed && IsValid(Player)) HUD->SetDisplayedBattleSpeed(static_cast<float>(Number(Player,TEXT("BattleSpeed"),1.0)));
    const bool bWasShowingEnemyATBAnnouncement=bShowingEnemyATBAnnouncement;
    bShowingEnemyATBAnnouncement=ShowExecutingEnemyAction(Battle);
    if (!bShowingEnemyATBAnnouncement && bReadLegacyBattle && bMirrorLegacyActionAnnouncement)
    {
        UUserWidget* Announcement=ChildMenu(ObjectValue(Controller,TEXT("battleUI")),TEXT("BP_ShowAttackElement"));
        if (IsValid(Announcement))
        {
            const ESlateVisibility SourceVisibility=Announcement->GetVisibility();
            const bool Shown=SourceVisibility!=ESlateVisibility::Hidden && SourceVisibility!=ESlateVisibility::Collapsed;
            FSlateBrush ElementIcon;
            if (UImage* SourceIcon=Cast<UImage>(NamedWidget(Announcement,TEXT("SkillElement"))))
                if (SourceIcon->GetVisibility()!=ESlateVisibility::Hidden && SourceIcon->GetVisibility()!=ESlateVisibility::Collapsed)
                    ElementIcon=SourceIcon->Brush;
            const FText AnnouncementLabel=WidgetText(Announcement,TEXT("SkillName"));
            UObject* ActiveSkill=ObjectValue(Controller,TEXT("currentSkill"));
            FName ActiveSkillID=NameValue(ActiveSkill,TEXT("SkillID"));
            // Player selection already stores its row ID on the acting unit;
            // legacy skill actors do not always receive their own SkillID.
            if (ActiveSkillID.IsNone()) ActiveSkillID=NameValue(ExecutingActor,TEXT("currentSkill"));
            FTextProperty* SkillNameField=CastField<FTextProperty>(Property(ActiveSkill,TEXT("name")));
            // Only override a notice belonging to this active skill. A regular-attack
            // notice must retain its AttackElement-to-texture mapping.
            if (Shown && IsValid(ActiveSkill) && !ActiveSkillID.IsNone() && SkillNameField &&
                AnnouncementLabel.EqualTo(SkillNameField->GetPropertyValue_InContainer(ActiveSkill)))
            {
                if (!IsValid(SkillDescriptions) && GetWorld() && GetWorld()->IsGameWorld())
                    SkillDescriptions=LoadObject<UDataTable>(nullptr,TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_Skills.DT_Skills"));
                if (UObject* ConfiguredIcon=TableThumbnail(SkillDescriptions,ActiveSkillID))
                    ElementIcon=ThumbnailBrush(ConfiguredIcon);
            }
            HUD->SetActionAnnouncement(AnnouncementLabel,ElementIcon,Shown);
        }
        else HUD->ClearActionAnnouncement();
    }
    else if (!bShowingEnemyATBAnnouncement && bWasShowingEnemyATBAnnouncement)
        HUD->ClearActionAnnouncement();
    if (bHideLegacyActionsWidget)
    {
        if (UWidget* Old=Cast<UWidget>(ObjectValue(Controller,TEXT("BattleActionsUI"))))
        {
            const int32 Index=HiddenLegacyWidgets.IndexOfByKey(Old);
            if (Index==INDEX_NONE)
            {
                HiddenLegacyWidgets.Add(Old);
                PreviousLegacyVisibility.Add(Old->GetVisibility());
            }
            Old->SetVisibility(ESlateVisibility::Hidden);
        }
    }
    for (UWidget* Widget : HiddenLegacyWidgets) if (IsValid(Widget)) Widget->SetVisibility(ESlateVisibility::Hidden);
    for (UWidget* Widget : SuppressedMenuWidgets) if (IsValid(Widget)) { Widget->SetVisibility(ESlateVisibility::HitTestInvisible); Widget->SetRenderOpacity(0.f); }
    if (IsValid(HostedLegacyRoot)) { HostedLegacyRoot->SetVisibility(ESlateVisibility::HitTestInvisible); HostedLegacyRoot->SetRenderOpacity(0.f); }
}

void UReditusBattleHUDPresenterComponent::SetTargetedEnemy(AActor* Enemy)
{
    bExplicitTarget=true;
    ExplicitEnemyTarget=Enemy;
    if (HUD) HUD->SetTargetedEnemy(Enemy);
}
void UReditusBattleHUDPresenterComponent::SetTargetGroupLabel(FText Label)
{
    bExplicitTarget=true;
    ExplicitEnemyTarget.Reset();
    if (HUD) HUD->SetTargetLabel(Label);
}
void UReditusBattleHUDPresenterComponent::FollowLegacyTarget() { bExplicitTarget=false; ExplicitEnemyTarget.Reset(); RefreshBattleHUD(); }
void UReditusBattleHUDPresenterComponent::SetDisplayedBattleSpeed(float Multiplier)
{
    bExplicitSpeed=true;
    if (HUD) HUD->SetDisplayedBattleSpeed(Multiplier);
}

void UReditusBattleHUDPresenterComponent::SetLegacyWidgetsToHide(const TArray<UWidget*>& Widgets)
{
    for (int32 I=0; I<HiddenLegacyWidgets.Num(); ++I)
        if (IsValid(HiddenLegacyWidgets[I]) && PreviousLegacyVisibility.IsValidIndex(I))
            HiddenLegacyWidgets[I]->SetVisibility(PreviousLegacyVisibility[I]);
    HiddenLegacyWidgets.Reset(); PreviousLegacyVisibility.Reset();
    for (UWidget* Widget : Widgets)
        if (IsValid(Widget) && Widget!=HUD && !HiddenLegacyWidgets.Contains(Widget))
        {
            HiddenLegacyWidgets.Add(Widget);
            PreviousLegacyVisibility.Add(Widget->GetVisibility());
            Widget->SetVisibility(ESlateVisibility::Hidden);
        }
}

bool UReditusBattleHUDPresenterComponent::HandleCommand(AActor* Unit,EReditusHUDCommand Command)
{
    const ReditusHUDInputPolicy::CommandRoute Route=ReditusHUDInputPolicy::ChooseCommandRoute(bRouteLegacyCommands,OnCommandRequested.IsBound());
    if (Route==ReditusHUDInputPolicy::CommandRoute::Legacy) return ExecuteLegacyCommand(Unit,Command);
    if (Route==ReditusHUDInputPolicy::CommandRoute::Blueprint)
    {
        LastRouteStatus=TEXT("Forwarded to Blueprint OnCommandRequested override");
        if (bLogCommandRouting) UE_LOG(LogReditusBattleHUD,Log,TEXT("%s"),*LastRouteStatus);
        OnCommandRequested.Broadcast(Unit,Command);
        return true;
    }
    LastRouteStatus=TEXT("Route Legacy Commands is disabled and OnCommandRequested has no Blueprint handler");
    if (bLogCommandRouting) UE_LOG(LogReditusBattleHUD,Warning,TEXT("%s"),*LastRouteStatus);
    return false;
}

bool UReditusBattleHUDPresenterComponent::ExecuteLegacyCommand(AActor* Unit,EReditusHUDCommand Command)
{
    if (!IsValid(Unit) || bRouting) return false;
    // Never promote a future ready entry by writing currentAttackingUnit.
    // Legacy commands are permitted only for the unit the existing controller owns.
    AActor* CurrentActor=CommandOwner(GetOwner());
    if (CurrentActor!=Unit)
    {
        LastRouteStatus=FString::Printf(TEXT("Command owner mismatch: selected=%s current=%s"),*GetNameSafe(Unit),*GetNameSafe(CurrentActor));
        if (bLogCommandRouting) UE_LOG(LogReditusBattleHUD,Log,TEXT("%s"),*LastRouteStatus);
        return false;
    }
    FName Function;
    switch (Command)
    {
    case EReditusHUDCommand::Attack: Function=TEXT("SelectEnemy"); break;
    case EReditusHUDCommand::Tech: Function=TEXT("TechPressed"); break;
    case EReditusHUDCommand::Items: Function=TEXT("UseItem"); break;
    case EReditusHUDCommand::Defend: Function=TEXT("Defend"); break;
    case EReditusHUDCommand::Overdrive:
        if (!Boolean(Unit,TEXT("CanEnterOverdrive")) || !Boolean(Unit,TEXT("OverdriveEntered"))) return false;
        Function=TEXT("Overdrive"); break;
    case EReditusHUDCommand::Hack:
        if (NameValue(Unit,TEXT("CharacterID"))!=HackCharacterID) return false;
        Function=TEXT("HackPressed"); break;
    case EReditusHUDCommand::Run: Function=TEXT("Run"); break;
    default: return false;
    }
    BeginNativeMenu(Unit,Command);
    TGuardValue<bool> Guard(bRouting,true);
    const bool Success=CallNoArgumentEvent(Unit,Function);
    if (Success && IsValid(HUD)) RecoverMenuPreparation(Unit,Command);
    if (!Success)
    {
        bAwaitingNativeMenu=false;
        if (IsValid(HUD) && PendingMenuOwner==Unit)
            HUD->ShowListStatus(FText::FromString(Command==EReditusHUDCommand::Items ? TEXT("ITEMS") : Command==EReditusHUDCommand::Overdrive ? TEXT("OVERDRIVE") : TEXT("SKILLS")),
                NSLOCTEXT("ReditusHUD","ChoiceCommandUnavailable","Choices are unavailable. Press Back to return."));
    }
    LastRouteStatus=FString::Printf(TEXT("%s: %s on %s"),Success ? TEXT("Called") : TEXT("Missing event"),*Function.ToString(),*GetNameSafe(Unit));
    if (bLogCommandRouting) UE_LOG(LogReditusBattleHUD,Log,TEXT("%s"),*LastRouteStatus);
    if (!Success) UE_LOG(LogReditusBattleHUD,Warning,TEXT("HUD could not call %s on %s. Bind OnCommandRequested for this command."),*Function.ToString(),*GetNameSafe(Unit));
    RefreshBattleHUD();
    return Success;
}

AActor* UReditusBattleHUDPresenterComponent::FindTimedAttackOwner() const
{
    const auto HasOpenWindow=[this](AActor* Unit)
    {
        // E_SelectType::RegularAttack is 5 in both timed-attack Blueprints.
        return IsValid(Unit) && PartyUnits.Contains(Unit) && Number(Unit,TEXT("currentHP"))>0.0 &&
            Number(Unit,TEXT("SelectType"),-1.0)==5.0 && Boolean(Unit,TEXT("TimedAttackOpportunity"));
    };
#if REDITUS_HUD_HAS_ATB
    if (const auto* ATB=RunningATB(GetOwner()))
    {
        AActor* EndingAttack=nullptr;
        for (const FReditusATBUnitView& State:ATB->GetUnitStates())
        {
            // The unit waiting for menu input can differ from the actor
            // whose action owns the execution lane.
            const bool bExecuting=State.Phase==EReditusATBPhase::Executing;
            if (!bExecuting && State.Phase!=EReditusATBPhase::CycleEnd) continue;
            FReditusATBAction Action;
            const bool bOwnsWindow=HasOpenWindow(State.Unit) && ATB->GetPendingAction(State.Unit,Action) &&
                Action.Kind==EReditusATBActionKind::Attack;
            if (bExecuting) return bOwnsWindow ? State.Unit : nullptr;
            if (bOwnsWindow) EndingAttack=State.Unit;
        }
        // Some legacy attacks acknowledge their main hit before the animation
        // closes the bonus window. CycleEnd still owns the lane until acknowledged.
        return EndingAttack;
    }
#endif
    AActor* LegacyAttacker=Cast<AActor>(ObjectValue(GetOwner(),TEXT("currentAttackingUnit")));
    return HasOpenWindow(LegacyAttacker) ? LegacyAttacker : nullptr;
}

bool UReditusBattleHUDPresenterComponent::ForwardGameplayInput(FName ActionName,FKey Key)
{
    if (!IsValid(HUD) || bRouting) return false;
    TGuardValue<bool> Guard(bRouting,true);
    if (!ActionName.IsNone() && ActionName==HUD->InputActions.TimedAttack)
    {
        AActor* Attacker=FindTimedAttackOwner();
        const bool bForwarded=ExecuteInputAction(Attacker,ActionName,Key,true);
        if (bLogCommandRouting)
            UE_LOG(LogReditusBattleHUD,Log,TEXT("Timed attack input on %s: %s"),*GetNameSafe(Attacker),
                bForwarded ? TEXT("forwarded") : Attacker ? TEXT("no bound input action") : TEXT("no open attack window"));
        return bForwarded;
    }
    if (RouteATBListTargetInput(ActionName))
    {
        RefreshBattleHUD();
        return true;
    }
    bool Routed=false;
    AActor* InputOwner=nullptr;
    if (ActionName==HUD->InputActions.Block || ActionName==HUD->InputActions.Speed)
    {
        Routed=ExecuteInputAction(Player,ActionName,Key);
        if (Routed) InputOwner=Player;
    }
    else
    {
        AActor* Unit=HUD->GetSelectedUnit();
        AActor* Current=CommandOwner(GetOwner());
        if (IsValid(Unit) && Unit==Current) { Routed=ExecuteInputAction(Unit,ActionName,Key); if (Routed) InputOwner=Unit; }
        if (!Routed) { Routed=ExecuteInputAction(Player,ActionName,Key); if (Routed) InputOwner=Player; }
    }
    if (!Routed && OnUnroutedGameplayInput.IsBound())
    {
        OnUnroutedGameplayInput.Broadcast(ActionName,Key);
        Routed=true;
        InputOwner=GetOwner();
    }
    AActor* SelectedActor=IsValid(HUD) ? HUD->GetSelectedUnit() : nullptr;
    LastRouteStatus=FString::Printf(TEXT("%s input %s; receiver=%s; selected=%s; current=%s; command owner=%s; SelectType=%d; input blocked=%d; target set=%d"),Routed ? TEXT("Forwarded") : TEXT("Unrouted"),
        *ActionName.ToString(),*GetNameSafe(InputOwner),*GetNameSafe(SelectedActor),*GetNameSafe(ObjectValue(GetOwner(),TEXT("currentAttackingUnit"))),
        *GetNameSafe(CommandOwner(GetOwner())),static_cast<int32>(Number(SelectedActor,TEXT("SelectType"),-1.0)),
        Boolean(SelectedActor,TEXT("isInputBlocked")),Boolean(SelectedActor,TEXT("TargetSet")));
    if (bLogCommandRouting || (!Routed && ActionName==TEXT("MenuConfirm"))) UE_LOG(LogReditusBattleHUD,Log,TEXT("%s"),*LastRouteStatus);
    RefreshBattleHUD();
    return Routed;
}

void UReditusBattleHUDPresenterComponent::HandleSelection(AActor* Unit) { OnSelectedUnitChanged.Broadcast(Unit); }
void UReditusBattleHUDPresenterComponent::HandleDevice(EReditusHUDInputDevice Device)
{
    if (!bSyncLegacyDeviceFlag || !IsValid(Player)) return;
    if (FBoolProperty* Field=CastField<FBoolProperty>(Property(Player,TEXT("IsUsingKeyboard"))))
        Field->SetPropertyValue_InContainer(Player,Device==EReditusHUDInputDevice::KeyboardMouse);
}
