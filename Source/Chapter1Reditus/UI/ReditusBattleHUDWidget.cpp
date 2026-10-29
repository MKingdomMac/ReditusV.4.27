#include "ReditusBattleHUDWidget.h"
#include "ReditusBattleHUDPolicy.h"
#include "ReditusBattleHUDInputPolicy.h"
#include "ReditusBattleHUDSkin.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/Spacer.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Engine/Font.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Actor.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Layout/WidgetPath.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Brushes/SlateColorBrush.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UnrealType.h"

namespace
{
    constexpr int32 MenuRows = 8;
    constexpr float BottomPanelHeight = 224.f;
    const EReditusHUDCommand CommandOrder[] = {
        EReditusHUDCommand::Attack, EReditusHUDCommand::Tech,
        EReditusHUDCommand::Items, EReditusHUDCommand::Defend,
        EReditusHUDCommand::ChangeParty, EReditusHUDCommand::Run,
        EReditusHUDCommand::ReadyAlly, EReditusHUDCommand::Hack
    };

    FText Text(const TCHAR* Value) { return FText::FromString(Value); }

    FText Numbers(float Current, float Maximum)
    {
        const auto Number = [](float Value) {
            return FText::AsNumber(FMath::IsFinite(Value) ? FMath::Max(0, FMath::RoundToInt(Value)) : 0);
        };
        return FText::Format(NSLOCTEXT("ReditusHUD", "ResourcePair", "{0}/{1}"), Number(Current), Number(Maximum));
    }

    /** Captures only the owning battle's keys inside the game widget path.
     * Menus and gameplay clicks share the same RequestCommand route.
     */
    class FHUDInputObserver final : public IInputProcessor
    {
    public:
        explicit FHUDInputObserver(UReditusBattleHUDWidget* InHUD) : HUD(InHUD) {}
        virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}
        virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& Event) override
        {
            if (!CanObserve(Event.GetUserIndex())) return false;
            if (!Event.IsRepeat()) HUD->ObserveKey(Event.GetKey());
            if (HUD->bCaptureBattleInput && HUD->HasGameInputFocus())
                return HUD->HandleBattleKeyDown(Event.GetKey(),Event.IsRepeat());
            return false;
        }
        virtual bool HandleKeyUpEvent(FSlateApplication&, const FKeyEvent& Event) override
        {
            if (!CanObserve(Event.GetUserIndex())) return false;
            const bool Owned=HUD->HandleBattleKeyUp(Event.GetKey());
            return Owned && HUD->bCaptureBattleInput && HUD->HasGameInputFocus();
        }
        virtual bool HandleAnalogInputEvent(FSlateApplication&, const FAnalogInputEvent& Event) override
        {
            const float Value = Event.GetAnalogValue();
            const float Previous = PreviousAxes.FindRef(Event.GetKey());
            PreviousAxes.Add(Event.GetKey(), Value);
            if (CanObserve(Event.GetUserIndex()) && ReditusHUDPolicy::MeaningfulAnalog(Previous, Value))
                HUD->ObserveKey(Event.GetKey());
            if ((FMath::Abs(Value)>0.30f || FMath::Abs(Previous)>0.30f) && CanObserve(Event.GetUserIndex()) && HUD->bCaptureBattleInput && HUD->HasGameInputFocus())
                return HUD->HandleBattleAnalog(Event.GetKey(),Previous,Value);
            return false;
        }
        virtual bool HandleMouseMoveEvent(FSlateApplication&, const FPointerEvent& Event) override
        {
            if (CanObserve(Event.GetUserIndex())) HUD->ObserveMouseMovement(Event.GetCursorDelta().SizeSquared());
            return false;
        }
        virtual bool HandleMouseButtonDownEvent(FSlateApplication&, const FPointerEvent& Event) override
        {
            if (CanObserve(Event.GetUserIndex())) HUD->ObserveKey(Event.GetEffectingButton());
            return false;
        }
    private:
        TWeakObjectPtr<UReditusBattleHUDWidget> HUD;
        TMap<FKey,float> PreviousAxes;
        bool CanObserve(uint32 UserIndex) const
        {
            if (!HUD.IsValid() || !HUD->IsInViewport()) return false;
            const ULocalPlayer* Player = HUD->GetOwningLocalPlayer();
            return !Player || Player->GetControllerId() < 0 || static_cast<uint32>(Player->GetControllerId()) == UserIndex;
        }
    };
}

UReditusBattleHUDButton::UReditusBattleHUDButton(const FObjectInitializer& Initializer) : Super(Initializer)
{
    IsFocusable = false;
}

void UReditusBattleHUDButton::Configure(UReditusBattleHUDWidget* OwnerHUD, EReditusHUDCommand InCommand, int32 InIndex)
{
    HUD = OwnerHUD;
    Command = InCommand;
    Index = InIndex;
    OnClicked.AddUniqueDynamic(this, &UReditusBattleHUDButton::Clicked);
    OnHovered.AddUniqueDynamic(this, &UReditusBattleHUDButton::Hovered);
}

void UReditusBattleHUDButton::Clicked() { if (IsValid(HUD)) HUD->HandleButton(Command, Index); }
void UReditusBattleHUDButton::Hovered() { if (IsValid(HUD)) HUD->HandleHover(Command, Index); }

UReditusBattleHUDWidget::UReditusBattleHUDWidget(const FObjectInitializer& Initializer) : Super(Initializer)
{
    bIsFocusable = true;
    ReadyUnitText=NSLOCTEXT("ReditusHUD","ReadyLabel","READY");
    ReadyUnitSwitchText=NSLOCTEXT("ReditusHUD","ReadyAllyLabel","READY ALLY");
    KeyboardBackKey=EKeys::B;
    GamepadBackKey=EKeys::Gamepad_FaceButton_Right;
    static ConstructorHelpers::FObjectFinderOptional<UFont> ExistingFont(
        TEXT("/Game/DialogueSystem/Font/final_fantasy_36_font_Font.final_fantasy_36_font_Font"));
    PixelFont = ExistingFont.Get();
    AmpIconWidgetClass=LoadClass<UUserWidget>(nullptr,TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/UI/AmpUIcon.AmpUIcon_C"));

    const auto Glyph = [this](FName Key, const TCHAR* Asset) {
        const FString Path = FString::Printf(TEXT("/Game/textures/ButtonIcons/Xbox/%s.%s"), Asset, Asset);
        if (UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *Path)) GlyphTextures.Add(Key, Texture);
    };
    Glyph(EKeys::Gamepad_FaceButton_Bottom.GetFName(), TEXT("XboxOne_A"));
    Glyph(EKeys::Gamepad_FaceButton_Right.GetFName(), TEXT("XboxOne_B"));
    Glyph(EKeys::Gamepad_FaceButton_Left.GetFName(), TEXT("XboxOne_X"));
    Glyph(EKeys::Gamepad_FaceButton_Top.GetFName(), TEXT("XboxOne_Y"));
    Glyph(EKeys::Gamepad_LeftShoulder.GetFName(), TEXT("XboxOne_LB"));
    Glyph(EKeys::Gamepad_RightShoulder.GetFName(), TEXT("XboxOne_RB"));
    Glyph(EKeys::Gamepad_LeftTrigger.GetFName(), TEXT("XboxOne_LT"));
    Glyph(EKeys::Gamepad_RightTrigger.GetFName(), TEXT("XboxOne_RT"));
    Glyph(EKeys::Gamepad_Special_Left.GetFName(), TEXT("XboxOne_Windows"));
    Glyph(EKeys::Gamepad_Special_Right.GetFName(), TEXT("XboxOne_Menu"));
    Glyph(EKeys::Gamepad_RightThumbstick.GetFName(), TEXT("XboxOne_Right_Stick"));
    KeyboardShortcuts.Add(EReditusHUDCommand::Attack, EKeys::One);
    KeyboardShortcuts.Add(EReditusHUDCommand::Tech, EKeys::Two);
    KeyboardShortcuts.Add(EReditusHUDCommand::Items, EKeys::Three);
    KeyboardShortcuts.Add(EReditusHUDCommand::Defend, EKeys::Four);
    KeyboardShortcuts.Add(EReditusHUDCommand::Overdrive, EKeys::Five);
    KeyboardShortcuts.Add(EReditusHUDCommand::Hack, EKeys::Six);
    KeyboardShortcuts.Add(EReditusHUDCommand::Run, EKeys::R);
    KeyboardShortcuts.Add(EReditusHUDCommand::ChangeParty, EKeys::Z);
    KeyboardShortcuts.Add(EReditusHUDCommand::ReadyAlly, EKeys::Seven);
}

FSlateFontInfo UReditusBattleHUDWidget::Font(int32 Size) const
{
    FSlateFontInfo Result = PixelFont ? FSlateFontInfo(PixelFont, Size) : FCoreStyle::GetDefaultFontStyle("Bold", Size);
    Result.OutlineSettings.OutlineSize = 1;
    Result.OutlineSettings.OutlineColor = FLinearColor::Black;
    return Result;
}

void UReditusBattleHUDWidget::InitializeSkin()
{
    if (GoldFrameBrush.GetResourceObject()) return;
    const auto BuildBrush=[this](const ReditusHUDSkin::Image& Artwork,FMargin SliceMargin) {
        FSlateBrush Result=FSlateColorBrush(Panel);
        UTexture2D* SkinTexture=UTexture2D::CreateTransient(Artwork.Width,Artwork.Height,PF_B8G8R8A8);
        if (!SkinTexture || !SkinTexture->PlatformData || SkinTexture->PlatformData->Mips.Num()==0) return Result;
        SkinTexture->SRGB=true;
        SkinTexture->NeverStream=true;
        SkinTexture->Filter=TF_Bilinear;
        SkinTexture->AddressX=TA_Clamp;
        SkinTexture->AddressY=TA_Clamp;
        SkinTexture->LODGroup=TEXTUREGROUP_UI;
        FTexture2DMipMap& SkinMip=SkinTexture->PlatformData->Mips[0];
        FColor* Destination=static_cast<FColor*>(SkinMip.BulkData.Lock(LOCK_READ_WRITE));
        if (!Destination) { SkinMip.BulkData.Unlock(); return Result; }
        for (int32 I=0; I<Artwork.Width*Artwork.Height; ++I)
        {
            const ReditusHUDSkin::Pixel& Ink=Artwork.Pixels[static_cast<std::size_t>(I)];
            Destination[I]=FColor(Ink.R,Ink.G,Ink.B,Ink.A);
        }
        SkinMip.BulkData.Unlock();
        SkinTexture->UpdateResource();
        SkinTextures.Add(SkinTexture);
        Result=FSlateBrush();
        Result.SetResourceObject(SkinTexture);
        Result.ImageSize=FVector2D(Artwork.Width,Artwork.Height);
        Result.DrawAs=ESlateBrushDrawType::Box;
        Result.Margin=SliceMargin;
        Result.TintColor=FSlateColor(FLinearColor::White);
        return Result;
    };
    GoldFrameBrush=BuildBrush(ReditusHUDSkin::Frame(),FMargin(1.f/6.f));
    SelectedFrameBrush=BuildBrush(ReditusHUDSkin::Frame(true),FMargin(1.f/6.f));
    SilverFrameBrush=BuildBrush(ReditusHUDSkin::Frame(false,true),FMargin(1.f/6.f));
    SelectionHaloBrush=BuildBrush(ReditusHUDSkin::Halo(),FMargin(1.f/6.f));
    GaugeBackgroundBrush=BuildBrush(ReditusHUDSkin::Bar(false),FMargin(0.125f,0.25f));
    GaugeFillBrush=BuildBrush(ReditusHUDSkin::Bar(true),FMargin(0.125f,0.25f));
    ResolvedAmpBrush=AmpImageBrush;
    if (!ResolvedAmpBrush.GetResourceObject() && AmpIconWidgetClass && GetOwningPlayer())
    {
        AmpIconTemplate=CreateWidget<UUserWidget>(GetOwningPlayer(),AmpIconWidgetClass);
        if (AmpIconTemplate && AmpIconTemplate->WidgetTree)
        {
            TArray<UWidget*> IconWidgets;
            AmpIconTemplate->WidgetTree->GetAllWidgets(IconWidgets);
            for (UWidget* IconWidget : IconWidgets)
            {
                UImage* ExistingImage=Cast<UImage>(IconWidget);
                if (ExistingImage && ExistingImage->Brush.GetResourceObject())
                {
                    ResolvedAmpBrush=ExistingImage->Brush;
                    ResolvedAmpBrush.TintColor=FSlateColor(ResolvedAmpBrush.TintColor.GetSpecifiedColor()*ExistingImage->ColorAndOpacity);
                    break;
                }
            }
        }
    }
    if (!ResolvedAmpBrush.GetResourceObject()) ResolvedAmpBrush=BuildBrush(ReditusHUDSkin::AmpPoint(),FMargin(0.f));
    ResolvedAmpBrush.DrawAs=ESlateBrushDrawType::Image;
    ResolvedAmpBrush.ImageSize=FVector2D(22.f,22.f);
}

void UReditusBattleHUDWidget::RefreshAmpImages(UHorizontalBox* IconHost,TArray<UImage*>& Images,int32 Total,int32 Committed)
{
    if (!IconHost) return;
    // A down-only ScaleBox on the card fits the existing art into narrow cards.
    // Keep every represented point; no cached layout geometry or resize tick.
    const int32 VisibleCount=FMath::Clamp(Total,0,8);
    if (Images.Num()!=VisibleCount)
    {
        IconHost->ClearChildren();
        Images.Reset();
        for (int32 I=0; I<VisibleCount; ++I)
        {
            UImage* Point=WidgetTree->ConstructWidget<UImage>();
            Point->SetBrush(ResolvedAmpBrush);
            Point->SetVisibility(ESlateVisibility::HitTestInvisible);
            USizeBox* PointSize=WidgetTree->ConstructWidget<USizeBox>();
            PointSize->SetWidthOverride(22.f);
            PointSize->SetHeightOverride(22.f);
            PointSize->AddChild(Point);
            UHorizontalBoxSlot* PointPlacement=IconHost->AddChildToHorizontalBox(PointSize);
            PointPlacement->SetPadding(FMargin(2.f,0.f));
            PointPlacement->SetVerticalAlignment(VAlign_Center);
            Images.Add(Point);
        }
    }
    for (int32 I=0; I<Images.Num(); ++I) Images[I]->SetRenderOpacity(I<Committed ? 1.f : 0.34f);
}

UTextBlock* UReditusBattleHUDWidget::MakeText(FText Value, int32 Size)
{
    UTextBlock* Result = WidgetTree->ConstructWidget<UTextBlock>();
    Result->SetText(Value);
    Result->SetFont(Font(Size));
    Result->SetColorAndOpacity(FSlateColor(FLinearColor::White));
    Result->SetShadowOffset(FVector2D(1.f,1.f));
    Result->SetShadowColorAndOpacity(FLinearColor(0.f,0.f,0.f,0.9f));
    Result->SetVisibility(ESlateVisibility::HitTestInvisible);
    return Result;
}

void UReditusBattleHUDWidget::Place(UCanvasPanel* Canvas, UWidget* Widget, float X, float Y, float W, float H)
{
    UCanvasPanelSlot* LayoutSlot = Canvas->AddChildToCanvas(Widget);
    LayoutSlot->SetPosition(FVector2D(X,Y));
    LayoutSlot->SetSize(FVector2D(W,H));
}

void UReditusBattleHUDWidget::PlaceStretchX(UCanvasPanel* Canvas, UWidget* Widget, float Left, float Y, float Right, float H, float MinAnchor, float MaxAnchor)
{
    UCanvasPanelSlot* LayoutSlot = Canvas->AddChildToCanvas(Widget);
    LayoutSlot->SetAnchors(FAnchors(MinAnchor,0.f,MaxAnchor,0.f));
    LayoutSlot->SetOffsets(FMargin(Left,Y,Right,H));
}

UBorder* UReditusBattleHUDWidget::MakeFrame(UWidget* Content, FMargin FramePadding)
{
    UBorder* Outer = WidgetTree->ConstructWidget<UBorder>();
    Outer->SetBrush(GoldFrameBrush);
    Outer->SetBrushColor(FLinearColor::White);
    Outer->SetPadding(FMargin(2.f));
    Outer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UBorder* Inner = WidgetTree->ConstructWidget<UBorder>();
    Inner->SetBrushColor(FLinearColor::Transparent);
    Inner->SetPadding(FramePadding);
    Inner->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    Inner->AddChild(Content);
    Outer->AddChild(Inner);
    return Outer;
}

UReditusBattleHUDWidget::FButtonView UReditusBattleHUDWidget::MakeButton(EReditusHUDCommand Command, FText Label, int32 Index)
{
    FButtonView View;
    const bool MenuEntry=Command==EReditusHUDCommand::SelectMenuEntry;
    View.Command = Command;
    View.Index = Index;
    View.Button = WidgetTree->ConstructWidget<UReditusBattleHUDButton>();
    View.Button->Configure(this, Command, Index);
    UHorizontalBox* Contents = WidgetTree->ConstructWidget<UHorizontalBox>();
    View.Glyph = WidgetTree->ConstructWidget<UImage>();
    View.Glyph->SetVisibility(ESlateVisibility::HitTestInvisible);
    const bool Compact = Command==EReditusHUDCommand::IncreaseAmp || Command==EReditusHUDCommand::DecreaseAmp ||
        Command==EReditusHUDCommand::Run || Command==EReditusHUDCommand::Block || Command==EReditusHUDCommand::Cancel ||
        Command==EReditusHUDCommand::ToggleAlternate || Command==EReditusHUDCommand::Overdrive;
    View.GlyphBox = WidgetTree->ConstructWidget<USizeBox>();
    View.GlyphBox->SetWidthOverride(Compact ? 24.f : 32.f);
    View.GlyphBox->SetHeightOverride(Compact ? 24.f : 32.f);
    View.GlyphBox->AddChild(View.Glyph);
    UHorizontalBoxSlot* GlyphSlot = Contents->AddChildToHorizontalBox(View.GlyphBox);
    GlyphSlot->SetVerticalAlignment(VAlign_Center);
    GlyphSlot->SetPadding(FMargin(MenuEntry ? 1.f : 4.f,0.f));
    View.Key = MakeText(FText::GetEmpty(), 15);
    Contents->AddChildToHorizontalBox(View.Key)->SetVerticalAlignment(VAlign_Center);
    if (MenuEntry)
    {
        View.Icon=WidgetTree->ConstructWidget<UImage>();
        View.Icon->SetVisibility(ESlateVisibility::HitTestInvisible);
        USizeBox* IconSize=WidgetTree->ConstructWidget<USizeBox>();
        IconSize->SetWidthOverride(26.f);
        IconSize->SetHeightOverride(26.f);
        IconSize->AddChild(View.Icon);
        UHorizontalBoxSlot* IconPlacement=Contents->AddChildToHorizontalBox(IconSize);
        IconPlacement->SetVerticalAlignment(VAlign_Center);
        IconPlacement->SetPadding(FMargin(2.f,0.f));
    }
    View.Label = MakeText(Label, Compact ? 17 : Command == EReditusHUDCommand::SelectMenuEntry ? 18 : 20);
    UHorizontalBoxSlot* LabelSlot = Contents->AddChildToHorizontalBox(View.Label);
    LabelSlot->SetPadding(FMargin(MenuEntry ? 3.f : 8.f,0.f));
    LabelSlot->SetVerticalAlignment(VAlign_Center);
    if (MenuEntry)
    {
        LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        View.Label->SetAutoWrapText(true);
        View.Label->SetClipping(EWidgetClipping::ClipToBounds);
        View.Detail = MakeText(FText::GetEmpty(),16);
        View.Detail->SetJustification(ETextJustify::Right);
        USizeBox* DetailSize = WidgetTree->ConstructWidget<USizeBox>();
        View.DetailBox=DetailSize;
        DetailSize->SetMinDesiredWidth(28.f);
        DetailSize->AddChild(View.Detail);
        Contents->AddChildToHorizontalBox(DetailSize)->SetVerticalAlignment(VAlign_Center);
    }
    UButtonSlot* ContentPlacement=CastChecked<UButtonSlot>(View.Button->AddChild(Contents));
    if (MenuEntry) ContentPlacement->SetHorizontalAlignment(HAlign_Fill);
    FButtonStyle Style = View.Button->WidgetStyle;
    Style.Normal = Command==EReditusHUDCommand::CycleBattleSpeed ? GoldFrameBrush : SilverFrameBrush;
    Style.Hovered = SelectedFrameBrush;
    Style.Pressed = SelectedFrameBrush;
    Style.Disabled = SilverFrameBrush;
    Style.NormalPadding=FMargin(MenuEntry ? 3.f : 8.f,3.f);
    Style.PressedPadding=Style.NormalPadding;
    View.Button->SetStyle(Style);
    return View;
}

TSharedRef<SWidget> UReditusBattleHUDWidget::RebuildWidget()
{
    if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this, TEXT("NativeHUDTree"));
    BuildLayout();
    return Super::RebuildWidget();
}

void UReditusBattleHUDWidget::BuildLayout()
{
    InitializeSkin();
    Cards.Reset(); Buttons.Reset(); ListButtons.Reset(); UtilityButtons.Reset();
    // Use the actual player viewport. A fixed 16:9 ScaleBox left unused side
    // margins in shorter windows and lifted the HUD above the viewport bottom.
    DesignCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();
    DesignCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    WidgetTree->RootWidget = DesignCanvas;

    TargetName = MakeText(TargetLabel,25);
    UBorder* TargetFrame = MakeFrame(TargetName,FMargin(14.f,6.f));
    TargetFrame->SetVisibility(ESlateVisibility::HitTestInvisible);
    UCanvasPanelSlot* TargetPlacement=DesignCanvas->AddChildToCanvas(TargetFrame);
    TargetPlacement->SetPosition(FVector2D(16.f,16.f));
    TargetPlacement->SetAutoSize(true);

    UHorizontalBox* ActionContent=WidgetTree->ConstructWidget<UHorizontalBox>();
    ActionElement=WidgetTree->ConstructWidget<UImage>();
    ActionElement->SetVisibility(ESlateVisibility::HitTestInvisible);
    ActionElementBox=WidgetTree->ConstructWidget<USizeBox>();
    ActionElementBox->SetWidthOverride(36.f);
    ActionElementBox->SetHeightOverride(36.f);
    UBorder* ActionIconFrame=WidgetTree->ConstructWidget<UBorder>();
    ActionIconFrame->SetBrush(SilverFrameBrush);
    ActionIconFrame->SetBrushColor(FLinearColor::White);
    ActionIconFrame->SetPadding(FMargin(4.f));
    ActionIconFrame->SetVisibility(ESlateVisibility::HitTestInvisible);
    ActionIconFrame->AddChild(ActionElement);
    ActionElementBox->AddChild(ActionIconFrame);
    UHorizontalBoxSlot* ElementPlacement=ActionContent->AddChildToHorizontalBox(ActionElementBox);
    ElementPlacement->SetVerticalAlignment(VAlign_Center);
    ElementPlacement->SetPadding(FMargin(0.f,0.f,10.f,0.f));
    ActionName=MakeText(FText::GetEmpty(),25);
    ActionName->SetColorAndOpacity(FSlateColor(Gold));
    ActionContent->AddChildToHorizontalBox(ActionName)->SetVerticalAlignment(VAlign_Center);
    ActionFrame=MakeFrame(ActionContent,FMargin(14.f,6.f));
    UCanvasPanelSlot* ActionPlacement=DesignCanvas->AddChildToCanvas(ActionFrame);
    ActionPlacement->SetAnchors(FAnchors(0.5f,0.f));
    ActionPlacement->SetAlignment(FVector2D(0.5f,0.f));
    ActionPlacement->SetPosition(FVector2D(0.f,16.f));
    ActionPlacement->SetAutoSize(true);

    FButtonView Speed = MakeButton(EReditusHUDCommand::CycleBattleSpeed,Text(TEXT("BATTLE SPEED 1x")));
    SpeedText = Speed.Label;
    UtilityButtons.Add(Speed);
    Place(DesignCanvas,Speed.Button,-326.f,16.f,310.f,55.f);
    CastChecked<UCanvasPanelSlot>(Speed.Button->Slot)->SetAnchors(FAnchors(1.f,0.f));

    const float SideMargin = FMath::Max(0.f,BottomHUDSideMargin);
    const float BottomInset = FMath::Max(0.f,BottomHUDInset);
    const float PartyShare = FMath::Clamp(PartyWidthFraction,0.55f,0.70f);
    UCanvasPanel* Bottom = WidgetTree->ConstructWidget<UCanvasPanel>();
    Bottom->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UBorder* BottomFrame = MakeFrame(Bottom,FMargin(4.f));
    UCanvasPanelSlot* BottomPlacement = DesignCanvas->AddChildToCanvas(BottomFrame);
    BottomPlacement->SetAnchors(FAnchors(0.f,1.f,1.f,1.f));
    BottomPlacement->SetOffsets(FMargin(SideMargin,-BottomPanelHeight-BottomInset,SideMargin,BottomPanelHeight));

    PartyCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();
    PartyCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    PlaceStretchX(Bottom,PartyCanvas,0.f,0.f,12.f,204.f,0.f,PartyShare);
    CommandRegion = WidgetTree->ConstructWidget<UCanvasPanel>();
    CommandRegion->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    PlaceStretchX(Bottom,CommandRegion,0.f,0.f,0.f,212.f,PartyShare,1.f);
    const EReditusHUDCommand AmpCommands[] = {EReditusHUDCommand::DecreaseAmp,EReditusHUDCommand::IncreaseAmp};
    const TCHAR* AmpLabels[] = {TEXT("- AMP"),TEXT("+ AMP")};
    for (int32 I=0; I<2; ++I)
    {
        FButtonView Button = MakeButton(AmpCommands[I],Text(AmpLabels[I]));
        UtilityButtons.Add(Button);
        Place(CommandRegion,Button.Button,I*132.f,0.f,126.f,32.f);
    }
    FButtonView Overdrive=MakeButton(EReditusHUDCommand::Overdrive,Text(TEXT("OVERDRIVE")));
    UtilityButtons.Add(Overdrive);
    Place(CommandRegion,Overdrive.Button,-262.f,0.f,262.f,32.f);
    CastChecked<UCanvasPanelSlot>(Overdrive.Button->Slot)->SetAnchors(FAnchors(1.f,0.f));

    CommandCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();
    CommandCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    PlaceStretchX(CommandRegion,CommandCanvas,0.f,38.f,0.f,174.f);
    for (int32 I=0; I<8; ++I)
    {
        const EReditusHUDCommand Command = CommandOrder[I];
        FButtonView Button = MakeButton(Command,Command==EReditusHUDCommand::ReadyAlly ? ReadyUnitSwitchText : CommandLabel(Command));
        Buttons.Add(Button);
        const int32 Face=I%4;
        const float LeftEdge=Face==1 ? 0.f : Face==2 ? 0.60f : 0.30f;
        const float RightEdge=LeftEdge+0.40f;
        const float Y=Face==3 ? 0.f : Face==0 ? 112.f : 56.f;
        PlaceStretchX(CommandCanvas,Button.Button,2.f,Y,2.f,46.f,LeftEdge,RightEdge);
    }
    FButtonView More=MakeButton(EReditusHUDCommand::ToggleAlternate,Text(TEXT("HOLD\nMORE")));
    More.Label->SetFont(Font(13));
    UtilityButtons.Add(More);
    PlaceStretchX(CommandCanvas,More.Button,2.f,54.f,2.f,50.f,0.40f,0.60f);

    // The list fills its host: two scrolling entry columns and a description column.
    MenuHost = WidgetTree->ConstructWidget<UOverlay>();
    MenuHost->SetVisibility(ESlateVisibility::Collapsed);
    PlaceStretchX(CommandRegion,MenuHost,0.f,0.f,0.f,212.f);
    ListCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();
    ListCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UOverlaySlot* ListPlacement=MenuHost->AddChildToOverlay(ListCanvas);
    ListPlacement->SetHorizontalAlignment(HAlign_Fill);
    ListPlacement->SetVerticalAlignment(VAlign_Fill);
    MenuHeader = MakeText(FText::GetEmpty(),24);
    MenuHeader->SetColorAndOpacity(FSlateColor(Gold));
    PlaceStretchX(ListCanvas,MenuHeader,2.f,0.f,6.f,30.f,0.f,0.66f);
    EntryScroll=WidgetTree->ConstructWidget<UScrollBox>();
    EntryScroll->SetOrientation(Orient_Vertical);
    EntryScroll->SetScrollBarVisibility(ESlateVisibility::Visible);
    EntryScroll->SetClipping(EWidgetClipping::ClipToBounds);
    PlaceStretchX(ListCanvas,EntryScroll,0.f,34.f,6.f,176.f,0.f,0.66f);
    EntryRows=WidgetTree->ConstructWidget<UVerticalBox>();
    UScrollBoxSlot* RowsPlacement=CastChecked<UScrollBoxSlot>(EntryScroll->AddChild(EntryRows));
    RowsPlacement->SetHorizontalAlignment(HAlign_Fill);
    RowsPlacement->SetVerticalAlignment(VAlign_Top);
    UTextBlock* DescriptionHeading=MakeText(Text(TEXT("DESCRIPTION")),18);
    DescriptionHeading->SetColorAndOpacity(FSlateColor(Gold));
    PlaceStretchX(ListCanvas,DescriptionHeading,12.f,2.f,4.f,28.f,0.66f,1.f);
    MenuDescription = MakeText(FText::GetEmpty(),ListDescriptionSize);
    MenuDescription->SetAutoWrapText(true);
    UScrollBox* DescriptionScroll=WidgetTree->ConstructWidget<UScrollBox>();
    DescriptionScroll->SetOrientation(Orient_Vertical);
    DescriptionScroll->SetClipping(EWidgetClipping::ClipToBounds);
    UScrollBoxSlot* DescriptionPlacement=CastChecked<UScrollBoxSlot>(DescriptionScroll->AddChild(MenuDescription));
    DescriptionPlacement->SetHorizontalAlignment(HAlign_Fill);
    PlaceStretchX(ListCanvas,DescriptionScroll,12.f,34.f,4.f,176.f,0.66f,1.f);
    for (const float DividerPosition : {0.33f,0.66f})
    {
        UBorder* Divider=WidgetTree->ConstructWidget<UBorder>();
        Divider->SetBrushColor(FLinearColor(Gold.R,Gold.G,Gold.B,0.30f));
        Divider->SetPadding(FMargin(0.f));
        Divider->SetVisibility(ESlateVisibility::HitTestInvisible);
        Place(ListCanvas,Divider,0.f,34.f,1.f,176.f);
        CastChecked<UCanvasPanelSlot>(Divider->Slot)->SetAnchors(FAnchors(DividerPosition,0.f));
    }
    if (HostedMenu) MenuHost->AddChildToOverlay(HostedMenu);
    Party.SetNum(ReditusHUDPolicy::MaxActiveUnits);
    BuildCards();
    Refresh();
}

void UReditusBattleHUDWidget::BuildCards()
{
    if (!PartyCanvas) return;
    PartyCanvas->ClearChildren();
    Cards.Reset();
    const int32 Count = ReditusHUDPolicy::MaxActiveUnits;
    const auto Bar = [this](FLinearColor Color) {
        UProgressBar* Result = WidgetTree->ConstructWidget<UProgressBar>();
        FProgressBarStyle Style;
        Style.BackgroundImage = GaugeBackgroundBrush;
        Style.FillImage = GaugeFillBrush;
        Result->WidgetStyle = Style;
        Result->SetFillColorAndOpacity(Color);
        Result->SetVisibility(ESlateVisibility::HitTestInvisible);
        return Result;
    };
    for (int32 I=0; I<Count; ++I)
    {
        const float LeftEdge=static_cast<float>(I)/Count;
        const float RightEdge=static_cast<float>(I+1)/Count;
        const float LeftInset=I==0 ? 0.f : 4.f;
        const float RightInset=I==Count-1 ? 0.f : 4.f;
        FCard Card;
        Card.SelectionGlow=WidgetTree->ConstructWidget<UImage>();
        Card.SelectionGlow->SetBrush(SelectionHaloBrush);
        Card.SelectionGlow->SetVisibility(ESlateVisibility::HitTestInvisible);
        PlaceStretchX(PartyCanvas,Card.SelectionGlow,LeftInset-4.f,-4.f,RightInset-4.f,212.f,LeftEdge,RightEdge);
        UCanvasPanel* Content = WidgetTree->ConstructWidget<UCanvasPanel>();
        Card.Content=Content;
        Content->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        Card.Frame = MakeFrame(Content,FMargin(5.f));
        PlaceStretchX(PartyCanvas,Card.Frame,LeftInset,0.f,RightInset,204.f,LeftEdge,RightEdge);
        UReditusBattleHUDButton* Select = WidgetTree->ConstructWidget<UReditusBattleHUDButton>();
        Card.Select=Select;
        Select->Configure(this,EReditusHUDCommand::SelectPartyCard,I);
        FButtonStyle Transparent = Select->WidgetStyle;
        Transparent.Normal = FSlateColorBrush(FLinearColor::Transparent);
        Transparent.Hovered = FSlateColorBrush(FLinearColor(1.f,1.f,1.f,0.05f));
        Transparent.Pressed = FSlateColorBrush(FLinearColor(1.f,1.f,1.f,0.10f));
        Transparent.Disabled = FSlateColorBrush(FLinearColor::Transparent);
        Select->SetStyle(Transparent);
        PlaceStretchX(Content,Select,0.f,0.f,0.f,190.f);
        Card.Portrait = WidgetTree->ConstructWidget<UImage>();
        Card.Portrait->SetVisibility(ESlateVisibility::HitTestInvisible);
        UScaleBox* PortraitScale = WidgetTree->ConstructWidget<UScaleBox>();
        PortraitScale->SetStretch(EStretch::ScaleToFit);
        PortraitScale->SetVisibility(ESlateVisibility::HitTestInvisible);
        PortraitScale->AddChild(Card.Portrait);
        UBorder* PortraitSurround=MakeFrame(PortraitScale,FMargin(3.f));
        PortraitSurround->SetBrush(SilverFrameBrush);
        Place(Content,PortraitSurround,2.f,33.f,88.f,130.f);
        Card.Name = MakeText(FText::GetEmpty(),22);
        Card.Name->SetColorAndOpacity(FSlateColor(Gold));
        Card.Name->SetClipping(EWidgetClipping::ClipToBounds);
        PlaceStretchX(Content,Card.Name,8.f,0.f,106.f,29.f);
        Card.ReadyLabel=MakeText(FText::GetEmpty(),16);
        Card.ReadyLabel->SetColorAndOpacity(FSlateColor(Gold));
        Card.ReadyLabel->SetJustification(ETextJustify::Right);
        Place(Content,Card.ReadyLabel,-106.f,2.f,94.f,25.f);
        CastChecked<UCanvasPanelSlot>(Card.ReadyLabel->Slot)->SetAnchors(FAnchors(1.f,0.f));
        const float X = 94.f;
        Card.HPBar = Bar(FLinearColor(0.f,0.85f,0.28f,1.f));
        Card.MPBar = Bar(FLinearColor(0.f,0.40f,0.95f,1.f));
        Card.ATBBar = Bar(FLinearColor(1.f,0.72f,0.06f,1.f));
        Card.ODBar = Bar(FLinearColor(0.65f,0.15f,1.f,1.f));
        UProgressBar* Bars[] = {Card.HPBar,Card.MPBar,Card.ATBBar,Card.ODBar};
        const TCHAR* Labels[] = {TEXT("HP"),TEXT("MP"),TEXT("ATB"),TEXT("OD")};
        for (int32 J=0; J<4; ++J)
        {
            Place(Content,MakeText(Text(Labels[J]),15),X,39.f+J*31.f,38.f,20.f);
            PlaceStretchX(Content,Bars[J],X+39.f,42.f+J*31.f,8.f,18.f);
        }
        Card.HP = MakeText(FText::GetEmpty(),15);
        Card.MP = MakeText(FText::GetEmpty(),15);
        Card.ATB = MakeText(FText::GetEmpty(),15);
        Card.OD = MakeText(FText::GetEmpty(),15);
        UTextBlock* Values[] = {Card.HP,Card.MP,Card.ATB,Card.OD};
        for (int32 J=0; J<4; ++J)
        {
            Values[J]->SetJustification(ETextJustify::Right);
            PlaceStretchX(Content,Values[J],X+39.f,40.f+J*31.f,8.f,20.f);
        }
        Card.Amp = MakeText(FText::GetEmpty(),15);
        Place(Content,Card.Amp,94.f,169.f,82.f,23.f);
        Card.AmpIcons=WidgetTree->ConstructWidget<UHorizontalBox>();
        Card.AmpIcons->SetVisibility(ESlateVisibility::HitTestInvisible);
        Card.AmpIcons->SetClipping(EWidgetClipping::ClipToBounds);
        UScaleBox* AmpScale=WidgetTree->ConstructWidget<UScaleBox>();
        AmpScale->SetStretch(EStretch::ScaleToFit);
        AmpScale->SetStretchDirection(EStretchDirection::DownOnly);
        AmpScale->SetVisibility(ESlateVisibility::HitTestInvisible);
        UScaleBoxSlot* AmpPlacement=CastChecked<UScaleBoxSlot>(AmpScale->AddChild(Card.AmpIcons));
        AmpPlacement->SetHorizontalAlignment(HAlign_Left);
        AmpPlacement->SetVerticalAlignment(VAlign_Center);
        PlaceStretchX(Content,AmpScale,176.f,168.f,8.f,24.f);
        Cards.Add(Card);
    }
}

void UReditusBattleHUDWidget::NativeConstruct()
{
    Super::NativeConstruct();
    if (!InputObserver.IsValid() && FSlateApplication::IsInitialized())
    {
        InputObserver = MakeShared<FHUDInputObserver>(this);
        FSlateApplication::Get().RegisterInputPreProcessor(InputObserver,0);
    }
    Refresh();
}

void UReditusBattleHUDWidget::NativeDestruct()
{
    if (InputObserver.IsValid() && FSlateApplication::IsInitialized())
        FSlateApplication::Get().UnregisterInputPreProcessor(InputObserver);
    InputObserver.Reset();
    Super::NativeDestruct();
}

const FReditusBattleHUDUnit* UReditusBattleHUDWidget::Selected() const
{
    return Party.IsValidIndex(SelectedIndex) ? &Party[SelectedIndex] : nullptr;
}

AActor* UReditusBattleHUDWidget::GetSelectedUnit() const
{
    const FReditusBattleHUDUnit* Data = Selected();
    return Data && IsValid(Data->Unit) ? Data->Unit : nullptr;
}

void UReditusBattleHUDWidget::SetPartySnapshot(const TArray<FReditusBattleHUDUnit>& Units)
{
    AActor* Previous = GetSelectedUnit();
    Party.SetNum(ReditusHUDPolicy::MaxActiveUnits);
    // Inputs are display positions, not a compact living-actor array. Keep KO
    // snapshots whose actor has been destroyed, and leave unused slots blank.
    TArray<FReditusBattleHUDUnit> Snapshot=Units;
    for (int32 I=0; I<Party.Num(); ++I)
    {
        Party[I]=Snapshot.IsValidIndex(I) ? Snapshot[I] : FReditusBattleHUDUnit();
    }
    SelectedIndex = Party.IndexOfByPredicate([Previous](const FReditusBattleHUDUnit& Data) {
        return Previous && Data.Unit==Previous && IsValid(Data.Unit) && Data.HP>0.f;
    });
    if (SelectedIndex == INDEX_NONE)
    {
        SelectedIndex = Party.IndexOfByPredicate([](const FReditusBattleHUDUnit& Data) {
            return IsValid(Data.Unit) && Data.HP > 0.f && Data.bReadyForCommands;
        });
        if (SelectedIndex == INDEX_NONE)
            SelectedIndex=Party.IndexOfByPredicate([](const FReditusBattleHUDUnit& Data) { return IsValid(Data.Unit) && Data.HP>0.f; });
    }
    if (IsMenuOpen())
    {
        const FReditusBattleHUDUnit* Owner=Selected();
        if (!Owner || Owner->Unit!=MenuOwner || !IsValid(MenuOwner) || Owner->HP<=0.f)
        {
            AActor* PreviousOwner=MenuOwner;
            CloseListMenu();
            OnMenuCancelled.Broadcast(PreviousOwner);
        }
    }
    if (Cards.Num()!=ReditusHUDPolicy::MaxActiveUnits) BuildCards();
    Refresh();
    if (Previous != GetSelectedUnit()) OnSelectedUnitChanged.Broadcast(GetSelectedUnit());
}

bool UReditusBattleHUDWidget::SelectReadyUnit(AActor* Unit)
{
    const int32 Index = Party.IndexOfByPredicate([Unit](const FReditusBattleHUDUnit& Data) {
        return Data.Unit == Unit && IsValid(Unit) && Data.HP>0.f && Data.bCanAct && Data.bReadyForCommands;
    });
    if (Index == INDEX_NONE || IsMenuOpen()) return false;
    AActor* Previous = GetSelectedUnit();
    SelectedIndex = Index;
    Refresh();
    if (Previous != Unit) OnSelectedUnitChanged.Broadcast(Unit);
    return true;
}

void UReditusBattleHUDWidget::SetCommandsEnabled(bool bEnabled)
{
    const bool WasEnabled = bCommandsEnabled;
    bCommandsEnabled = bEnabled;
    if (!bEnabled && !IsMenuOpen()) SetAlternateCommands(false);
    Refresh();
    if (!WasEnabled && bEnabled && bAutoFocusCommandPanel && !IsMenuOpen()) FocusCommands();
}

void UReditusBattleHUDWidget::SetReactionWindow(bool bEnabled)
{
    if (bReactionWindow == bEnabled) return;
    bReactionWindow = bEnabled;
    Refresh();
}

void UReditusBattleHUDWidget::SetTargetSelectionActive(bool bEnabled)
{
    if (bTargetSelectionActive==bEnabled) return;
    bTargetSelectionActive=bEnabled;
    if (bEnabled)
    {
        SetAlternateCommands(false);
        SelectedCommand=0;
    }
    Refresh();
}

void UReditusBattleHUDWidget::SetTargetedEnemy(AActor* Enemy)
{
    FText Name;
    if (IsValid(Enemy))
    {
        if (const FTextProperty* Property = FindFProperty<FTextProperty>(Enemy->GetClass(),TEXT("title")))
            Name = Property->GetPropertyValue_InContainer(Enemy);
    }
    SetTargetLabel(Name);
}

void UReditusBattleHUDWidget::SetTargetLabel(FText Label)
{
    TargetLabel = Label;
    if (TargetName)
    {
        TargetName->SetText(Label);
        if (UWidget* Parent = TargetName->GetParent())
        {
            if (UWidget* Frame = Parent->GetParent()) Frame->SetVisibility(Label.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
        }
    }
}

void UReditusBattleHUDWidget::ClearTargetLabel() { SetTargetLabel(FText::GetEmpty()); }

void UReditusBattleHUDWidget::SetActionAnnouncement(FText Label,const FSlateBrush& ElementIcon,bool bVisible)
{
    ActionLabel=Label;
    ActionElementBrush=ElementIcon;
    bActionVisible=bVisible && !Label.IsEmpty();
    if (ActionName) ActionName->SetText(ActionLabel);
    if (ActionElement) ActionElement->SetBrush(ActionElementBrush);
    if (ActionElementBox) ActionElementBox->SetVisibility(ActionElementBrush.GetResourceObject() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    if (ActionFrame) ActionFrame->SetVisibility(bActionVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void UReditusBattleHUDWidget::ClearActionAnnouncement()
{
    SetActionAnnouncement(FText::GetEmpty(),FSlateBrush(),false);
}

void UReditusBattleHUDWidget::SetDisplayedBattleSpeed(float Multiplier)
{
    DisplayedSpeed = FMath::IsFinite(Multiplier) && Multiplier > 0.f ? Multiplier : 1.f;
    if (SpeedText) SpeedText->SetText(FText::Format(NSLOCTEXT("ReditusHUD","Speed","BATTLE SPEED {0}x"),FText::AsNumber(DisplayedSpeed)));
}

void UReditusBattleHUDWidget::SetInputDevice(EReditusHUDInputDevice Device)
{
    if (InputDevice == Device) return;
    InputDevice = Device;
    Refresh();
    OnInputDeviceChanged.Broadcast(Device);
}

void UReditusBattleHUDWidget::ObserveKey(const FKey& Key)
{
    if (bAutomaticGlyphSwitching) SetInputDevice(Key.IsGamepadKey() ? EReditusHUDInputDevice::Xbox : EReditusHUDInputDevice::KeyboardMouse);
}

void UReditusBattleHUDWidget::ObserveMouseMovement(float DistanceSquared)
{
    if (bAutomaticGlyphSwitching && DistanceSquared > 4.f) SetInputDevice(EReditusHUDInputDevice::KeyboardMouse);
}

FText UReditusBattleHUDWidget::CommandLabel(EReditusHUDCommand Command)
{
    switch (Command)
    {
    case EReditusHUDCommand::Attack: return Text(TEXT("ATTACK"));
    case EReditusHUDCommand::Tech: return Text(TEXT("SKILL"));
    case EReditusHUDCommand::Items: return Text(TEXT("ITEM"));
    case EReditusHUDCommand::Defend: return Text(TEXT("DEFEND"));
    case EReditusHUDCommand::Overdrive: return Text(TEXT("OVERDRIVE"));
    case EReditusHUDCommand::Hack: return Text(TEXT("HACK"));
    case EReditusHUDCommand::ChangeParty: return Text(TEXT("CHANGE"));
    case EReditusHUDCommand::Run: return Text(TEXT("RUN"));
    case EReditusHUDCommand::ReadyAlly: return Text(TEXT("READY ALLY"));
    case EReditusHUDCommand::ToggleAlternate: return Text(TEXT("MORE"));
    default: return FText::GetEmpty();
    }
}

bool UReditusBattleHUDWidget::MatchesAction(FName Action, const FKey& Key) const
{
    TArray<FInputActionKeyMapping> Mappings;
    if (const APlayerController* PC=GetOwningPlayer())
        if (PC->PlayerInput) Mappings=PC->PlayerInput->GetKeysForAction(Action);
    if (Mappings.Num()==0) GetDefault<UInputSettings>()->GetActionMappingByName(Action,Mappings);
    return Mappings.ContainsByPredicate([Key](const FInputActionKeyMapping& Mapping) { return Mapping.Key == Key; });
}

FKey UReditusBattleHUDWidget::KeyForAction(FName Action) const
{
    TArray<FInputActionKeyMapping> Mappings;
    if (const APlayerController* PC=GetOwningPlayer())
        if (PC->PlayerInput) Mappings=PC->PlayerInput->GetKeysForAction(Action);
    if (Mappings.Num()==0) GetDefault<UInputSettings>()->GetActionMappingByName(Action,Mappings);
    for (const FInputActionKeyMapping& Mapping : Mappings)
        if (Mapping.Key.IsGamepadKey() == (InputDevice == EReditusHUDInputDevice::Xbox)) return Mapping.Key;
    return FKey();
}

FKey UReditusBattleHUDWidget::KeyForCommand(EReditusHUDCommand Command) const
{
    const bool Gamepad=InputDevice==EReditusHUDInputDevice::Xbox;
    if (bTargetSelectionActive && Command==EReditusHUDCommand::Attack)
        return Gamepad ? GamepadKeys.Bottom : KeyForAction(TEXT("MenuConfirm"));
    if (Gamepad)
    {
        for (int32 I=0; I<8; ++I) if (CommandOrder[I]==Command)
        {
            const FKey Faces[] = {GamepadKeys.Bottom,GamepadKeys.Left,GamepadKeys.Right,GamepadKeys.Top};
            return Faces[I%4];
        }
    }
    else if (bAlternateCommands)
    {
        for (int32 I=4; I<8; ++I) if (CommandOrder[I]==Command)
            if (const FKey* Key=KeyboardShortcuts.Find(CommandOrder[I%4])) return *Key;
    }
    switch (Command)
    {
    case EReditusHUDCommand::IncreaseAmp: return Gamepad ? GamepadKeys.IncreaseAmp : KeyForAction(InputActions.IncreaseAmp);
    case EReditusHUDCommand::DecreaseAmp: return Gamepad ? GamepadKeys.DecreaseAmp : KeyForAction(InputActions.DecreaseAmp);
    case EReditusHUDCommand::Block: return Gamepad ? GamepadKeys.Block : KeyForAction(InputActions.Block);
    case EReditusHUDCommand::CycleBattleSpeed: return KeyForAction(InputActions.Speed);
    case EReditusHUDCommand::Cancel: return Gamepad ? GamepadBackKey : KeyboardBackKey;
    case EReditusHUDCommand::ToggleAlternate: return Gamepad ? GamepadKeys.More : KeyboardMoreKey;
    case EReditusHUDCommand::SelectMenuEntry: return Gamepad ? GamepadKeys.Bottom : EKeys::Enter;
    case EReditusHUDCommand::Overdrive:
        if (Gamepad) return GamepadKeys.Overdrive;
        break;
    default: break;
    }
    if (const FKey* Key=KeyboardShortcuts.Find(Command)) return *Key;
    return FKey();
}

bool UReditusBattleHUDWidget::RouteGameplayInput(FName Action, FKey Key)
{
    if (GameplayInputRouter.IsBound() && GameplayInputRouter.Execute(Action,Key)) return true;
    if (!OnGameplayInputRequested.IsBound()) return false;
    OnGameplayInputRequested.Broadcast(Action,Key);
    return true;
}

bool UReditusBattleHUDWidget::CanExecuteCommand(EReditusHUDCommand Command) const
{
    if (bInputSuspended || UGameplayStatics::IsGamePaused(this)) return false;
    if (Command==EReditusHUDCommand::CycleBattleSpeed) return true;
    if (Command==EReditusHUDCommand::Block) return IsValid(GetOwningPlayer());
    if (Command==EReditusHUDCommand::Cancel) return IsMenuOpen() || IsValid(GetSelectedUnit());
    if (Command==EReditusHUDCommand::ToggleAlternate) return bCommandsEnabled && !IsMenuOpen();
    const FReditusBattleHUDUnit* Data=Selected();
    if (bTargetSelectionActive && !IsMenuOpen())
        return Command==EReditusHUDCommand::Attack && Data && IsValid(Data->Unit) && Data->HP>0.f && Data->bCanAct;
    const bool Allowed=Data && !Data->bQueued && ReditusHUDPolicy::CanCommand(IsValid(Data->Unit),Data->HP,Data->bCanAct,Data->bReadyForCommands,bCommandsEnabled);
    if (!Allowed) return false;
    if (IsMenuOpen() && Command!=EReditusHUDCommand::IncreaseAmp && Command!=EReditusHUDCommand::DecreaseAmp) return false;
    switch (Command)
    {
    case EReditusHUDCommand::Overdrive: return ReditusHUDPolicy::CanOverdrive(Allowed,Data->bCanOverdrive,Data->bOverdriveEntered);
    case EReditusHUDCommand::Hack: return Data->bCanHack;
    case EReditusHUDCommand::ChangeParty: return bAllowPartyChanges;
    case EReditusHUDCommand::Run: return bAllowRun;
    case EReditusHUDCommand::IncreaseAmp: return Data->CommittedAmp<Data->TotalAmp;
    case EReditusHUDCommand::DecreaseAmp: return Data->CommittedAmp>0;
    case EReditusHUDCommand::ReadyAlly:
    {
        int32 Count=0;
        for (const FReditusBattleHUDUnit& Entry : Party)
            if (IsValid(Entry.Unit) && Entry.HP>0.f && Entry.bCanAct && Entry.bReadyForCommands && !Entry.bQueued) ++Count;
        return Count>1;
    }
    default: return true;
    }
}

bool UReditusBattleHUDWidget::RequestCommand(EReditusHUDCommand Command)
{
    if (!CanExecuteCommand(Command))
    {
        LastInputStatus=FString::Printf(TEXT("Unavailable: %s; commands=%d owner=%s"),*CommandLabel(Command).ToString(),bCommandsEnabled,*GetNameSafe(GetSelectedUnit()));
        if (bLogInput) UE_LOG(LogTemp,Log,TEXT("Battle HUD: %s"),*LastInputStatus);
        return false;
    }
    if (bTargetSelectionActive && Command==EReditusHUDCommand::Attack && !IsMenuOpen())
    {
        AActor* ConfirmingUnit=GetSelectedUnit();
        const bool Routed=RouteGameplayInput(TEXT("MenuConfirm"),KeyForCommand(Command));
        LastInputStatus=FString::Printf(TEXT("Target confirm %s on %s"),Routed ? TEXT("dispatched") : TEXT("has no route"),*GetNameSafe(ConfirmingUnit));
        if (bLogInput) UE_LOG(LogTemp,Log,TEXT("Battle HUD: %s"),*LastInputStatus);
        return Routed;
    }
    if (Command==EReditusHUDCommand::ToggleAlternate) { SetAlternateCommands(!bAlternateCommands); return true; }
    if (Command==EReditusHUDCommand::ReadyAlly)
    {
        if (!ReadyUnitSwitchAction.IsNone() && RouteGameplayInput(ReadyUnitSwitchAction,KeyForCommand(Command))) return true;
        return CycleReadyAlly();
    }
    for (int32 I=0; I<Buttons.Num(); ++I)
        if (Buttons[I].Command==Command && (I>=4)==bAlternateCommands) SelectedCommand=I;
    if (Command == EReditusHUDCommand::Cancel && IsMenuOpen()) { CancelListMenu(); return true; }
    FName Action;
    if (Command == EReditusHUDCommand::Block) Action = InputActions.Block;
    else if (Command == EReditusHUDCommand::IncreaseAmp) Action = InputActions.IncreaseAmp;
    else if (Command == EReditusHUDCommand::DecreaseAmp) Action = InputActions.DecreaseAmp;
    else if (Command == EReditusHUDCommand::ChangeParty) Action = InputActions.ChangeParty;
    else if (Command == EReditusHUDCommand::CycleBattleSpeed) Action = InputActions.Speed;
    else if (Command == EReditusHUDCommand::Cancel) Action = TEXT("MenuCancel");
    if (!Action.IsNone() && RouteGameplayInput(Action,KeyForCommand(Command))) return true;
    bool Routed = CommandRouter.IsBound() && CommandRouter.Execute(GetSelectedUnit(),Command);
    if (!Routed && OnCommandRequested.IsBound())
    {
        OnCommandRequested.Broadcast(GetSelectedUnit(),Command);
        Routed = true;
    }
    LastInputStatus=FString::Printf(TEXT("%s: %s (%s)"),Routed ? TEXT("Dispatched") : TEXT("No route"),*CommandLabel(Command).ToString(),*GetNameSafe(GetSelectedUnit()));
    if (bLogInput) UE_LOG(LogTemp,Log,TEXT("Battle HUD: %s"),*LastInputStatus);
    if (Routed && Command==EReditusHUDCommand::Attack) PlayConfirmCue();
    return Routed;
}

void UReditusBattleHUDWidget::HandleButton(EReditusHUDCommand Command, int32 Index)
{
    if (Command == EReditusHUDCommand::SelectPartyCard)
    {
        if (Party.IsValidIndex(Index)) SelectReadyUnit(Party[Index].Unit);
    }
    else if (Command == EReditusHUDCommand::SelectMenuEntry)
    {
        const int32 EntryIndex = Index;
        if (CanChooseMenuEntry() && MenuEntries.IsValidIndex(EntryIndex) && MenuEntries[EntryIndex].bEnabled)
        {
            SelectedMenuIndex = EntryIndex;
            OnMenuEntryChosen.Broadcast(GetSelectedUnit(),MenuEntries[EntryIndex].ID);
        }
    }
    else RequestCommand(Command);
    if (bCommandsEnabled && !HostedMenu) FocusCommands();
}

void UReditusBattleHUDWidget::HandleHover(EReditusHUDCommand Command, int32 Index)
{
    if (Command == EReditusHUDCommand::SelectMenuEntry && bListOpen)
    {
        const int32 Entry = Index;
        if (MenuEntries.IsValidIndex(Entry)) SelectedMenuIndex = Entry;
        RefreshMenu();
        RefreshGlyphs();
    }
    else
    {
        for (int32 I=0; I<Buttons.Num(); ++I) if (Buttons[I].Command == Command) SelectedCommand = I;
        Refresh();
    }
}

void UReditusBattleHUDWidget::SetAlternateCommands(bool bEnabled)
{
    const bool Next=bEnabled && bCommandsEnabled && !IsMenuOpen();
    if (bAlternateCommands==Next) return;
    bAlternateCommands=Next;
    SelectedCommand=SelectedCommand%4+(Next ? 4 : 0);
    Refresh();
}

void UReditusBattleHUDWidget::MoveCommandSelection(int32 Horizontal,int32 Vertical)
{
    const int32 PreviousCommand=SelectedCommand;
    const int32 Offset=bAlternateCommands ? 4 : 0;
    bool Enabled[4];
    for (int32 I=0; I<4; ++I) Enabled[I]=CanExecuteCommand(CommandOrder[Offset+I]);
    SelectedCommand=Offset+ReditusHUDInputPolicy::CrossMove(SelectedCommand%4,Horizontal,Vertical,Enabled);
    if (PreviousCommand!=SelectedCommand) PlayNavigationCue();
    Refresh();
}

bool UReditusBattleHUDWidget::CycleReadyAlly()
{
    for (int32 Step=1; Step<=Party.Num(); ++Step)
    {
        const int32 Index=ReditusHUDInputPolicy::Wrap(SelectedIndex+Step,Party.Num());
        if (Index!=SelectedIndex && Party[Index].bCanAct && !Party[Index].bQueued && SelectReadyUnit(Party[Index].Unit)) return true;
    }
    return false;
}

bool UReditusBattleHUDWidget::CanChooseMenuEntry() const
{
    if (bInputSuspended || UGameplayStatics::IsGamePaused(this)) return false;
    const FReditusBattleHUDUnit* Data=Selected();
    return Data && Data->Unit==MenuOwner && ReditusHUDPolicy::CanCommand(IsValid(Data->Unit),Data->HP,
        Data->bCanAct,Data->bReadyForCommands,bCommandsEnabled);
}

void UReditusBattleHUDWidget::MoveMenuSelection(int32 Horizontal,int32 Vertical)
{
    const int32 PreviousEntry=SelectedMenuIndex;
    SelectedMenuIndex=ReditusHUDInputPolicy::ListMove(SelectedMenuIndex,Horizontal,Vertical,MenuEntries.Num());
    if (PreviousEntry!=SelectedMenuIndex) PlayNavigationCue();
    RefreshMenu();
    RefreshGlyphs();
    RevealMenuSelection();
}

void UReditusBattleHUDWidget::ChangeMenuPage(int32 Direction)
{
    const int32 PreviousEntry=SelectedMenuIndex;
    SelectedMenuIndex=ReditusHUDInputPolicy::PageMove(SelectedMenuIndex,Direction,MenuEntries.Num(),MenuRows);
    if (PreviousEntry!=SelectedMenuIndex) PlayNavigationCue();
    RefreshMenu();
    RefreshGlyphs();
    RevealMenuSelection();
}

void UReditusBattleHUDWidget::PlayNavigationCue()
{
    if (!bPlayNavigationSound || NavigationSoundVolume<=0.f || bInputSuspended || !GetWorld() || !GetWorld()->IsGameWorld() ||
        UGameplayStatics::IsGamePaused(this) || !GetOwningPlayer() || !GetOwningPlayer()->IsLocalController()) return;
    if (!NavigationSound) NavigationSound=LoadObject<USoundBase>(nullptr,TEXT("/Game/Sounds/JrpgBlueprintSounds/CUE_MenuNavigation.CUE_MenuNavigation"));
    if (NavigationSound) UGameplayStatics::PlaySound2D(this,NavigationSound,FMath::Clamp(NavigationSoundVolume,0.f,1.f));
}

void UReditusBattleHUDWidget::PlayConfirmCue()
{
    if (!bPlayConfirmSound || ConfirmSoundVolume<=0.f || bInputSuspended || !GetWorld() || !GetWorld()->IsGameWorld() ||
        UGameplayStatics::IsGamePaused(this) || !GetOwningPlayer() || !GetOwningPlayer()->IsLocalController()) return;
    if (!ConfirmSound) ConfirmSound=LoadObject<USoundBase>(nullptr,TEXT("/Game/Sounds/JrpgBlueprintSounds/CUE_Confirm.CUE_Confirm"));
    if (ConfirmSound) UGameplayStatics::PlaySound2D(this,ConfirmSound,FMath::Clamp(ConfirmSoundVolume,0.f,1.f));
}

bool UReditusBattleHUDWidget::ChooseSelectedMenuEntry()
{
    if (!CanChooseMenuEntry() || !MenuEntries.IsValidIndex(SelectedMenuIndex) || !MenuEntries[SelectedMenuIndex].bEnabled) return false;
    const FName ID=MenuEntries[SelectedMenuIndex].ID;
    OnMenuEntryChosen.Broadcast(GetSelectedUnit(),ID);
    return true;
}

void UReditusBattleHUDWidget::ShowListMenu(FText Title, const TArray<FReditusBattleHUDMenuEntry>& Entries)
{
    if (HostedMenu) SetHostedMenu(nullptr);
    MenuTitle = Title;
    MenuStatus=FText::GetEmpty();
    MenuEntries = Entries;
    SelectedMenuIndex = MenuEntries.IndexOfByPredicate([](const FReditusBattleHUDMenuEntry& Entry){return Entry.bEnabled;});
    if (SelectedMenuIndex==INDEX_NONE && MenuEntries.Num()>0) SelectedMenuIndex=0;
    MenuOwner=GetSelectedUnit();
    bListOpen = true;
    if (EntryScroll) EntryScroll->SetScrollOffset(0.f);
    SetAlternateCommands(false);
    Refresh();
    RevealMenuSelection();
    FocusCommands();
}

void UReditusBattleHUDWidget::ShowListStatus(FText Title,FText Message)
{
    ShowListMenu(Title,TArray<FReditusBattleHUDMenuEntry>());
    MenuStatus=Message;
    RefreshMenu();
}

void UReditusBattleHUDWidget::CloseListMenu()
{
    bListOpen = false;
    MenuEntries.Reset();
    MenuStatus=FText::GetEmpty();
    SelectedMenuIndex = INDEX_NONE;
    MenuOwner=nullptr;
    if (HostedMenu) { HostedMenu->RemoveFromParent(); HostedMenu=nullptr; }
    SetAlternateCommands(PressedKeys.Contains(GamepadKeys.More) || PressedKeys.Contains(KeyboardMoreKey));
    Refresh();
}

void UReditusBattleHUDWidget::UpdateListEntries(const TArray<FReditusBattleHUDMenuEntry>& Entries)
{
    if (!bListOpen) return;
    const FName Previous=MenuEntries.IsValidIndex(SelectedMenuIndex) ? MenuEntries[SelectedMenuIndex].ID : NAME_None;
    const int32 OldIndex=SelectedMenuIndex;
    const bool CountChanged=MenuEntries.Num()!=Entries.Num();
    MenuEntries=Entries;
    MenuStatus=FText::GetEmpty();
    SelectedMenuIndex=MenuEntries.IndexOfByPredicate([Previous](const FReditusBattleHUDMenuEntry& Entry) { return Entry.ID==Previous; });
    if (SelectedMenuIndex==INDEX_NONE && MenuEntries.Num()>0) SelectedMenuIndex=FMath::Clamp(OldIndex,0,MenuEntries.Num()-1);
    RefreshMenu();
    RefreshGlyphs();
    if (CountChanged || OldIndex!=SelectedMenuIndex) RevealMenuSelection();
}

void UReditusBattleHUDWidget::CancelListMenu()
{
    AActor* OwnerUnit = GetSelectedUnit();
    CloseListMenu();
    OnMenuCancelled.Broadcast(OwnerUnit);
    FocusCommands();
}

void UReditusBattleHUDWidget::SetHostedMenu(UUserWidget* Menu)
{
    if (HostedMenu) HostedMenu->RemoveFromParent();
    HostedMenu = Menu;
    MenuOwner=Menu ? GetSelectedUnit() : nullptr;
    bListOpen = false;
    if (MenuHost && Menu)
    {
        Menu->RemoveFromParent();
        MenuHost->AddChildToOverlay(Menu);
    }
    Refresh();
}

void UReditusBattleHUDWidget::FocusCommands()
{
    if (GetOwningPlayer() && IsInViewport() && !bInputSuspended && !UGameplayStatics::IsGamePaused(this) && !bReactionWindow && !bTargetSelectionActive && (bCommandsEnabled || bListOpen))
    {
        FInputModeGameAndUI Mode;
        Mode.SetWidgetToFocus(TakeWidget());
        Mode.SetHideCursorDuringCapture(false);
        GetOwningPlayer()->SetInputMode(Mode);
        SetUserFocus(GetOwningPlayer());
    }
}

void UReditusBattleHUDWidget::Refresh()
{
    if (!DesignCanvas) return;
    SetTargetLabel(TargetLabel);
    SetActionAnnouncement(ActionLabel,ActionElementBrush,bActionVisible);
    SetDisplayedBattleSpeed(DisplayedSpeed);
    for (int32 I=0; I<Cards.Num(); ++I)
    {
        if (!Party.IsValidIndex(I)) continue;
        FCard& Card = Cards[I];
        const FReditusBattleHUDUnit& Data = Party[I];
        const bool Occupied=Data.HasCharacter();
        const bool Alive=Occupied && IsValid(Data.Unit) && Data.HP>0.f;
        const bool IsReady=Alive && Data.bCanAct && Data.bReadyForCommands;
        const FLinearColor Dull(0.64f,0.64f,0.64f,1.f);
        Card.Content->SetVisibility(Occupied ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Hidden);
        Card.Select->SetIsEnabled(Alive);
        Card.Frame->SetBrush(!Alive ? SilverFrameBrush : I==SelectedIndex ? SelectedFrameBrush : GoldFrameBrush);
        Card.Frame->SetBrushColor(FLinearColor::White);
        Card.Frame->SetRenderOpacity(!Occupied ? 0.16f : Alive ? 1.f : 0.62f);
        Card.SelectionGlow->SetVisibility(Alive && I==SelectedIndex ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
        if (!Occupied) continue;
        Card.Name->SetText(Data.DisplayName);
        Card.Name->SetColorAndOpacity(FSlateColor(Alive ? Gold : Dull));
        Card.ReadyLabel->SetText(Alive ? ReadyUnitText : Text(TEXT("KO")));
        Card.ReadyLabel->SetColorAndOpacity(FSlateColor(Alive ? Gold : FLinearColor::White));
        Card.ReadyLabel->SetVisibility(IsReady || !Alive ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
        FSlateBrush Portrait;
        Portrait.DrawAs = ESlateBrushDrawType::Image;
        Portrait.ImageSize = FVector2D(128.f,128.f);
        Portrait.SetResourceObject(IsValid(Data.Portrait) ? Data.Portrait : nullptr);
        Card.Portrait->SetBrush(Portrait);
        Card.Portrait->SetColorAndOpacity(Alive ? FLinearColor::White : FLinearColor(0.55f,0.55f,0.55f,1.f));
        Card.Portrait->SetVisibility(IsValid(Data.Portrait) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
        Card.HPBar->SetFillColorAndOpacity(Alive ? FLinearColor(0.f,0.85f,0.28f,1.f) : Dull);
        Card.MPBar->SetFillColorAndOpacity(Alive ? FLinearColor(0.f,0.40f,0.95f,1.f) : Dull);
        Card.ATBBar->SetFillColorAndOpacity(Alive ? FLinearColor(1.f,0.72f,0.06f,1.f) : Dull);
        Card.ODBar->SetFillColorAndOpacity(Alive ? FLinearColor(0.65f,0.15f,1.f,1.f) : Dull);
        Card.HPBar->SetPercent(Alive ? ReditusHUDPolicy::Ratio(Data.HP,Data.MaxHP) : 0.f);
        Card.MPBar->SetPercent(ReditusHUDPolicy::Ratio(Data.MP,Data.MaxMP));
        Card.ATBBar->SetPercent(Alive ? ReditusHUDPolicy::Ratio(Data.ATBPercent,1.f) : 0.f);
        Card.ODBar->SetPercent(ReditusHUDPolicy::Ratio(Data.Overdrive,Data.MaxOverdrive));
        Card.HP->SetText(Numbers(Alive ? Data.HP : 0.f,Data.MaxHP));
        Card.MP->SetText(Numbers(Data.MP,Data.MaxMP));
        Card.ATB->SetText(!Alive ? Text(TEXT("-")) : Data.bQueued ? Text(TEXT("QUEUED")) :
            Data.bReadyForCommands ? ReadyUnitText : FText::AsPercent(ReditusHUDPolicy::Ratio(Data.ATBPercent,1.f)));
        Card.OD->SetText(Alive && Data.bOverdriveEntered ? Text(TEXT("OD READY")) :
            FText::AsPercent(ReditusHUDPolicy::Ratio(Data.Overdrive,Data.MaxOverdrive)));
        Card.OD->SetColorAndOpacity(FSlateColor(Alive && Data.bOverdriveEntered ? FLinearColor(1.f,0.16f,0.16f,1.f) : FLinearColor::White));
        Card.Amp->SetText(FText::Format(NSLOCTEXT("ReditusHUD","CardAmp","AMP {0}/{1}"),FText::AsNumber(Data.CommittedAmp),FText::AsNumber(Data.TotalAmp)));
        RefreshAmpImages(Card.AmpIcons,Card.AmpImages,Data.TotalAmp,Data.CommittedAmp);
    }
    const FReditusBattleHUDUnit* Data = Selected();
    const bool MenuVisible=IsMenuOpen();
    const int32 Offset=bAlternateCommands ? 4 : 0;
    if (bCommandsEnabled && !IsMenuOpen() && Buttons.IsValidIndex(SelectedCommand) && !CanExecuteCommand(Buttons[SelectedCommand].Command))
        for (int32 I=Offset; I<Offset+4; ++I) if (CanExecuteCommand(Buttons[I].Command)) { SelectedCommand=I; break; }
    for (int32 I=0; I<Buttons.Num(); ++I)
    {
        FButtonView& View=Buttons[I];
        const bool Visible=(I>=4)==bAlternateCommands && (View.Command!=EReditusHUDCommand::Hack || (Data && Data->bCanHack));
        View.Button->SetVisibility(Visible ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
        View.Button->SetIsEnabled(CanExecuteCommand(View.Command));
        if (View.Command==EReditusHUDCommand::Attack)
            View.Label->SetText(bTargetSelectionActive ? Text(TEXT("CONFIRM")) : CommandLabel(View.Command));
        View.Label->SetColorAndOpacity(FSlateColor(I==SelectedCommand ? Gold : FLinearColor::White));
        FButtonStyle Style=View.Button->WidgetStyle;
        Style.Normal=I==SelectedCommand ? SelectedFrameBrush : SilverFrameBrush;
        View.Button->SetStyle(Style);
    }
    for (FButtonView& View : UtilityButtons)
    {
        if (View.Command!=EReditusHUDCommand::CycleBattleSpeed)
            View.Button->SetVisibility(MenuVisible ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
        View.Button->SetIsEnabled(CanExecuteCommand(View.Command));
        if (View.Command==EReditusHUDCommand::Overdrive)
        {
            const bool Ready=Data && Data->HP>0.f && Data->bCanOverdrive && Data->bOverdriveEntered;
            View.Label->SetColorAndOpacity(FSlateColor(Ready ? FLinearColor(1.f,0.18f,0.18f,1.f) : FLinearColor::White));
            FButtonStyle Style=View.Button->WidgetStyle;
            Style.Normal=Ready ? SelectedFrameBrush : SilverFrameBrush;
            Style.Disabled=Style.Normal;
            View.Button->SetStyle(Style);
        }
        if (View.Command==EReditusHUDCommand::ToggleAlternate)
            View.Label->SetText(Text(InputDevice==EReditusHUDInputDevice::Xbox ? (bAlternateCommands ? TEXT("RELEASE\nMAIN") : TEXT("HOLD\nMORE")) : (bAlternateCommands ? TEXT("MAIN") : TEXT("MORE"))));
    }
    CommandCanvas->SetVisibility(MenuVisible ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
    MenuHost->SetVisibility(MenuVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    ListCanvas->SetVisibility(bListOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    RefreshMenu();
    RefreshGlyphs();
}

void UReditusBattleHUDWidget::RefreshMenu()
{
    if (!MenuHeader || !bListOpen) return;
    if (ListButtons.Num()!=MenuEntries.Num()) BuildListRows();
    MenuHeader->SetText(!MenuStatus.IsEmpty() || MenuEntries.Num()>0 ? MenuTitle : FText::Format(NSLOCTEXT("ReditusHUD","EmptyList","{0} - No available entries"),MenuTitle));
    MenuDescription->SetText(!MenuStatus.IsEmpty() ? MenuStatus : MenuEntries.IsValidIndex(SelectedMenuIndex) ? MenuEntries[SelectedMenuIndex].Description : FText::GetEmpty());
    MenuDescription->SetFont(bMatchLegacyListFonts && bHasLegacyListFonts ? LegacyListDescriptionFont : Font(ListDescriptionSize));
    for (int32 I=0; I<ListButtons.Num(); ++I)
    {
        const int32 Entry=I;
        FButtonView& View=ListButtons[I];
        View.Button->SetVisibility(MenuEntries.IsValidIndex(Entry) ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
        if (!MenuEntries.IsValidIndex(Entry)) continue;
        const FReditusBattleHUDMenuEntry& Data=MenuEntries[Entry];
        View.Label->SetText(Data.Label);
        View.Label->SetFont(bMatchLegacyListFonts && bHasLegacyListFonts ? LegacyListNameFont : Font(ListTextSize));
        if (View.Detail)
        {
            View.Detail->SetText(Data.Detail);
            View.Detail->SetFont(bMatchLegacyListFonts && bHasLegacyListFonts ? LegacyListDetailFont : Font(ListTextSize));
        }
        if (View.DetailBox) View.DetailBox->SetVisibility(Data.Detail.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
        if (View.Icon)
        {
            View.Icon->SetBrush(Data.Icon);
            View.Icon->SetVisibility(Data.Icon.GetResourceObject() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
        }
        View.Label->SetColorAndOpacity(FSlateColor(Entry==SelectedMenuIndex ? Gold : FLinearColor::White));
        FButtonStyle RowStyle=View.Button->WidgetStyle;
        RowStyle.Normal=Entry==SelectedMenuIndex ? SelectedFrameBrush : SilverFrameBrush;
        View.Button->SetStyle(RowStyle);
        View.Button->SetIsEnabled(Data.bEnabled && CanChooseMenuEntry());
    }
}

void UReditusBattleHUDWidget::CopyLegacyListFonts(UTextBlock* NameText,UTextBlock* DetailText,UTextBlock* DescriptionText)
{
    if (!NameText || NameText->Font.Size<=0) return;
    LegacyListNameFont=NameText->Font;
    LegacyListDetailFont=DetailText && DetailText->Font.Size>0 ? DetailText->Font : LegacyListNameFont;
    LegacyListDescriptionFont=DescriptionText && DescriptionText->Font.Size>0 ? DescriptionText->Font : Font(ListDescriptionSize);
    bHasLegacyListFonts=true;
}

void UReditusBattleHUDWidget::BuildListRows()
{
    if (!EntryRows) return;
    EntryRows->ClearChildren();
    ListButtons.Reset();
    UHorizontalBox* ItemRow=nullptr;
    for (int32 I=0; I<MenuEntries.Num(); ++I)
    {
        if (I%2==0)
        {
            ItemRow=WidgetTree->ConstructWidget<UHorizontalBox>();
            USizeBox* RowHeight=WidgetTree->ConstructWidget<USizeBox>();
            RowHeight->SetMinDesiredHeight(48.f);
            RowHeight->AddChild(ItemRow);
            UVerticalBoxSlot* RowPlacement=EntryRows->AddChildToVerticalBox(RowHeight);
            RowPlacement->SetHorizontalAlignment(HAlign_Fill);
            RowPlacement->SetPadding(FMargin(0.f,0.f,0.f,4.f));
        }
        FButtonView EntryView=MakeButton(EReditusHUDCommand::SelectMenuEntry,FText::GetEmpty(),I);
        EntryView.Label->SetFont(Font(ListTextSize));
        EntryView.Key->SetFont(Font(12));
        EntryView.GlyphBox->SetWidthOverride(20.f);
        EntryView.GlyphBox->SetHeightOverride(20.f);
        UHorizontalBoxSlot* EntryPlacement=ItemRow->AddChildToHorizontalBox(EntryView.Button);
        EntryPlacement->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        EntryPlacement->SetHorizontalAlignment(HAlign_Fill);
        EntryPlacement->SetVerticalAlignment(VAlign_Fill);
        EntryPlacement->SetPadding(FMargin(I%2==0 ? 0.f : 3.f,0.f,I%2==0 ? 3.f : 0.f,0.f));
        ListButtons.Add(EntryView);
        if (I%2==0 && I==MenuEntries.Num()-1)
            ItemRow->AddChildToHorizontalBox(WidgetTree->ConstructWidget<USpacer>())->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }
}

void UReditusBattleHUDWidget::RevealMenuSelection()
{
    if (bListOpen && EntryScroll && ListButtons.IsValidIndex(SelectedMenuIndex))
        EntryScroll->ScrollWidgetIntoView(ListButtons[SelectedMenuIndex].Button,false,EDescendantScrollDestination::IntoView,0.f);
}

void UReditusBattleHUDWidget::RefreshGlyphs()
{
    const auto Apply=[this](FButtonView& View,bool bShow) {
        const FKey Key=KeyForCommand(View.Command);
        UTexture2D* Texture=GlyphTextures.FindRef(Key.GetFName());
        View.Glyph->SetBrushFromTexture(Texture);
        View.Glyph->SetVisibility(bShow && Texture ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
        View.GlyphBox->SetVisibility(bShow && Texture ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
        View.Key->SetText(Key==EKeys::Gamepad_RightThumbstick ? Text(TEXT("RS")) : Key.IsValid() ? Key.GetDisplayName() : FText::GetEmpty());
        View.Key->SetVisibility(bShow && !Texture ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    };
    for (FButtonView& View : Buttons) Apply(View,true);
    for (FButtonView& View : UtilityButtons) Apply(View,true);
    for (int32 I=0; I<ListButtons.Num(); ++I)
    {
        FButtonView& View=ListButtons[I];
        const bool SelectedEntry=SelectedMenuIndex>=0 && I==SelectedMenuIndex;
        const bool HasBrush=ListSelectionBrush.GetResourceObject()!=nullptr;
        View.Glyph->SetBrush(ListSelectionBrush);
        View.GlyphBox->SetVisibility(HasBrush ? (SelectedEntry ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden) : ESlateVisibility::Collapsed);
        View.Glyph->SetVisibility(ESlateVisibility::HitTestInvisible);
        View.Key->SetText(Text(TEXT(">")));
        View.Key->SetVisibility(!HasBrush && SelectedEntry ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    }
}


bool UReditusBattleHUDWidget::HasGameInputFocus() const
{
    if (bInputSuspended || !IsInViewport() || !GetOwningPlayer() || UGameplayStatics::IsGamePaused(this) || !FSlateApplication::IsInitialized()) return false;
    const ULocalPlayer* Local=GetOwningLocalPlayer();
    if (!Local || !Local->ViewportClient) return false;
    const TSharedPtr<SViewport> ViewportWidget=Local->ViewportClient->GetGameViewportWidget();
    FSlateApplication& Slate=FSlateApplication::Get();
    if (!ViewportWidget.IsValid() || Slate.GetActiveModalWindow().IsValid()) return false;
    const int32 User=FMath::Max(0,Local->GetControllerId());
    const TSharedPtr<SWidget> Focused=Slate.GetUserFocusedWidget(User);
    if (!Focused.IsValid()) return false;
    FWidgetPath Path;
    if (!Slate.GeneratePathToWidgetUnchecked(Focused.ToSharedRef(),Path,EVisibility::All)) return false;
    const TSharedPtr<SWindow> Active=Slate.GetActiveTopLevelWindow();
    if (Active.IsValid() && Path.TopLevelWindow.IsValid() && Active!=Path.TopLevelWindow) return false;
    const TSharedPtr<SWidget> CachedHUDWidget=GetCachedWidget();
    bool GamePath=Path.ContainsWidget(ViewportWidget.ToSharedRef()) ||
        (CachedHUDWidget.IsValid() && Path.ContainsWidget(CachedHUDWidget.ToSharedRef()));
    for (int32 I=0; I<Path.Widgets.Num(); ++I)
    {
        const FString Type=Path.Widgets[I].Widget->GetTypeAsString();
        if (Type.Contains(TEXT("EditableText"))) return false;
        if (Type==TEXT("SGameLayerManager")) GamePath=true;
    }
    return GamePath;
}

void UReditusBattleHUDWidget::NativeTick(const FGeometry& Geometry,float DeltaTime)
{
    Super::NativeTick(Geometry,DeltaTime);
    if ((PressedKeys.Num()>0 || bAlternateCommands) && !HasGameInputFocus()) { PressedKeys.Reset(); SetAlternateCommands(false); }
    const FReditusBattleHUDUnit* Data=Selected();
    const bool Ready=Data && Data->HP>0.f && Data->bCanOverdrive && Data->bOverdriveEntered;
    const float Pulse=Ready && GetWorld() ? 0.80f+0.20f*FMath::Sin(GetWorld()->GetRealTimeSeconds()*5.f) : 1.f;
    for (FButtonView& View : UtilityButtons)
        if (View.Command==EReditusHUDCommand::Overdrive) View.Button->SetRenderOpacity(Pulse);
}

bool UReditusBattleHUDWidget::HandleBattleKeyDown(FKey Key,bool bRepeat)
{
    if (bInputSuspended || !IsInViewport() || UGameplayStatics::IsGamePaused(this)) return false;
    ObserveKey(Key);
    if (!bRepeat)
    {
        LastInputStatus=FString::Printf(TEXT("Key %s; commands=%d target=%d list=%d owner=%s"),*Key.GetFName().ToString(),bCommandsEnabled,bTargetSelectionActive,bListOpen,*GetNameSafe(GetSelectedUnit()));
        if (bLogInput) UE_LOG(LogTemp,Log,TEXT("Battle HUD: %s"),*LastInputStatus);
    }
    const bool bIsNavigationKey=Key==EKeys::Up || Key==EKeys::W || Key==EKeys::Gamepad_DPad_Up || Key==EKeys::Gamepad_LeftStick_Up ||
        Key==EKeys::Down || Key==EKeys::S || Key==EKeys::Gamepad_DPad_Down || Key==EKeys::Gamepad_LeftStick_Down ||
        Key==EKeys::Left || Key==EKeys::A || Key==EKeys::Gamepad_DPad_Left || Key==EKeys::Gamepad_LeftStick_Left ||
        Key==EKeys::Right || Key==EKeys::D || Key==EKeys::Gamepad_DPad_Right || Key==EKeys::Gamepad_LeftStick_Right;
    if (!ReditusHUDInputPolicy::ShouldDispatchPress(PressedKeys.Contains(Key),bRepeat,bIsNavigationKey)) return true;
    const auto Owned=[this,Key]() { PressedKeys.Add(Key); return true; };
    const bool Gamepad=Key.IsGamepadKey();
    const bool Block=Gamepad ? Key==GamepadKeys.Block : bReactionWindow && MatchesAction(InputActions.Block,Key);
    if (Block && (bReactionWindow || bCommandsEnabled || bListOpen))
    {
        if (!bRepeat) RequestCommand(EReditusHUDCommand::Block);
        return Owned();
    }
    const bool Increase=Gamepad ? Key==GamepadKeys.IncreaseAmp : MatchesAction(InputActions.IncreaseAmp,Key);
    const bool Decrease=Gamepad ? Key==GamepadKeys.DecreaseAmp : MatchesAction(InputActions.DecreaseAmp,Key);
    if ((Increase || Decrease) && (bCommandsEnabled || bListOpen))
    {
        if (!bRepeat) RequestCommand(Increase ? EReditusHUDCommand::IncreaseAmp : EReditusHUDCommand::DecreaseAmp);
        return Owned();
    }
    if (MatchesAction(InputActions.Speed,Key))
    {
        if (!bRepeat) RequestCommand(EReditusHUDCommand::CycleBattleSpeed);
        return Owned();
    }
    if (bTargetSelectionActive && !IsMenuOpen())
    {
        const int32 Face=Key==GamepadKeys.Bottom ? 0 : Key==GamepadKeys.Right ? 2 : -1;
        const int32 Target=Gamepad ? ReditusHUDInputPolicy::FaceTarget(ReditusHUDInputPolicy::Surface::Target,Face,false) : ReditusHUDInputPolicy::Pass;
        const FKey* AttackShortcut=KeyboardShortcuts.Find(EReditusHUDCommand::Attack);
        const bool ConfirmKey=Gamepad ? Target==ReditusHUDInputPolicy::Confirm :
            Key==EKeys::Enter || MatchesAction(TEXT("MenuConfirm"),Key) || (AttackShortcut && Key==*AttackShortcut);
        const bool BackKey=Gamepad ? Key==GamepadBackKey : Key==KeyboardBackKey;
        if (BackKey || ConfirmKey)
        {
            if (!bRepeat) RequestCommand(BackKey ? EReditusHUDCommand::Cancel : EReditusHUDCommand::Attack);
            return Owned();
        }
        FName TargetAction;
        if (Key==EKeys::Up || Key==EKeys::W || Key==EKeys::Gamepad_DPad_Up || Key==EKeys::Gamepad_LeftStick_Up) TargetAction=TEXT("MenuUp");
        else if (Key==EKeys::Down || Key==EKeys::S || Key==EKeys::Gamepad_DPad_Down || Key==EKeys::Gamepad_LeftStick_Down) TargetAction=TEXT("MenuDown");
        else if (Key==EKeys::Left || Key==EKeys::A || Key==EKeys::Gamepad_DPad_Left || Key==EKeys::Gamepad_LeftStick_Left) TargetAction=TEXT("MenuLeft");
        else if (Key==EKeys::Right || Key==EKeys::D || Key==EKeys::Gamepad_DPad_Right || Key==EKeys::Gamepad_LeftStick_Right) TargetAction=TEXT("MenuRight");
        if (!TargetAction.IsNone() && RouteGameplayInput(TargetAction,Key)) return Owned();
        return false;
    }
    if (HostedMenu) return false;
    const bool Root=bCommandsEnabled && !bListOpen && !bTargetSelectionActive;
    if (Key==GamepadKeys.More || Key==KeyboardMoreKey)
    {
        if (!Root) return false;
        SetAlternateCommands(true);
        return Owned();
    }
    if (Gamepad)
    {
        if (bListOpen && Key==GamepadBackKey)
        {
            if (!bRepeat) RequestCommand(EReditusHUDCommand::Cancel);
            return Owned();
        }
        const FKey Faces[] = {GamepadKeys.Bottom,GamepadKeys.Left,GamepadKeys.Right,GamepadKeys.Top};
        int32 Face=INDEX_NONE;
        for (int32 I=0; I<4; ++I) if (Key==Faces[I]) Face=I;
        const auto Surface=bListOpen ? ReditusHUDInputPolicy::Surface::List : Root ? ReditusHUDInputPolicy::Surface::Commands : ReditusHUDInputPolicy::Surface::Inactive;
        int32 Target=ReditusHUDInputPolicy::FaceTarget(Surface,Face,bAlternateCommands);
        if (bListOpen && Target==ReditusHUDInputPolicy::Back && Key!=GamepadBackKey) Target=ReditusHUDInputPolicy::Pass;
        if (Target!=ReditusHUDInputPolicy::Pass)
        {
            if (!bRepeat)
            {
                if (Target==ReditusHUDInputPolicy::Back) RequestCommand(EReditusHUDCommand::Cancel);
                else if (Target==ReditusHUDInputPolicy::Confirm) ChooseSelectedMenuEntry();
                else RequestCommand(CommandOrder[Target]);
            }
            return Owned();
        }
        if (Root && Key==GamepadKeys.Overdrive)
        {
            if (!bRepeat) RequestCommand(EReditusHUDCommand::Overdrive);
            return Owned();
        }
    }
    if (bListOpen && !Gamepad && Key==KeyboardBackKey)
    {
        if (!bRepeat) RequestCommand(EReditusHUDCommand::Cancel);
        return Owned();
    }
    if (!Root && !bListOpen) return false; // Existing target/dialogue input remains authoritative.
    const int32 V=(Key==EKeys::Up || Key==EKeys::W || Key==EKeys::Gamepad_DPad_Up || Key==EKeys::Gamepad_LeftStick_Up) ? -1 :
        (Key==EKeys::Down || Key==EKeys::S || Key==EKeys::Gamepad_DPad_Down || Key==EKeys::Gamepad_LeftStick_Down) ? 1 : 0;
    const int32 H=(Key==EKeys::Left || Key==EKeys::A || Key==EKeys::Gamepad_DPad_Left || Key==EKeys::Gamepad_LeftStick_Left) ? -1 :
        (Key==EKeys::Right || Key==EKeys::D || Key==EKeys::Gamepad_DPad_Right || Key==EKeys::Gamepad_LeftStick_Right) ? 1 : 0;
    if (H || V)
    {
        if (bListOpen) MoveMenuSelection(H,V); else MoveCommandSelection(H,V);
        return Owned();
    }
    if (bListOpen && (Key==EKeys::PageUp || Key==EKeys::PageDown))
    {
        ChangeMenuPage(Key==EKeys::PageUp ? -1 : 1);
        return Owned();
    }
    if (Key==EKeys::Enter || (!Gamepad && MatchesAction(TEXT("MenuConfirm"),Key)))
    {
        if (!bRepeat)
        {
            if (bListOpen) ChooseSelectedMenuEntry();
            else if (Buttons.IsValidIndex(SelectedCommand)) RequestCommand(Buttons[SelectedCommand].Command);
        }
        return Owned();
    }
    if (Root && !Gamepad)
    {
        if (Key==KeyboardBackKey) { if (!bRepeat) RequestCommand(EReditusHUDCommand::Cancel); return Owned(); }
        for (int32 I=0; I<4; ++I)
            if (const FKey* Shortcut=KeyboardShortcuts.Find(CommandOrder[I]))
                if (*Shortcut==Key)
                {
                    const int32 Target=ReditusHUDInputPolicy::FaceTarget(ReditusHUDInputPolicy::Surface::Commands,I,bAlternateCommands);
                    if (!bRepeat) RequestCommand(CommandOrder[Target]);
                    return Owned();
                }
        for (const auto& Shortcut : KeyboardShortcuts)
            if (Shortcut.Value==Key) { if (!bRepeat) RequestCommand(Shortcut.Key); return Owned(); }
    }
    return false;
}

bool UReditusBattleHUDWidget::HandleBattleKeyUp(FKey Key)
{
    const bool Owned=PressedKeys.Remove(Key)>0;
    if (Key==GamepadKeys.More || Key==KeyboardMoreKey)
        SetAlternateCommands(PressedKeys.Contains(GamepadKeys.More) || PressedKeys.Contains(KeyboardMoreKey));
    return Owned;
}

bool UReditusBattleHUDWidget::HandleBattleAnalog(FKey Key,float Previous,float Current)
{
    if (Key==EKeys::Gamepad_LeftTriggerAxis || Key==EKeys::Gamepad_RightTriggerAxis)
    {
        const FKey Digital=Key==EKeys::Gamepad_LeftTriggerAxis ? EKeys::Gamepad_LeftTrigger : EKeys::Gamepad_RightTrigger;
        const int32 Edge=ReditusHUDInputPolicy::TriggerEdge(Previous,Current);
        if (Edge>0) return HandleBattleKeyDown(Digital,false);
        if (Edge<0) return HandleBattleKeyUp(Digital);
        return PressedKeys.Contains(Digital);
    }
    if (Key==EKeys::Gamepad_LeftX || Key==EKeys::Gamepad_LeftY)
    {
        const bool Horizontal=Key==EKeys::Gamepad_LeftX;
        const FKey Positive=Horizontal ? EKeys::Gamepad_LeftStick_Right : EKeys::Gamepad_LeftStick_Up;
        const FKey Negative=Horizontal ? EKeys::Gamepad_LeftStick_Left : EKeys::Gamepad_LeftStick_Down;
        bool Owned=false;
        const int32 PosEdge=ReditusHUDInputPolicy::TriggerEdge(FMath::Max(0.f,Previous),FMath::Max(0.f,Current));
        const int32 NegEdge=ReditusHUDInputPolicy::TriggerEdge(FMath::Max(0.f,-Previous),FMath::Max(0.f,-Current));
        if (PosEdge<0) Owned=HandleBattleKeyUp(Positive) || Owned;
        if (NegEdge<0) Owned=HandleBattleKeyUp(Negative) || Owned;
        if (PosEdge>0) Owned=HandleBattleKeyDown(Positive,false) || Owned;
        if (NegEdge>0) Owned=HandleBattleKeyDown(Negative,false) || Owned;
        return Owned || PressedKeys.Contains(Positive) || PressedKeys.Contains(Negative);
    }
    return false;
}

FReply UReditusBattleHUDWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    return HandleBattleKeyDown(Event.GetKey(),Event.IsRepeat()) ? FReply::Handled() : Super::NativeOnPreviewKeyDown(Geometry,Event);
}

FReply UReditusBattleHUDWidget::NativeOnKeyUp(const FGeometry& Geometry,const FKeyEvent& Event)
{
    return HandleBattleKeyUp(Event.GetKey()) ? FReply::Handled() : Super::NativeOnKeyUp(Geometry,Event);
}

FReply UReditusBattleHUDWidget::NativeOnMouseWheel(const FGeometry& Geometry,const FPointerEvent& Event)
{
    if (bListOpen && EntryScroll && Event.GetWheelDelta()!=0.f)
    {
        if (bAutomaticGlyphSwitching) SetInputDevice(EReditusHUDInputDevice::KeyboardMouse);
        EntryScroll->SetScrollOffset(FMath::Max(0.f,EntryScroll->GetScrollOffset()-Event.GetWheelDelta()*34.f));
        return FReply::Handled();
    }
    return Super::NativeOnMouseWheel(Geometry,Event);
}
