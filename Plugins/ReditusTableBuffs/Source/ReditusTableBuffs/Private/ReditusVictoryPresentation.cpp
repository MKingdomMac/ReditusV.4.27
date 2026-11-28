#include "ReditusVictoryPresentation.h"
#include "ReditusTechMasteryLibrary.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Camera/CameraComponent.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Styling/CoreStyle.h"
#include "TimerManager.h"

struct FReditusResultsPresentation
{
    FTimerHandle Timer;
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<AActor> StaticCamera;
    TWeakObjectPtr<UCameraComponent> Camera;
    TWeakObjectPtr<UUserWidget> Screen;
    TWeakObjectPtr<UWidget> Panel;
    FTransform OriginalRelative;
    FTransform Start;
    FVector Location;
    FQuat Rotation;
    float OriginalLens = 0.f;
    float Lens = 0.f;
    float CameraStartTime = 0.f;
    float PanelStartTime = 0.f;
    bool bOrthographic = false;
    bool bCameraSettled = false;
    bool bPanelSettled = false;
    bool bRunning = true;
};

namespace ReditusVictoryPresentation
{
    namespace
    {
        const FName PanelName(TEXT("ReditusVictoryResults"));
        const FName MasteryName(TEXT("ReditusSelectedTechMastery"));
        const FLinearColor PanelColor(0.009f, 0.016f, 0.031f, 0.82f);
        const FLinearColor MasteryBadgeColor(0.009f, 0.016f, 0.031f, 0.94f);
        const FLinearColor Brass(0.64f, 0.40f, 0.14f, 1.f);
        const FLinearColor PaleGold(1.f, 0.78f, 0.43f, 1.f);
        const FLinearColor Ivory(0.97f, 0.92f, 0.83f, 1.f);
        const FLinearColor Slate(0.48f, 0.61f, 0.72f, 1.f);
        const FLinearColor StatGain(0.47f, 0.84f, 0.64f, 1.f);
        const FLinearColor StatLoss(0.94f, 0.51f, 0.44f, 1.f);
        constexpr float PartyScreenOffset = 0.44f; // 72% of the screen width.
        constexpr float ZoomFactor = 0.84f;
        constexpr float BlendDuration = 0.45f;

        UTextBlock* Label(UWidgetTree* Tree, const FSlateFontInfo& Font, const FText& Value, int32 Size, FLinearColor Color)
        {
            auto* Text = Tree->ConstructWidget<UTextBlock>();
            FSlateFontInfo Sized = Font;
            Sized.Size = Size;
            Sized.OutlineSettings.OutlineSize = 0;
            Text->SetFont(Sized);
            Text->SetText(Value);
            Text->SetColorAndOpacity(FSlateColor(Color));
            return Text;
        }

        void Rule(UWidgetTree* Tree, UVerticalBox* Box, FLinearColor Color, float Height, FMargin Padding)
        {
            auto* Stroke = Tree->ConstructWidget<UBorder>();
            Stroke->SetBrushColor(Color);
            auto* Size = Tree->ConstructWidget<USizeBox>();
            Size->SetHeightOverride(Height);
            Size->AddChild(Stroke);
            Box->AddChildToVerticalBox(Size)->SetPadding(Padding);
        }

        UVerticalBox* MakeValueCard(UWidgetTree* Tree, UHorizontalBox* Row, const FSlateFontInfo& Font, const FText& Name,
            FMargin Padding)
        {
            auto* Card = Tree->ConstructWidget<UBorder>();
            Card->SetBrushColor(FLinearColor(0.038f, 0.065f, 0.093f, 0.32f));
            Card->SetPadding(Padding);
            auto* Column = Tree->ConstructWidget<UVerticalBox>();
            Card->SetContent(Column);
            Column->AddChildToVerticalBox(Label(Tree, Font, Name, 16, Slate));
            auto* Slot = Row->AddChildToHorizontalBox(Card);
            Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            if (Row->GetChildrenCount() > 1) Slot->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f));
            return Column;
        }

        FText SignedValue(const FText& Number, bool bPositive)
        {
            return bPositive ? FText::Format(NSLOCTEXT("ReditusVictory", "Amount", "+{0}"), Number) : Number;
        }

        void Reward(UWidgetTree* Tree, UHorizontalBox* Row, const FSlateFontInfo& Font, const FText& Name, int32 Amount)
        {
            auto* Column = MakeValueCard(Tree, Row, Font, Name, FMargin(12.f, 9.f));
            const FText Value = SignedValue(FText::AsNumber(Amount), Amount > 0);
            Column->AddChildToVerticalBox(Label(Tree, Font, Value, 27, Ivory))->SetPadding(FMargin(0.f, 3.f, 0.f, 0.f));
        }

        void LevelStat(UWidgetTree* Tree, UHorizontalBox* Row, const FSlateFontInfo& Font, const FText& Name,
            const FText& Value, int64 Change)
        {
            auto* Column = MakeValueCard(Tree, Row, Font, Name, FMargin(10.f, 5.f));
            auto* Numbers = Tree->ConstructWidget<UHorizontalBox>();
            Column->AddChildToVerticalBox(Numbers)->SetPadding(FMargin(0.f, 1.f, 0.f, 0.f));
            Numbers->AddChildToHorizontalBox(Label(Tree, Font, Value, 22, Ivory))->SetVerticalAlignment(VAlign_Center);
            if (Change != 0)
            {
                const FText Delta = SignedValue(FText::AsNumber(Change), Change > 0);
                auto* Slot = Numbers->AddChildToHorizontalBox(Label(Tree, Font, Delta, 17, Change > 0 ? StatGain : StatLoss));
                Slot->SetPadding(FMargin(10.f, 0.f, 0.f, 0.f));
                Slot->SetVerticalAlignment(VAlign_Center);
            }
        }

        void TechProgress(UWidgetTree* Tree, UVerticalBox* List, const FSlateFontInfo& Font, const FReditusVictoryTechProgress& Tech)
        {
            auto* Lines = Tree->ConstructWidget<UVerticalBox>();
            auto* Header = Tree->ConstructWidget<UHorizontalBox>();
            Lines->AddChildToVerticalBox(Header);
            auto* Name = Label(Tree, Font, FText::Format(NSLOCTEXT("ReditusVictory", "Tech", "{0}  /  {1}"),
                Tech.Character, Tech.Tech), 15, Ivory);
            Name->SetAutoWrapText(true);
            auto* NameSlot = Header->AddChildToHorizontalBox(Name);
            NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            NameSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
            Header->AddChildToHorizontalBox(Label(Tree, Font, FText::Format(NSLOCTEXT("ReditusVictory", "Progress", "{0}/{1}"),
                FText::AsNumber(Tech.Current), FText::AsNumber(Tech.Required)), 15, PaleGold));

            FProgressBarStyle Style;
            Style.SetBackgroundImage(FSlateColorBrush(FLinearColor(0.045f, 0.068f, 0.096f, 1.f)));
            Style.SetFillImage(FSlateColorBrush(Brass));
            auto* Bar = Tree->ConstructWidget<UProgressBar>();
            Bar->WidgetStyle = Style;
            Bar->SetPercent(FMath::Clamp(float(Tech.Current) / FMath::Max(1, Tech.Required), 0.f, 1.f));
            auto* Size = Tree->ConstructWidget<USizeBox>();
            Size->SetHeightOverride(4.f);
            Size->AddChild(Bar);
            Lines->AddChildToVerticalBox(Size)->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
            List->AddChildToVerticalBox(Lines)->SetPadding(FMargin(0.f, 2.f, 0.f, 7.f));
        }

        UCanvasPanel* MountCanvas(UWidgetTree* Tree)
        {
            if (auto* Canvas = Cast<UCanvasPanel>(Tree->RootWidget)) return Canvas;
            auto* Overlay = Cast<UOverlay>(Tree->RootWidget);
            if (!Overlay) return nullptr;
            auto* Canvas = Tree->ConstructWidget<UCanvasPanel>();
            Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
            auto* Slot = Overlay->AddChildToOverlay(Canvas);
            Slot->SetHorizontalAlignment(HAlign_Fill);
            Slot->SetVerticalAlignment(VAlign_Fill);
            return Canvas;
        }

        struct FResultsPanel
        {
            UBorder* Sheet = nullptr;
            UVerticalBox* Column = nullptr;
            TArray<UWidget*> Previous;
        };

        FResultsPanel MakeResultsPanel(UWidgetTree* Tree, const FSlateFontInfo& Font, const FText& Title)
        {
            if (!Tree || Tree->FindWidget(PanelName)) return {};
            auto* Root = Cast<UPanelWidget>(Tree->RootWidget);
            if (!Root) return {};
            FResultsPanel Panel;
            Panel.Previous = Root->GetAllChildren();
            auto* Canvas = MountCanvas(Tree);
            if (!Canvas) return {};

            Panel.Sheet = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), PanelName);
            Panel.Sheet->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
            Panel.Sheet->SetBrushColor(PanelColor);
            Panel.Sheet->SetPadding(FMargin(24.f, 18.f));
            auto* Placement = Canvas->AddChildToCanvas(Panel.Sheet);
            Placement->SetAnchors(FAnchors(0.055f, 0.19f, 0.45f, 0.77f));
            Placement->SetOffsets(FMargin(0.f));
            Placement->SetZOrder(20);

            Panel.Column = Tree->ConstructWidget<UVerticalBox>();
            Panel.Sheet->SetContent(Panel.Column);
            Rule(Tree, Panel.Column, Brass, 2.f, FMargin(0.f, 0.f, 0.f, 9.f));
            Panel.Column->AddChildToVerticalBox(Label(Tree, Font, Title, 42, PaleGold));
            return Panel;
        }

        UVerticalBox* ScrollableResults(UWidgetTree* Tree, UVerticalBox* Column)
        {
            auto* Scroll = Tree->ConstructWidget<UScrollBox>();
            Column->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            auto* Results = Tree->ConstructWidget<UVerticalBox>();
            Scroll->AddChild(Results);
            return Results;
        }

        void AddFooter(UWidgetTree* Tree, UVerticalBox* Column, const FSlateFontInfo& Font, UWidget* ContinueButton = nullptr)
        {
            Rule(Tree, Column, Brass, 1.f, FMargin(0.f, 10.f, 0.f, 7.f));
            if (ContinueButton)
            {
                ContinueButton->RemoveFromParent();
                Column->AddChildToVerticalBox(ContinueButton)->SetHorizontalAlignment(HAlign_Right);
                if (ContinueButton->GetVisibility() != ESlateVisibility::Collapsed &&
                    ContinueButton->GetVisibility() != ESlateVisibility::Hidden) return;
            }
            Column->AddChildToVerticalBox(Label(Tree, Font, NSLOCTEXT("ReditusVictory", "Continue", "Confirm to continue"),
                14, Slate))->SetHorizontalAlignment(HAlign_Right);
        }

        void HideOldLayout(const FResultsPanel& Panel, const UWidget* MovedList, const UWidget* MovedButton = nullptr)
        {
            for (auto* OldWidget : Panel.Previous)
            {
                if (OldWidget != MovedList && OldWidget != MovedButton) OldWidget->SetVisibility(ESlateVisibility::Collapsed);
            }
        }

        bool SetPartyFraming(UCameraComponent* Camera, const TArray<AActor*>& Party, FReditusResultsPresentation& Blend)
        {
            FVector Center = FVector::ZeroVector;
            float Left = MAX_flt;
            float Right = -MAX_flt;
            int32 Count = 0;
            for (const AActor* Unit : Party)
            {
                if (!IsValid(Unit) || Unit->IsHidden()) continue;
                const FVector Local = Blend.Start.InverseTransformPositionNoScale(Unit->GetActorLocation());
                if (Local.X <= KINDA_SMALL_NUMBER) continue;
                const float Horizontal = Blend.bOrthographic ? Local.Y : FMath::Atan2(Local.Y, Local.X);
                Left = FMath::Min(Left, Horizontal);
                Right = FMath::Max(Right, Horizontal);
                Center += Local;
                ++Count;
            }
            if (Count == 0) return false;
            Center /= Count;

            if (Blend.bOrthographic)
            {
                Blend.OriginalLens = Camera->OrthoWidth;
                Blend.Lens = FMath::Clamp((Right - Left) / 0.60f, Blend.OriginalLens * ZoomFactor, Blend.OriginalLens);
                const float CenterRight = (Left + Right) * 0.5f;
                const float Offset = FMath::Min(CenterRight - Blend.Lens * PartyScreenOffset * 0.5f, Left + Blend.Lens * 0.40f);
                Blend.Location += Blend.Start.TransformVectorNoScale(FVector(0.f, Offset, 0.f));
                return true;
            }

            Blend.OriginalLens = Camera->FieldOfView;
            const float MinimumFOV = FMath::RadiansToDegrees(Right - Left) / 0.60f;
            Blend.Lens = FMath::Clamp(MinimumFOV, Blend.OriginalLens * ZoomFactor, Blend.OriginalLens);
            const float HalfWidth = FMath::Tan(FMath::DegreesToRadians(Blend.Lens * 0.5f));
            const float Aim = (Left + Right) * 0.5f - FMath::Atan(PartyScreenOffset * HalfWidth);
            const float MinAim = Right - FMath::Atan(0.86f * HalfWidth);
            const float RightSideAim = Left + FMath::Atan(0.07f * HalfWidth);
            const float MaxAim = MinAim <= RightSideAim ? RightSideAim : Left + FMath::Atan(0.73f * HalfWidth);
            const float Yaw = MinAim <= MaxAim ? FMath::Clamp(Aim, MinAim, MaxAim) : (Left + Right) * 0.5f;
            const float ViewHeight = HalfWidth / FMath::Max(1.f, Camera->AspectRatio);
            const float CenterPitch = FMath::Atan2(Center.Z, FVector2D(Center.X, Center.Y).Size());
            const float Pitch = FMath::Clamp(CenterPitch + FMath::Atan(0.24f * ViewHeight),
                FMath::DegreesToRadians(-8.f), FMath::DegreesToRadians(8.f));
            Blend.Rotation *= FRotator(FMath::RadiansToDegrees(Pitch), FMath::RadiansToDegrees(Yaw), 0.f).Quaternion();
            return true;
        }

        void SetLens(UCameraComponent* Camera, bool bOrthographic, float Value)
        {
            if (bOrthographic) Camera->SetOrthoWidth(Value);
            else Camera->SetFieldOfView(Value);
        }

        void Stop(FReditusResultsPresentation& Presentation)
        {
            if (!Presentation.bRunning) return;
            Presentation.bRunning = false;
            if (auto* Camera = Presentation.Camera.Get())
            {
                Camera->SetRelativeTransform(Presentation.OriginalRelative);
                SetLens(Camera, Presentation.bOrthographic, Presentation.OriginalLens);
            }
            if (auto* World = Presentation.World.Get()) World->GetTimerManager().ClearTimer(Presentation.Timer);
        }

        float EaseInOut(float Alpha)
        {
            return Alpha * Alpha * (3.f - 2.f * Alpha);
        }

        void TickPresentation(FReditusResultsPresentation& Presentation)
        {
            auto* World = Presentation.World.Get();
            auto* Screen = Presentation.Screen.Get();
            if (!World || !Screen || !Screen->IsInViewport())
            {
                Stop(Presentation);
                return;
            }

            const float Now = World->GetTimeSeconds();
            if (auto* Panel = Presentation.Panel.Get())
            {
                if (!Presentation.bPanelSettled)
                {
                    const float Alpha = FMath::Clamp((Now - Presentation.PanelStartTime) / BlendDuration, 0.f, 1.f);
                    const float Ease = EaseInOut(Alpha);
                    Panel->SetRenderOpacity(Ease);
                    Panel->SetRenderTranslation(FVector2D(-16.f * (1.f - Ease), 0.f));
                    Presentation.bPanelSettled = Alpha >= 1.f;
                }
            }
            if (auto* Camera = Presentation.Camera.Get())
            {
                if (!Presentation.bCameraSettled)
                {
                    const float Alpha = FMath::Clamp((Now - Presentation.CameraStartTime) / BlendDuration, 0.f, 1.f);
                    const float Ease = EaseInOut(Alpha);
                    Camera->SetWorldLocationAndRotation(
                        FMath::Lerp(Presentation.Start.GetLocation(), Presentation.Location, Ease),
                        FQuat::Slerp(Presentation.Start.GetRotation(), Presentation.Rotation, Ease));
                    SetLens(Camera, Presentation.bOrthographic, FMath::Lerp(Presentation.OriginalLens, Presentation.Lens, Ease));
                    Presentation.bCameraSettled = Alpha >= 1.f;
                }
            }
        }
    }

    UWidget* Build(UUserWidget* Widget, UPanelWidget* Drops, int32 EXP, int32 Gold, const UTextBlock* FontSource,
        const FReditusEncounterMastery* Mastery)
    {
        auto* Tree = Widget->WidgetTree;
        const FSlateFontInfo Font = FontSource
            ? FontSource->Font : FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 20);
        const auto Panel = MakeResultsPanel(Tree, Font, NSLOCTEXT("ReditusVictory", "Title", "Victory"));
        if (!Panel.Sheet) return nullptr;
        auto* Column = Panel.Column;
        auto* Rewards = Tree->ConstructWidget<UHorizontalBox>();
        Column->AddChildToVerticalBox(Rewards)->SetPadding(FMargin(0.f, 14.f, 0.f, 12.f));
        Reward(Tree, Rewards, Font, NSLOCTEXT("ReditusVictory", "EXP", "EXP"), EXP);
        Reward(Tree, Rewards, Font, NSLOCTEXT("ReditusVictory", "Gold", "Gold"), Gold);
        Reward(Tree, Rewards, Font, NSLOCTEXT("ReditusVictory", "Mastery", "Mastery"),
            Mastery && Mastery->bAnyProgress ? Mastery->Reward : 0);

        auto* Results = ScrollableResults(Tree, Column);
        if (Mastery && Mastery->bAnyProgress)
        {
            Results->AddChildToVerticalBox(Label(Tree, Font, NSLOCTEXT("ReditusVictory", "EquipmentMastery", "Equipment Mastery"),
                17, Slate))->SetPadding(FMargin(0.f, 0.f, 0.f, 7.f));
            for (const FText& Learned : Mastery->Learned)
            {
                auto* Text = Label(Tree, Font, Learned, 17, PaleGold);
                Text->SetAutoWrapText(true);
                Results->AddChildToVerticalBox(Text)->SetPadding(FMargin(0.f, 0.f, 0.f, 7.f));
            }
            for (const auto& Tech : Mastery->Progress) TechProgress(Tree, Results, Font, Tech);
        }
        const bool bHasDrops = IsValid(Drops) && Drops->GetChildrenCount() > 0;
        if (bHasDrops)
        {
            Rule(Tree, Results, Brass, 1.f, FMargin(0.f, 6.f, 0.f, 8.f));
            Results->AddChildToVerticalBox(Label(Tree, Font, NSLOCTEXT("ReditusVictory", "Drops", "Items obtained"),
                17, Slate))->SetPadding(FMargin(0.f, 0.f, 0.f, 5.f));
            Drops->RemoveFromParent();
            Results->AddChildToVerticalBox(Drops);
        }
        AddFooter(Tree, Column, Font);
        HideOldLayout(Panel, bHasDrops ? Drops : nullptr);
        return Panel.Sheet;
    }

    UWidget* BuildLevelUp(UUserWidget* Widget, const FReditusLevelUpChanges& Changes)
    {
        auto* Tree = Widget->WidgetTree;
        if (!Tree) return nullptr;
        if (auto* Existing = Tree->FindWidget(PanelName)) return Existing;
        const auto ReadText = [Tree](FName Name)
        {
            auto* Field = Cast<UTextBlock>(Tree->FindWidget(Name));
            return Field ? Field->GetText() : FText::GetEmpty();
        };
        auto* UnitName = Cast<UTextBlock>(Tree->FindWidget(TEXT("UnitNameText")));
        const FText Level = ReadText(TEXT("UnitLevelText"));
        if (!UnitName || UnitName->GetText().IsEmpty() || Level.IsEmpty()) return nullptr;

        const FSlateFontInfo Font = UnitName->Font;
        const auto Panel = MakeResultsPanel(Tree, Font, NSLOCTEXT("ReditusVictory", "LevelUpTitle", "Level Up"));
        if (!Panel.Sheet) return nullptr;
        auto* Header = Tree->ConstructWidget<UHorizontalBox>();
        Panel.Column->AddChildToVerticalBox(Header)->SetPadding(FMargin(0.f, 6.f, 0.f, 8.f));
        auto* Name = Label(Tree, Font, UnitName->GetText(), 25, Ivory);
        Name->SetAutoWrapText(true);
        auto* NameSlot = Header->AddChildToHorizontalBox(Name);
        NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        NameSlot->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
        Header->AddChildToHorizontalBox(Label(Tree, Font, Level, 20, PaleGold))->SetVerticalAlignment(VAlign_Center);

        struct FStatField { FName WidgetName; FName StatName; FText Title; };
        const FStatField Stats[] = {
            {TEXT("HpText"), TEXT("maxHP"), NSLOCTEXT("ReditusVictory", "StatHP", "HP")},
            {TEXT("MPText"), TEXT("maxMP"), NSLOCTEXT("ReditusVictory", "StatMP", "MP")},
            {TEXT("PhysicalAttackText"), TEXT("Attack"), NSLOCTEXT("ReditusVictory", "StatPAtk", "P.ATK")},
            {TEXT("DefenseText"), TEXT("defense"), NSLOCTEXT("ReditusVictory", "StatPDef", "P.DEF")},
            {TEXT("MagicalAttackText"), TEXT("MagicalAttack"), NSLOCTEXT("ReditusVictory", "StatMAtk", "M.ATK")},
            {TEXT("MagicalDefenseText"), TEXT("magicDefense"), NSLOCTEXT("ReditusVictory", "StatMDef", "M.DEF")},
            {TEXT("HitText"), TEXT("hit"), NSLOCTEXT("ReditusVictory", "StatHit", "HIT")},
            {TEXT("SpeedText"), TEXT("speed"), NSLOCTEXT("ReditusVictory", "StatSpd", "SPD")}
        };

        auto* Fit = Tree->ConstructWidget<UScaleBox>();
        Fit->SetStretch(EStretch::ScaleToFit);
        Fit->SetStretchDirection(EStretchDirection::DownOnly);
        auto* Results = Tree->ConstructWidget<UVerticalBox>();
        auto* FitSlot = CastChecked<UScaleBoxSlot>(Fit->AddChild(Results));
        FitSlot->SetHorizontalAlignment(HAlign_Fill);
        FitSlot->SetVerticalAlignment(VAlign_Top);
        Panel.Column->AddChildToVerticalBox(Fit)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

        UHorizontalBox* Row = nullptr;
        for (const FStatField& Stat : Stats)
        {
            if (!Row || Row->GetChildrenCount() == 2)
            {
                Row = Tree->ConstructWidget<UHorizontalBox>();
                Results->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
            }
            LevelStat(Tree, Row, Font, Stat.Title, ReadText(Stat.WidgetName), Changes.Stats.FindRef(Stat.StatName));
        }
        auto* ActionTime = Tree->FindWidget(TEXT("ActionTimeOverlay"));
        const FText TimeValue = ReadText(TEXT("ActionTimeText"));
        if (ActionTime && ActionTime->GetVisibility() != ESlateVisibility::Collapsed &&
            ActionTime->GetVisibility() != ESlateVisibility::Hidden && !TimeValue.IsEmpty())
        {
            auto* Time = Tree->ConstructWidget<UHorizontalBox>();
            Results->AddChildToVerticalBox(Time)->SetPadding(FMargin(0.f, 5.f, 0.f, 0.f));
            Time->AddChildToHorizontalBox(Label(Tree, Font, TimeValue, 16, Slate));
            if (!FMath::IsNearlyZero(Changes.ActionTime, 0.0005f))
            {
                const FText Delta = SignedValue(FText::AsNumber(Changes.ActionTime), Changes.ActionTime > 0.f);
                Time->AddChildToHorizontalBox(Label(Tree, Font, Delta, 16, PaleGold))->SetPadding(FMargin(10.f, 0.f, 0.f, 0.f));
            }
        }

        auto* Skills = Cast<UPanelWidget>(Tree->FindWidget(TEXT("skillsList")));
        const bool bHasSkills = Skills && Skills->GetChildrenCount() > 0;
        if (bHasSkills)
        {
            Rule(Tree, Results, Brass, 1.f, FMargin(0.f, 6.f, 0.f, 8.f));
            Results->AddChildToVerticalBox(Label(Tree, Font, NSLOCTEXT("ReditusVictory", "NewTechs", "New Techs"),
                17, Slate))->SetPadding(FMargin(0.f, 0.f, 0.f, 5.f));
            Skills->RemoveFromParent();
            Results->AddChildToVerticalBox(Skills);
        }
        auto* ContinueButton = Tree->FindWidget(TEXT("BP_ContinueButton"));
        AddFooter(Tree, Panel.Column, Font, ContinueButton);
        HideOldLayout(Panel, bHasSkills ? Skills : nullptr, ContinueButton);
        return Panel.Sheet;
    }

    bool UpdateTechMastery(UUserWidget* Widget, const UTextBlock* FontSource, const FText& Status)
    {
        auto* Tree = Widget->WidgetTree;
        if (!Tree) return false;
        auto* Badge = Cast<UBorder>(Tree->FindWidget(MasteryName));
        if (Status.IsEmpty())
        {
            if (Badge) Badge->SetVisibility(ESlateVisibility::Collapsed);
            return true;
        }
        if (!Badge)
        {
            auto* Canvas = MountCanvas(Tree);
            if (!Canvas) return false;
            Badge = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), MasteryName);
            Badge->SetBrushColor(MasteryBadgeColor);
            Badge->SetPadding(FMargin(16.f, 8.f));
            Badge->SetContent(Label(Tree, FontSource->Font, Status, 19, PaleGold));
            auto* Place = Canvas->AddChildToCanvas(Badge);
            // Centered in the lower half of the existing right-hand Tech details pane.
            Place->SetAnchors(FAnchors(0.735f, 0.62f, 0.735f, 0.62f));
            Place->SetAlignment(FVector2D(0.5f, 0.5f));
            Place->SetAutoSize(true);
            Place->SetOffsets(FMargin(0.f));
            Place->SetZOrder(20);
        }
        auto* Text = Cast<UTextBlock>(Badge->GetChildAt(0));
        if (!Text) return false;
        Text->SetText(Status);
        Badge->SetVisibility(ESlateVisibility::HitTestInvisible);
        return true;
    }

    void Reveal(UUserWidget* Widget, UWidget* Panel, APlayerController* Player, AActor* StaticCamera,
        const TArray<AActor*>& Party, TSharedPtr<FReditusResultsPresentation>& Active)
    {
        auto* World = Widget->GetWorld();
        if (!World) return;
        const bool bStart = !Active || !Active->bRunning || Active->World.Get() != World ||
            Active->StaticCamera.Get() != StaticCamera;
        const float Now = World->GetTimeSeconds();
        if (bStart)
        {
            Finish(Active);
            Active = MakeShared<FReditusResultsPresentation>();
            Active->World = World;
            Active->StaticCamera = StaticCamera;
            Active->CameraStartTime = Now;
            auto* Camera = IsValid(Player) && IsValid(StaticCamera) ? StaticCamera->FindComponentByClass<UCameraComponent>() : nullptr;
            if (Camera && Camera->IsActive())
            {
                Active->OriginalRelative = Camera->GetRelativeTransform();
                Active->Start = Camera->GetComponentTransform();
                Active->Location = Active->Start.GetLocation();
                Active->Rotation = Active->Start.GetRotation();
                Active->bOrthographic = Camera->ProjectionMode == ECameraProjectionMode::Orthographic;
                if (SetPartyFraming(Camera, Party, *Active)) Active->Camera = Camera;
            }
        }

        if (Active->Screen.Get() == Widget) return;
        if (auto* Previous = Active->Panel.Get()) Previous->SetVisibility(ESlateVisibility::Collapsed);
        Active->Screen = Widget;
        Active->Panel = Panel;
        Active->PanelStartTime = Now;
        Active->bPanelSettled = false;
        if (Panel)
        {
            Panel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
            Panel->SetRenderOpacity(0.f);
            Panel->SetRenderTranslation(FVector2D(-16.f, 0.f));
        }
        if (IsValid(Player) && Active->Camera.IsValid())
        {
            if (!bStart || Player->GetViewTarget() != StaticCamera)
            {
                Player->SetViewTargetWithBlend(StaticCamera, bStart ? BlendDuration : 0.f);
            }
        }
        if (bStart)
        {
            auto Tick = FTimerDelegate::CreateLambda([Presentation = Active]() { TickPresentation(*Presentation); });
            World->GetTimerManager().SetTimer(Active->Timer, Tick, 1.f / 60.f, true);
        }
    }

    void Finish(TSharedPtr<FReditusResultsPresentation>& Active)
    {
        if (Active) Stop(*Active);
        Active.Reset();
    }
}
