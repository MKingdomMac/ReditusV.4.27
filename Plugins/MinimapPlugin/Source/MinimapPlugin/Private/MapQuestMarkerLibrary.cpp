#include "MapQuestMarkerLibrary.h"
#include "MapQuestMarkerPolicy.h"
#include "MapIconComponent.h"
#include "MapFunctionLibrary.h"
#include "MapTrackerComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/WidgetComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

UTexture2D* UMapQuestMarkerStyle::Icon(bool MainQuest)
{
    auto& Custom = MainQuest ? MainExclamationIcon : SideQuestionIcon;
    if (!Custom.IsNull()) if (auto* Loaded = Custom.LoadSynchronous()) return Loaded;
    auto*& Generated = MainQuest ? GeneratedMain : GeneratedSide;
    if (Generated) return Generated;
    using namespace MapQuestMarkerPolicy;
    Generated = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
    if (!Generated || !Generated->PlatformData || Generated->PlatformData->Mips.Num() == 0) return nullptr;
    const auto Raster = MapQuestMarkerPolicy::Raster(MainQuest);
    auto& Mip = Generated->PlatformData->Mips[0];
    auto* Data = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
    for (int32 I = 0; I < Width * Height; ++I)
        Data[I] = Raster[unsigned(I)] == 2 ? FColor::White
            : Raster[unsigned(I)] == 1 ? FColor(12, 16, 24, 255) : FColor(0, 0, 0, 0);
    Mip.BulkData.Unlock();
    Generated->SRGB = true; Generated->Filter = TF_Nearest;
    Generated->UpdateResource(); return Generated;
}

void UMapQuestHeadWidget::SetQuestType(bool MainQuest)
{
    bMainQuest = MainQuest;
    if (!Glyph) return;
    auto* Style = GetMutableDefault<UMapQuestMarkerStyle>();
    Glyph->SetBrushFromTexture(Style->Icon(MainQuest));
    Glyph->SetColorAndOpacity(MainQuest ? Style->MainQuestColor : Style->SideQuestColor);
}
TSharedRef<SWidget> UMapQuestHeadWidget::RebuildWidget()
{
    WidgetTree = NewObject<UWidgetTree>(this, TEXT("QuestHeadTree"));
    Glyph = WidgetTree->ConstructWidget<UImage>(); WidgetTree->RootWidget = Glyph;
    SetVisibility(ESlateVisibility::HitTestInvisible); SetQuestType(bMainQuest);
    return Super::RebuildWidget();
}

void UMapQuestMarkerLibrary::ApplyQuestMapIcon(UMapIconComponent* Icon, bool Visible, bool MainQuest, FText Tooltip)
{
    if (!IsValid(Icon)) return;
    // Retry registration if an NPC began play before the game's minimap tracker.
    // The original RegisterMapIcon uses Add, so guard against duplicate entries.
    if (auto* Tracker = Icon->HasBegunPlay() ? UMapFunctionLibrary::GetMapTracker(Icon) : nullptr)
        if (!Tracker->GetMapIcons().Contains(Icon)) Tracker->RegisterMapIcon(Icon);
    auto* Style = GetMutableDefault<UMapQuestMarkerStyle>();
    if (Visible)
    {
        auto* Texture = Style->Icon(MainQuest);
        const auto Color = MainQuest ? Style->MainQuestColor : Style->SideQuestColor;
        if (Icon->GetIconTexture() != Texture) Icon->SetIconTexture(Texture);
        if (Icon->GetIconDrawColor() != Color) Icon->SetIconDrawColor(Color);
        if (Icon->GetIconSize() != Style->MinimapSize || Icon->GetIconSizeUnit() != EIconSizeUnit::ScreenSpace)
            Icon->SetIconSize(Style->MinimapSize, EIconSizeUnit::ScreenSpace);
        if (Icon->GetIconTooltipText() != FName(*Tooltip.ToString())) Icon->SetIconTooltipText(FName(*Tooltip.ToString()));
        if (Icon->DoesIconRotate()) Icon->SetIconRotates(false);
        if (Icon->IsObjectiveArrowEnabled() != Style->EdgeMarkers) Icon->SetObjectiveArrowEnabled(Style->EdgeMarkers);
        if (Icon->GetObjectiveArrowTexture() != Texture) Icon->SetObjectiveArrowTexture(Texture);
        if (Icon->DoesObjectiveArrowRotate()) Icon->SetObjectiveArrowRotates(false);
    }
    if (Icon->IsIconVisible() != Visible) Icon->SetIconVisible(Visible);
}

UWidgetComponent* UMapQuestMarkerLibrary::UpdateQuestHead(AActor* Owner, UWidgetComponent* Existing,
    FVector HeadTop, bool Visible, bool MainQuest)
{
    if (!IsValid(Owner) || !Owner->GetWorld() || Owner->GetNetMode() == NM_DedicatedServer) return Existing;
    auto* Style = GetMutableDefault<UMapQuestMarkerStyle>();
    if (!IsValid(Existing)) Existing = nullptr;
    if (!Existing && Visible)
    {
        auto* Player = UGameplayStatics::GetPlayerController(Owner, 0);
        if (!Player || !Owner->GetRootComponent()) return nullptr;
        auto* View = CreateWidget<UMapQuestHeadWidget>(Player);
        if (!View) return nullptr;
        Existing = NewObject<UWidgetComponent>(Owner);
        Existing->SetWidgetSpace(EWidgetSpace::Screen);
        Existing->SetDrawSize(Style->OverheadSize); Existing->SetPivot(FVector2D(.5f, 1.f));
        Existing->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Existing->SetupAttachment(Owner->GetRootComponent()); Existing->SetWidget(View);
        Existing->SetVisibility(false); Owner->AddInstanceComponent(Existing); Existing->RegisterComponent();
    }
    if (!IsValid(Existing)) return nullptr;
    if (auto* View = Cast<UMapQuestHeadWidget>(Existing->GetUserWidgetObject())) View->SetQuestType(MainQuest);
    Existing->SetWorldLocation(HeadTop + Style->OverheadOffset);
    Existing->SetHiddenInGame(!Visible); Existing->SetVisibility(Visible);
    return Existing;
}
