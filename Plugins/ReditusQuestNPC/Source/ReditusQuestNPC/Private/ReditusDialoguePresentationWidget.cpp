#include "ReditusDialoguePresentationWidget.h"
#include "ReditusQuestNPCReflection.h"
#include "Dialogue.h"
#include "DialogueUserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/AudioComponent.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/RichTextBlock.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "MovieSceneSequencePlayer.h"
#include "Styling/CoreStyle.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogReditusDialogueUI, Log, All);

namespace ReditusDialogueUI
{
    const FLinearColor Gold(0.87f, 0.64f, 0.22f, 1.f);
    const FLinearColor GoldDim(0.37f, 0.27f, 0.11f, 1.f);
    const FLinearColor Navy(0.018f, 0.035f, 0.062f, .98f);
    const FLinearColor Cream(1.f, .95f, .82f, 1.f);
    const FLinearColor Muted(.63f, .69f, .74f, 1.f);
    const FLinearColor Mist(.018f, .05f, .075f, .22f);
    const FLinearColor Accent(.43f, .82f, .89f, 1.f);

    FVector2D ExistingPortraitBounds(UImage* Image)
    {
        // Read the working Designer bounds BEFORE reparenting the original
        // image. Never substitute the reference image's oversized portrait.
        FVector2D Bounds(0, 0);
        for (UWidget* W = Image; W; W = W->GetParent())
        {
            if (auto* Size = Cast<USizeBox>(W))
            {
                auto* WidthEnabled = FindFProperty<FBoolProperty>(Size->GetClass(), TEXT("bOverride_WidthOverride"));
                auto* HeightEnabled = FindFProperty<FBoolProperty>(Size->GetClass(), TEXT("bOverride_HeightOverride"));
                // UE4.27 has the reflected fields; the getter methods are not
                // available in the user's engine build. Read only enabled overrides.
                auto* WidthValue = FindFProperty<FFloatProperty>(Size->GetClass(), TEXT("WidthOverride"));
                auto* HeightValue = FindFProperty<FFloatProperty>(Size->GetClass(), TEXT("HeightOverride"));
                if (Bounds.X <= 0 && WidthEnabled && WidthValue && WidthEnabled->GetPropertyValue_InContainer(Size))
                    Bounds.X = WidthValue->GetPropertyValue_InContainer(Size);
                if (Bounds.Y <= 0 && HeightEnabled && HeightValue && HeightEnabled->GetPropertyValue_InContainer(Size))
                    Bounds.Y = HeightValue->GetPropertyValue_InContainer(Size);
            }
            if (auto* CanvasSlot = Cast<UCanvasPanelSlot>(W->Slot))
            {
                const auto A = CanvasSlot->GetAnchors();
                const auto S = CanvasSlot->GetSize();
                if (Bounds.X <= 0 && A.Minimum.X == A.Maximum.X && S.X > 0) Bounds.X = S.X;
                if (Bounds.Y <= 0 && A.Minimum.Y == A.Maximum.Y && S.Y > 0) Bounds.Y = S.Y;
                // An enclosing screen-sized canvas is not a portrait size.
                break;
            }
        }
        const FVector2D BrushSize = Image ? Image->Brush.ImageSize : FVector2D::ZeroVector;
        const bool HasAspect = Image && Image->Brush.GetResourceObject() && BrushSize.X > 0 && BrushSize.Y > 0;
        if (Bounds.X <= 0 && Bounds.Y > 0) Bounds.X = HasAspect ? Bounds.Y * BrushSize.X / BrushSize.Y : Bounds.Y;
        if (Bounds.Y <= 0 && Bounds.X > 0) Bounds.Y = HasAspect ? Bounds.X * BrushSize.Y / BrushSize.X : Bounds.X;
        if (Bounds.X <= 0 || Bounds.Y <= 0)
        {
            // A stretched source slot cannot be measured before Slate exists.
            // Fall back conservatively rather than promote native 1K artwork
            // to a screen-sized portrait. Fixed Designer sizes above win.
            const float Scale = HasAspect ? FMath::Min(1.f, 256.f / FMath::Max(BrushSize.X, BrushSize.Y)) : 1.f;
            Bounds = HasAspect ? BrushSize * Scale : FVector2D(256, 256);
        }
        return Bounds;
    }

    UMovieSceneSequencePlayer* SequencePlayer(UDialogueUserWidget* Widget)
    {
        if (!IsValid(Widget)) return nullptr;
        // The exported widget reaches battleController THROUGH JrpgController.
        // It has no direct battleController property of its own.
        UObject* Controller = ReditusQuestNPCReflection::ReadObject(Widget, TEXT("JrpgController"));
        if (!IsValid(Controller)) Controller = Widget->GetOwningPlayer();
        UObject* Battle = ReditusQuestNPCReflection::ReadObject(Controller, TEXT("battleController"));
        auto* Player = Cast<UMovieSceneSequencePlayer>(ReditusQuestNPCReflection::ReadObject(Battle, TEXT("CurrentLevelSequence")));
        if (IsValid(Player) && (Player->IsPlaying() || Player->IsPaused())) return Player;
        // Exploration sequences use the controller's existing sequence actor.
        // Preserve the trailing space in this actual Blueprint member name.
        UObject* Actor = ReditusQuestNPCReflection::ReadObject(Controller, TEXT("Current Sequence "));
        Player = Cast<UMovieSceneSequencePlayer>(ReditusQuestNPCReflection::ReadObject(Actor, TEXT("SequencePlayer")));
        return IsValid(Player) && (Player->IsPlaying() || Player->IsPaused()) ? Player : nullptr;
    }

    bool Shown(const UWidget* Widget)
    {
        if (!Widget) return false;
        const auto V = Widget->GetVisibility();
        return V == ESlateVisibility::Visible || V == ESlateVisibility::HitTestInvisible
            || V == ESlateVisibility::SelfHitTestInvisible;
    }

    UWidget* WidgetField(UDialogueUserWidget* Source, FName Name)
    {
        if (!Source) return nullptr;
        if (auto* W = Cast<UWidget>(ReditusQuestNPCReflection::ReadObject(Source, Name))) return W;
        return Source->GetWidgetFromName(Name);
    }

    bool ReadBool(UObject* Object, FName Name, bool& Value)
    {
        auto* P = Object ? FindFProperty<FBoolProperty>(Object->GetClass(), Name) : nullptr;
        if (!P) return false;
        Value = P->GetPropertyValue_InContainer(Object); return true;
    }

    const FDialogueNode* Node(UObject* Object, FName Name)
    {
        auto* P = Object ? FindFProperty<FStructProperty>(Object->GetClass(), Name) : nullptr;
        // The supplied plugin's FDialogueNode lacks a module export macro, so
        // do not import its StaticStruct symbol across DLLs on Windows.
        return P && P->Struct && P->Struct->GetPathName() == TEXT("/Script/DialoguePlugin.DialogueNode")
            ? P->ContainerPtrToValuePtr<FDialogueNode>(Object) : nullptr;
    }

    FText TextOf(UWidget* Widget)
    {
        if (auto* Rich = Cast<URichTextBlock>(Widget)) return Rich->GetText();
        if (auto* Text = Cast<UTextBlock>(Widget)) return Text->GetText();
        return FText::GetEmpty();
    }

    FString Plain(const FString& Input)
    {
        FString Result;
        for (int32 I = 0; I < Input.Len(); ++I)
        {
            if (Input[I] == '<')
            {
                const int32 End = Input.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, I + 1);
                if (End != INDEX_NONE) { I = End; continue; }
            }
            Result.AppendChar(Input[I]);
        }
        return Result;
    }

    UTextBlock* Label(UWidgetTree* Tree, const FText& Text, int32 Size, FLinearColor Color = Cream, bool Bold = false)
    {
        auto* Result = Tree->ConstructWidget<UTextBlock>();
        Result->SetText(Text);
        Result->SetFont(FCoreStyle::GetDefaultFontStyle(Bold ? TEXT("Bold") : TEXT("Regular"), Size));
        Result->SetColorAndOpacity(FSlateColor(Color));
        Result->SetShadowOffset(FVector2D(1.f, 1.f));
        Result->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, .8f));
        return Result;
    }

    UBorder* Solid(UWidgetTree* Tree, FLinearColor Color, FMargin Padding = FMargin(0))
    {
        auto* Result = Tree->ConstructWidget<UBorder>();
        Result->SetBrush(FSlateColorBrush(Color));
        Result->SetPadding(Padding);
        return Result;
    }

    UBorder* Frame(UWidgetTree* Tree, UWidget* Content, FMargin Padding)
    {
        auto* Outer = Solid(Tree, Gold, FMargin(2));
        auto* Gap = Solid(Tree, Navy, FMargin(2));
        auto* Inner = Solid(Tree, GoldDim, FMargin(1));
        auto* Face = Solid(Tree, Navy, Padding);
        Outer->SetContent(Gap); Gap->SetContent(Inner); Inner->SetContent(Face); Face->SetContent(Content);
        return Outer;
    }

    void ButtonStyle(UButton* Button, bool Selected = false)
    {
        FButtonStyle Style;
        Style.SetNormal(FSlateColorBrush(Selected ? FLinearColor(.055f, .34f, .43f, .48f) : FLinearColor(.02f, .05f, .075f, .12f)));
        Style.SetHovered(FSlateColorBrush(FLinearColor(.08f, .33f, .41f, .46f)));
        Style.SetPressed(FSlateColorBrush(FLinearColor(.11f, .42f, .49f, .56f)));
        Style.SetDisabled(FSlateColorBrush(FLinearColor(0, 0, 0, .025f)));
        Style.SetNormalPadding(FMargin(15, 11)); Style.SetPressedPadding(FMargin(16, 12, 14, 10));
        Button->SetStyle(Style);
        Button->IsFocusable = false; // Existing dialogue widget keeps keyboard/gamepad focus.
    }

    UButton* Button(UWidgetTree* Tree, const FText& Caption, UTextBlock*& Text, int32 FontSize = 17)
    {
        auto* Result = Tree->ConstructWidget<UButton>();
        ButtonStyle(Result);
        Text = Label(Tree, Caption, FontSize, Cream, true);
        Text->SetJustification(ETextJustify::Center);
        Result->SetContent(Text); return Result;
    }

    UCanvasPanelSlot* Anchored(UCanvasPanel* Canvas, UWidget* Child, FAnchors Anchors, int32 Z = 0)
    {
        auto* CanvasSlot = Canvas->AddChildToCanvas(Child);
        CanvasSlot->SetAnchors(Anchors); CanvasSlot->SetOffsets(FMargin(0)); CanvasSlot->SetZOrder(Z); return CanvasSlot;
    }

    UCanvasPanel* DialogueBackdrop(UWidgetTree* Tree)
    {
        auto* Backdrop = Tree->ConstructWidget<UCanvasPanel>();
        // A translucent wash, not an invisible panel. Fade its upper/lower edges
        // while retaining enough tint behind the text on a bright battlefield.
        // Uses the same simple UE4.27 border brushes as the existing UI.
        const int32 Bands = 32;
        for (int32 Band = 0; Band < Bands; ++Band)
        {
            const float Top = float(Band) / Bands;
            const float Bottom = float(Band + 1) / Bands;
            const float Middle = (Top + Bottom) * .5f;
            const float Fade = FMath::Min(FMath::Clamp(Middle / .20f, 0.f, 1.f),
                FMath::Clamp((1.f - Middle) / .18f, 0.f, 1.f));
            auto* Tint = Solid(Tree, FLinearColor(.018f, .035f, .062f, .62f * Fade));
            Anchored(Backdrop, Tint, FAnchors(0.f, Top, 1.f, Bottom));
        }
        Backdrop->SetVisibility(ESlateVisibility::HitTestInvisible);
        return Backdrop;
    }

    void Corner(UWidgetTree* Tree, UCanvasPanel* Canvas, float X, float Y, bool Right, bool Bottom)
    {
        auto* H = Anchored(Canvas, Solid(Tree, Gold), FAnchors(X, Y), 11);
        H->SetPosition(FVector2D(Right ? -22.f : 7.f, Bottom ? -9.f : 7.f)); H->SetSize(FVector2D(15, 2));
        auto* V = Anchored(Canvas, Solid(Tree, Gold), FAnchors(X, Y), 11);
        V->SetPosition(FVector2D(Right ? -9.f : 7.f, Bottom ? -22.f : 7.f)); V->SetSize(FVector2D(2, 15));
    }

    UBorder* GlassFrame(UWidgetTree* Tree, UWidget* Content, FMargin Padding)
    {
        auto* Holder = Solid(Tree, FLinearColor::Transparent);
        auto* Canvas = Tree->ConstructWidget<UCanvasPanel>();
        Holder->SetContent(Canvas);
        Anchored(Canvas, Solid(Tree, Mist), FAnchors(0, 0, 1, 1));
        auto* Face = Solid(Tree, FLinearColor::Transparent, Padding);
        Face->SetContent(Content);
        Anchored(Canvas, Face, FAnchors(0, 0, 1, 1), 1);
        const FLinearColor Edge(Gold.R, Gold.G, Gold.B, .38f);
        auto* Top = Anchored(Canvas, Solid(Tree, Edge), FAnchors(0, 0, 1, 0), 2);
        Top->SetOffsets(FMargin(22, 0, 22, 1));
        auto* Bottom = Anchored(Canvas, Solid(Tree, Edge), FAnchors(0, 1, 1, 1), 2);
        Bottom->SetOffsets(FMargin(22, -1, 22, 1));
        auto* Left = Anchored(Canvas, Solid(Tree, Edge), FAnchors(0, 0, 0, 1), 2);
        Left->SetOffsets(FMargin(0, 22, 1, 22));
        auto* Right = Anchored(Canvas, Solid(Tree, Edge), FAnchors(1, 0, 1, 1), 2);
        Right->SetOffsets(FMargin(-1, 22, 1, 22));
        Corner(Tree, Canvas, 0, 0, false, false); Corner(Tree, Canvas, 1, 0, true, false);
        Corner(Tree, Canvas, 0, 1, false, true); Corner(Tree, Canvas, 1, 1, true, true);
        return Holder;
    }

    UWidget* Rule(UWidgetTree* Tree)
    {
        auto* Row = Tree->ConstructWidget<UHorizontalBox>();
        for (int32 I = 0; I < 8; ++I)
        {
            const float Alpha = .60f * (1.f - float(I) / 8.f);
            auto* Piece = Solid(Tree, FLinearColor(Gold.R, Gold.G, Gold.B, Alpha));
            Row->AddChildToHorizontalBox(Piece)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        }
        auto* Height = Tree->ConstructWidget<USizeBox>();
        Height->SetHeightOverride(1); Height->SetContent(Row);
        return Height;
    }

    bool ConfirmKey(const FKey& K)
    {
        return K == EKeys::Enter || K == EKeys::SpaceBar || K == EKeys::Gamepad_FaceButton_Bottom;
    }
}

class FReditusDialogueInputProcessor : public IInputProcessor
{
    TWeakObjectPtr<UReditusDialoguePresentationWidget> Owner;
public:
    explicit FReditusDialogueInputProcessor(UReditusDialoguePresentationWidget* InOwner) : Owner(InOwner) {}
    virtual void Tick(const float DeltaTime, FSlateApplication& App, TSharedRef<ICursor> Cursor) override
    {
        (void)DeltaTime; (void)App; (void)Cursor;
        if (Owner.IsValid() && !Owner->CanRouteInput()) Owner->InputInactive();
    }
    virtual bool HandleKeyDownEvent(FSlateApplication& App, const FKeyEvent& Event) override
    { (void)App; return Owner.IsValid() && Owner->RouteKeyDown(Event.GetKey(), Event.IsRepeat()); }
    virtual bool HandleKeyUpEvent(FSlateApplication& App, const FKeyEvent& Event) override
    { (void)App; return Owner.IsValid() && Owner->RouteKeyUp(Event.GetKey()); }
    virtual bool HandleAnalogInputEvent(FSlateApplication& App, const FAnalogInputEvent& Event) override
    { (void)App; return Owner.IsValid() && Owner->RouteAnalog(Event.GetKey(), Event.GetAnalogValue()); }
    virtual bool HandleMouseButtonDownEvent(FSlateApplication& App, const FPointerEvent& Event) override
    {
        (void)App; (void)Event;
        if (Owner.IsValid() && Owner->CanRouteInput()) Owner->UsingGamepad = false;
        return false;
    }
};

void UReditusDialogueChoiceProxy::Clicked()
{ if (Owner.IsValid()) Owner->ChooseReply(Index, OriginalReply.Get()); }
void UReditusDialogueChoiceProxy::Hovered()
{ if (Owner.IsValid()) Owner->SelectReply(Index, OriginalReply.Get()); }

bool UReditusDialoguePresentationWidget::Install(UDialogueUserWidget* DialogueWidget)
{
    using namespace ReditusDialogueUI;
    if (!IsValid(DialogueWidget) || !DialogueWidget->WidgetTree || !DialogueWidget->GetOwningPlayer()) return false;
    if (DialogueWidget->GetRootWidget() && DialogueWidget->GetRootWidget()->GetFName() == TEXT("ReditusPresentationRoot")) return true;
    // Only install before Slate construction. Failed preflight leaves the working UI intact.
    if (DialogueWidget->GetCachedWidget().IsValid()) return false;
    UWidget* OldRoot = DialogueWidget->GetRootWidget();
    UWidget* Text = WidgetField(DialogueWidget, TEXT("NPCText"));
    auto* Name = Cast<UTextBlock>(WidgetField(DialogueWidget, TEXT("SpeakerName")));
    auto* Image = Cast<UImage>(WidgetField(DialogueWidget, TEXT("CharacterPortrait")));
    auto* Choices = Cast<UPanelWidget>(WidgetField(DialogueWidget, TEXT("PlayerVBox")));
    auto* Next = WidgetField(DialogueWidget, TEXT("NextButtonContainer"));
    UFunction* Reply = DialogueWidget->FindFunction(TEXT("GamepadReply"));
    auto* Index = Reply ? FindFProperty<FIntProperty>(Reply, TEXT("Index")) : nullptr;
    bool Blocked = true, Unable = true;
    if (!OldRoot || (!Cast<UTextBlock>(Text) && !Cast<URichTextBlock>(Text)) || !Name || !Image || !Choices || !Next
        || !Node(DialogueWidget, TEXT("CurrentNode")) || !Node(DialogueWidget, TEXT("CurrentPlayerNode"))
        || !ReadBool(DialogueWidget, TEXT("isUIInputBlocked"), Blocked)
        || !ReadBool(DialogueWidget, TEXT("Unable to Give Input"), Unable)
        || !Index || ReditusQuestNPCReflection::ParameterCount(Reply) != 1
        || !Index->HasAnyPropertyFlags(CPF_Parm) || Index->HasAnyPropertyFlags(CPF_OutParm | CPF_ReturnParm)
        || !ReditusQuestNPCReflection::DelegateSignature(DialogueWidget->FindFunction(TEXT("MenuConfirm")), false)
        || !ReditusQuestNPCReflection::DelegateSignature(DialogueWidget->FindFunction(TEXT("SimulateNextClicked")), false))
    {
        UE_LOG(LogReditusDialogueUI, Warning, TEXT("%s does not match the exported dialogue-widget contract; keeping its existing layout."), *DialogueWidget->GetClass()->GetName());
        return false;
    }
    auto* Skin = CreateWidget<UReditusDialoguePresentationWidget>(DialogueWidget->GetOwningPlayer(), StaticClass());
    auto* Root = DialogueWidget->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ReditusPresentationRoot"));
    if (!Skin || !Root) return false;
    Skin->Source = DialogueWidget; Skin->OriginalRoot = OldRoot;
    Skin->OriginalPortraitBounds = ExistingPortraitBounds(Image);
    Skin->SetVisibility(ESlateVisibility::Visible);
    // Stay layout-active so Construct/Tick still run, but do not paint the
    // presenter until the first synchronized state is ready.
    Skin->SetRenderOpacity(0.f);
    Skin->Body = Text; Skin->Speaker = Name; Skin->Portrait = Image;
    Skin->Replies = Choices; Skin->ContinueContainer = Next;
    Text->RemoveFromParent(); Name->RemoveFromParent(); Image->RemoveFromParent();
    // Keep legacy controls constructed: reply widgets still run their existing
    // Construct events, substitutions, selection and click handlers in this layer.
    // The separate host has no Blueprint visibility binding or animation. An old
    // root binding changing itself back to Visible cannot expose this subtree.
    auto* LegacyHost = DialogueWidget->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ReditusLegacyHost"));
    LegacyHost->SetPadding(FMargin(0));
    LegacyHost->SetVisibility(ESlateVisibility::Hidden);
    OldRoot->SetVisibility(ESlateVisibility::Hidden);
    LegacyHost->SetContent(OldRoot);
    Anchored(Root, LegacyHost, FAnchors(0, 0, 1, 1), 0);
    Anchored(Root, Skin, FAnchors(0, 0, 1, 1), 1);
    DialogueWidget->WidgetTree->RootWidget = Root;
    UE_LOG(LogReditusDialogueUI, Display, TEXT("Applied dialogue presentation to %s."), *DialogueWidget->GetClass()->GetName());
    return true;
}

TSharedRef<SWidget> UReditusDialoguePresentationWidget::RebuildWidget()
{
    if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this, TEXT("PresentationTree"));
    if (!WidgetTree->RootWidget) BuildLayout();
    return Super::RebuildWidget();
}

void UReditusDialoguePresentationWidget::BuildLayout()
{
    using namespace ReditusDialogueUI;
    ApplyTextStyle(); // Set the actual text controls before Slate builds them.
    auto* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
    WidgetTree->RootWidget = Canvas;
    Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    Anchored(Canvas, DialogueBackdrop(WidgetTree), FAnchors(.025f, .72f, .975f, .955f), 5);
    auto* PortraitSize = WidgetTree->ConstructWidget<USizeBox>();
    PortraitContainer = PortraitSize;
    PortraitSize->SetWidthOverride(OriginalPortraitBounds.X); PortraitSize->SetHeightOverride(OriginalPortraitBounds.Y);
    auto* PortraitScale = WidgetTree->ConstructWidget<UScaleBox>();
    PortraitScale->SetStretch(EStretch::ScaleToFit); PortraitScale->SetContent(Portrait);
    PortraitSize->SetContent(PortraitScale);
    auto* PortraitSlot = Anchored(Canvas, PortraitSize, FAnchors(.045f, .94f), 11);
    PortraitSlot->SetAlignment(FVector2D(0, 1)); PortraitSlot->SetAutoSize(true);

    // Original portrait and dialogue sit above the translucent background.
    auto* TextColumn = WidgetTree->ConstructWidget<UVerticalBox>();
    DialogueTextContainer = TextColumn;
    SpeakerFrame = Solid(WidgetTree, FLinearColor::Transparent);
    SpeakerFrame->SetContent(Speaker);
    TextColumn->AddChildToVerticalBox(SpeakerFrame)->SetPadding(FMargin(0, 0, 0, 9));
    TextColumn->AddChildToVerticalBox(Rule(WidgetTree))->SetPadding(FMargin(0, 0, 0, 16));
    TextColumn->AddChildToVerticalBox(Body)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    // Independent canvas bounds keep a tall portrait out of the text/footer's
    // height calculation. Preserve its Designer size instead of squeezing it.
    auto* BodySlot = Anchored(Canvas, TextColumn, FAnchors(.045f, .735f, .955f, .94f), 10);
    BodySlot->SetOffsets(FMargin(OriginalPortraitBounds.X + 26.f, 12.f, 12.f, 10.f));

    // Small cinematic controls, separate from the dialogue; collapsed at
    // construction so a normal NPC conversation never flashes a footer.
    auto* Footer = WidgetTree->ConstructWidget<UHorizontalBox>();
    SequenceControls = Footer;
    AdvanceButton = Button(WidgetTree, FText::FromString(TEXT("ENTER  Advance")), AdvanceLabel, 15);
    AutoButton = Button(WidgetTree, FText::FromString(TEXT("F6  Auto")), AutoLabel, 15);
    LogButton = Button(WidgetTree, FText::FromString(TEXT("F7  Log")), LogLabel, 15);
    SkipButton = Button(WidgetTree, FText::FromString(TEXT("SHIFT  Hold skip")), SkipLabel, 15);
    for (auto* B : {AdvanceButton, AutoButton, LogButton, SkipButton})
    {
        auto* S = Footer->AddChildToHorizontalBox(B); S->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); S->SetPadding(FMargin(4, 0));
    }
    AdvanceButton->OnClicked.AddDynamic(this, &UReditusDialoguePresentationWidget::Advance);
    AutoButton->OnClicked.AddDynamic(this, &UReditusDialoguePresentationWidget::ToggleAuto);
    LogButton->OnClicked.AddDynamic(this, &UReditusDialoguePresentationWidget::ToggleLog);
    SkipButton->OnPressed.AddDynamic(this, &UReditusDialoguePresentationWidget::SkipPressed);
    SkipButton->OnReleased.AddDynamic(this, &UReditusDialoguePresentationWidget::SkipReleased);
    Anchored(Canvas, Footer, FAnchors(.36f, .95f, .965f, .995f), 12);
    Footer->SetVisibility(ESlateVisibility::Collapsed);

    auto* ChoiceColumn = WidgetTree->ConstructWidget<UVerticalBox>();
    auto* ChoiceTitle = Label(WidgetTree, FText::FromString(TEXT("\u2500\u2500   \u25c6   YOUR RESPONSE   \u25c6   \u2500\u2500")), 14, Gold, true);
    ChoiceTitle->SetJustification(ETextJustify::Center);
    ChoiceColumn->AddChildToVerticalBox(ChoiceTitle)->SetPadding(FMargin(5, 0, 5, 12));
    auto* ChoiceScroll = WidgetTree->ConstructWidget<UScrollBox>();
    ChoiceRows = WidgetTree->ConstructWidget<UVerticalBox>(); ChoiceScroll->AddChild(ChoiceRows);
    ChoiceColumn->AddChildToVerticalBox(ChoiceScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    ChoiceFrame = GlassFrame(WidgetTree, ChoiceColumn, FMargin(22, 18));
    auto* ChoiceSlot = Anchored(Canvas, ChoiceFrame, FAnchors(.18f, .48f, .82f, .48f), 15);
    ChoiceSlot->SetAlignment(FVector2D(0, .5f)); ChoiceSlot->SetOffsets(FMargin(0, 0, 0, 260));
    ChoiceFrame->SetVisibility(ESlateVisibility::Collapsed);
    StatusLabel = Label(WidgetTree, FText::GetEmpty(), 15, Gold, true);
    auto* Status = Anchored(Canvas, StatusLabel, FAnchors(.05f, .96f, .35f, .99f), 14);
    Status->SetAlignment(FVector2D(0, 1));
    StatusLabel->SetVisibility(ESlateVisibility::Collapsed);

    auto* LogCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(); LogLayer = LogCanvas;
    auto* BackText = WidgetTree->ConstructWidget<UTextBlock>();
    auto* Back = WidgetTree->ConstructWidget<UButton>(); ButtonStyle(Back);
    FButtonStyle Shade; Shade.SetNormal(FSlateColorBrush(FLinearColor(0, 0, 0, .72f)));
    Shade.SetHovered(Shade.Normal); Shade.SetPressed(Shade.Normal); Back->SetStyle(Shade); Back->SetContent(BackText);
    Back->OnClicked.AddDynamic(this, &UReditusDialoguePresentationWidget::ToggleLog);
    Anchored(LogCanvas, Back, FAnchors(0, 0, 1, 1));
    auto* LogColumn = WidgetTree->ConstructWidget<UVerticalBox>();
    auto* LogHeader = WidgetTree->ConstructWidget<UHorizontalBox>();
    auto* Heading = Label(WidgetTree, FText::FromString(TEXT("CONVERSATION LOG")), 23, Gold, true);
    LogHeader->AddChildToHorizontalBox(Heading)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    UTextBlock* CloseText = nullptr;
    auto* Close = Button(WidgetTree, FText::FromString(TEXT("CLOSE  /  ESC")), CloseText, 16);
    Close->OnClicked.AddDynamic(this, &UReditusDialoguePresentationWidget::ToggleLog);
    LogHeader->AddChildToHorizontalBox(Close); LogColumn->AddChildToVerticalBox(LogHeader)->SetPadding(FMargin(0, 0, 0, 15));
    LogScroll = WidgetTree->ConstructWidget<UScrollBox>(); LogRows = WidgetTree->ConstructWidget<UVerticalBox>();
    LogScroll->AddChild(LogRows); LogColumn->AddChildToVerticalBox(LogScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    Anchored(LogCanvas, Frame(WidgetTree, LogColumn, FMargin(25)), FAnchors(.13f, .12f, .87f, .87f), 2);
    Anchored(Canvas, LogCanvas, FAnchors(0, 0, 1, 1), 30); LogCanvas->SetVisibility(ESlateVisibility::Collapsed);
}

void UReditusDialoguePresentationWidget::NativeConstruct()
{
    bPresentationReady = false;
    SetRenderOpacity(0.f);
    ApplyTextStyle();
    Super::NativeConstruct(); LastTime = FPlatformTime::Seconds();
    bool Keyboard = true;
    if (ReditusDialogueUI::ReadBool(UGameplayStatics::GetGameInstance(this), TEXT("UsingKeyboard"), Keyboard)) UsingGamepad = !Keyboard;
    if (GetOwningPlayer()) NeedTriggerRelease = GetOwningPlayer()->GetInputAnalogKeyState(EKeys::Gamepad_RightTriggerAxis) > .2f;
    if (FSlateApplication::IsInitialized())
    {
        InputProcessor = MakeShared<FReditusDialogueInputProcessor>(this);
        FSlateApplication::Get().RegisterInputPreProcessor(InputProcessor, 0); RegisteredInput = true;
    }
}

void UReditusDialoguePresentationWidget::NativeDestruct()
{
    if (RegisteredInput && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(InputProcessor);
    RegisteredInput = false; InputProcessor.Reset(); InputInactive();
    bPresentationReady = false;
    Super::NativeDestruct();
}

bool UReditusDialoguePresentationWidget::CanRouteInput() const
{
    auto* W = Source.Get();
    if (!bPresentationReady || !IsValid(W) || !W->IsInViewport() || !W->GetIsEnabled() || !ReditusDialogueUI::Shown(W)
        || !FSlateApplication::IsInitialized() || !FSlateApplication::Get().IsActive()) return false;
    auto* Player = W->GetOwningPlayer();
    return IsValid(Player) && Player->IsLocalController()
        && ReditusQuestNPCReflection::ReadObject(Player, TEXT("DialogueWidget")) == W
        && (W->HasUserFocus(Player) || W->HasUserFocusedDescendants(Player));
}

bool UReditusDialoguePresentationWidget::InputAllowed() const
{
    bool Blocked = true, Unable = true;
    // In the exported Blueprint, AllowInput reveals InputImage rather than
    // clearing the initial Unable-to-Give-Input setting.
    return CanRouteInput() && ReditusDialogueUI::ReadBool(Source.Get(), TEXT("isUIInputBlocked"), Blocked)
        && ReditusDialogueUI::ReadBool(Source.Get(), TEXT("Unable to Give Input"), Unable) && !Blocked
        && (!Unable || ReditusDialogueUI::Shown(ReditusDialogueUI::WidgetField(Source.Get(), TEXT("InputImage"))));
}

ReditusDialoguePresentationPolicy::Snapshot UReditusDialoguePresentationWidget::ReadSnapshot() const
{
    ReditusDialoguePresentationPolicy::Snapshot State;
    State.Active = CanRouteInput(); State.Blocked = !InputAllowed();
    State.Choices = Replies ? Replies->GetChildrenCount() : 0;
    State.ContinueReady = ReditusDialogueUI::Shown(ContinueContainer);
    State.Characters = ReditusDialogueUI::Plain(ReditusDialogueUI::TextOf(Body).ToString()).Len();
    const auto* N = ReditusDialogueUI::Node(Source.Get(), TEXT("CurrentNode"));
    const auto* P = ReditusDialogueUI::Node(Source.Get(), TEXT("CurrentPlayerNode"));
    State.Line = (uint64(uint32(N ? N->id : INDEX_NONE)) << 32) | uint32(P ? P->id : INDEX_NONE);
    State.ScriptedSequence = !N || N->PlayLevelSequence;
    auto* Sequence = ReditusDialogueUI::SequencePlayer(Source.Get());
    State.InLevelSequence = (N && N->PlayLevelSequence)
        || (IsValid(Sequence) && (Sequence->IsPlaying() || Sequence->IsPaused()));
    if (auto* Sound = Cast<UAudioComponent>(ReditusQuestNPCReflection::ReadObject(Source.Get(), TEXT("Sound2D")))) State.VoicePlaying = Sound->IsPlaying();
    return State;
}

void UReditusDialoguePresentationWidget::ApplyTextStyle()
{
    if (!StyledText && Body && Speaker)
    {
        FTextBlockStyle TextStyle;
        TextStyle.SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 26));
        TextStyle.SetColorAndOpacity(FSlateColor(ReditusDialogueUI::Cream));
        TextStyle.SetShadowOffset(FVector2D(1, 1)); TextStyle.SetShadowColorAndOpacity(FLinearColor(0, 0, 0, .8f));
        if (auto* Rich = Cast<URichTextBlock>(Body)) { Rich->SetDefaultTextStyle(TextStyle); Rich->SetAutoWrapText(true); }
        if (auto* Text = Cast<UTextBlock>(Body))
        {
            Text->SetFont(TextStyle.Font); Text->SetColorAndOpacity(TextStyle.ColorAndOpacity); Text->SetAutoWrapText(true);
            Text->SetShadowOffset(FVector2D(1, 2)); Text->SetShadowColorAndOpacity(FLinearColor(0, 0, 0, .9f));
        }
        Speaker->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 23)); Speaker->SetColorAndOpacity(FSlateColor(ReditusDialogueUI::Gold));
        StyledText = true;
    }
}

void UReditusDialoguePresentationWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
    Super::NativeTick(Geometry, DeltaTime);
    const double Now = FPlatformTime::Seconds(); const double Delta = FMath::Clamp(Now - LastTime, 0.0, .25); LastTime = Now;
    ApplyTextStyle();
    if (PortraitContainer) PortraitContainer->SetVisibility(ReditusDialogueUI::Shown(Portrait) ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    if (DialogueTextContainer)
        if (auto* TextCanvasSlot = Cast<UCanvasPanelSlot>(DialogueTextContainer->Slot))
            TextCanvasSlot->SetOffsets(FMargin(ReditusDialogueUI::Shown(Portrait) ? OriginalPortraitBounds.X + 26.f : 0.f, 12.f, 12.f, 10.f));
    if (SpeakerFrame) SpeakerFrame->SetVisibility(ReditusDialogueUI::Shown(Speaker) && !Speaker->GetText().IsEmpty() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    // Parent Blueprint Construct has now populated the source dialogue. Sync
    // portrait, name, replies and cinematic controls before the first reveal.
    const bool bFirstPresentation = !bPresentationReady;
    bPresentationReady = true;
    RefreshChoices(); UpdateLog(Delta); RefreshControls();
    if (bFirstPresentation) SetRenderOpacity(1.f);
    const auto State = ReadSnapshot();
    if (Playback.Tick(Delta, State))
    {
        // Automatic actions never call GamepadReply or select an answer.
        ReditusQuestNPCReflection::CallNoArguments(Source.Get(), TEXT("SimulateNextClicked"));
    }
}

bool UReditusDialoguePresentationWidget::CallReply(int32 Index)
{
    UFunction* Function = Source.IsValid() ? Source->FindFunction(TEXT("GamepadReply")) : nullptr;
    auto* Parameter = Function ? FindFProperty<FIntProperty>(Function, TEXT("Index")) : nullptr;
    if (!Parameter || ReditusQuestNPCReflection::ParameterCount(Function) != 1
        || !Parameter->HasAnyPropertyFlags(CPF_Parm) || Parameter->HasAnyPropertyFlags(CPF_OutParm | CPF_ReturnParm)) return false;
    FStructOnScope Parameters(Function); Parameter->SetPropertyValue_InContainer(Parameters.GetStructMemory(), Index);
    Source->ProcessEvent(Function, Parameters.GetStructMemory()); return true;
}

void UReditusDialoguePresentationWidget::RestoreSourceFocus()
{
    auto* Widget = Source.Get();
    auto* Player = IsValid(Widget) ? Widget->GetOwningPlayer() : nullptr;
    // A reply can close this widget, start a battle/sequence, or open another
    // widget. Never steal focus back from the next screen after that transition.
    if (IsValid(Player) && Widget->IsInViewport() && ReditusDialogueUI::Shown(Widget)
        && ReditusQuestNPCReflection::ReadObject(Player, TEXT("DialogueWidget")) == Widget)
        Widget->SetUserFocus(Player);
}

FReply UReditusDialoguePresentationWidget::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
    (void)Geometry;
    // Normal NPCs have no footer; clicking the floating dialogue still uses the
    // original confirm handler. Choice buttons consume their own clicks.
    if (Event.GetEffectingButton() == EKeys::LeftMouseButton && !Playback.Log) Advance();
    RestoreSourceFocus(); return FReply::Handled();
}

void UReditusDialoguePresentationWidget::Advance()
{
    if (!InputAllowed() || Playback.Log || !ReditusDialogueUI::Shown(ContinueContainer) || !Replies || Replies->GetChildrenCount() > 0) return;
    Playback.ManualInput(); ReditusQuestNPCReflection::CallNoArguments(Source.Get(), TEXT("MenuConfirm")); RestoreSourceFocus();
}

void UReditusDialoguePresentationWidget::ToggleAuto()
{ if (CanRouteInput() && ReadSnapshot().InLevelSequence) Playback.Auto = !Playback.Auto; RestoreSourceFocus(); }

void UReditusDialoguePresentationWidget::ToggleLog()
{
    const auto State = ReadSnapshot();
    if (!Playback.Log && (!State.InLevelSequence || !State.Active || State.Blocked || State.ScriptedSequence || (!State.ContinueReady && State.Choices == 0))) return;
    Playback.ToggleLog(); MouseHeld = false; UpdateHeld();
    if (LogLayer) LogLayer->SetVisibility(Playback.Log ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    if (Playback.Log && LogScroll) LogScroll->ScrollToEnd(); RestoreSourceFocus();
}

void UReditusDialoguePresentationWidget::UpdateHeld()
{ Playback.SetHeld(KeyHeld || TriggerHeld || MouseHeld); }
void UReditusDialoguePresentationWidget::SkipPressed()
{ if (CanRouteInput() && ReadSnapshot().InLevelSequence && !Playback.Log) { MouseHeld = true; UpdateHeld(); } RestoreSourceFocus(); }
void UReditusDialoguePresentationWidget::SkipReleased()
{ MouseHeld = false; UpdateHeld(); }
void UReditusDialoguePresentationWidget::InputInactive()
{
    KeyHeld = false; TriggerHeld = false; MouseHeld = false;
    NeedTriggerRelease = GetOwningPlayer() && GetOwningPlayer()->GetInputAnalogKeyState(EKeys::Gamepad_RightTriggerAxis) > .2f;
    UpdateHeld();
    ReditusDialoguePresentationPolicy::Snapshot Inactive; Playback.Tick(0, Inactive);
    if (Playback.Log) { Playback.ToggleLog(); if (LogLayer) LogLayer->SetVisibility(ESlateVisibility::Collapsed); }
}

bool UReditusDialoguePresentationWidget::RouteKeyDown(const FKey& Key, bool Repeat)
{
    if (!CanRouteInput()) return false;
    UsingGamepad = Key.IsGamepadKey();
    if (!ReadSnapshot().InLevelSequence) return false;
    if (Key == EKeys::F6 || Key == EKeys::Gamepad_FaceButton_Top) { if (!Repeat) ToggleAuto(); return true; }
    if (Key == EKeys::F7 || Key == EKeys::Gamepad_FaceButton_Left) { if (!Repeat) ToggleLog(); return true; }
    if (Key == EKeys::LeftShift || Key == EKeys::RightShift || Key == EKeys::Gamepad_RightTrigger)
    { if (!Repeat && !Playback.Log) { KeyHeld = true; UpdateHeld(); } return true; }
    if (Playback.Log)
    {
        if (!Repeat && (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right)) ToggleLog();
        if (LogScroll && (Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up)) LogScroll->SetScrollOffset(FMath::Max(0.f, LogScroll->GetScrollOffset() - 60.f));
        if (LogScroll && (Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Down)) LogScroll->SetScrollOffset(LogScroll->GetScrollOffset() + 60.f);
        return true; // Do not let the old widget confirm a hidden choice while the log is open.
    }
    if (ReditusDialogueUI::ConfirmKey(Key) && !Repeat && InputAllowed()) Playback.ManualInput();
    return false; // Existing keyboard/gamepad confirm and navigation retain their current route.
}

bool UReditusDialoguePresentationWidget::RouteKeyUp(const FKey& Key)
{
    const bool Active = CanRouteInput();
    if (Key == EKeys::LeftShift || Key == EKeys::RightShift || Key == EKeys::Gamepad_RightTrigger)
    { const bool OwnedPress = KeyHeld; KeyHeld = false; UpdateHeld(); return Active && OwnedPress; }
    // Key releases must reach the legacy widget/controller, including its
    // IsSelectReplyKeyDown reset, even if the log opened after the key press.
    return false;
}

bool UReditusDialoguePresentationWidget::RouteAnalog(const FKey& Key, float Value)
{
    const bool Active = CanRouteInput();
    if (Key == EKeys::Gamepad_RightTriggerAxis)
    {
        if (!ReadSnapshot().InLevelSequence)
        {
            TriggerHeld = false; NeedTriggerRelease = Value > .2f; UpdateHeld(); return false;
        }
        if (Value < .2f) { TriggerHeld = false; NeedTriggerRelease = false; UpdateHeld(); }
        else if (Active && Value > .55f && !NeedTriggerRelease && !Playback.Log) { TriggerHeld = true; UpdateHeld(); UsingGamepad = true; }
        return Active && Value > .2f; // Let neutral/release values reach the underlying input state.
    }
    if (Active && Playback.Log && Key == EKeys::Gamepad_LeftY && LogScroll && FMath::Abs(Value) > .2f)
        LogScroll->SetScrollOffset(FMath::Max(0.f, LogScroll->GetScrollOffset() - Value * 14.f));
    return false; // Preserve raw analog releases; log navigation key presses are handled above.
}

void UReditusDialoguePresentationWidget::SelectReply(int32 Index, UUserWidget* ExpectedReply)
{
    if (!InputAllowed() || Playback.Log || !Replies || Replies->GetChildAt(Index) != ExpectedReply) return;
    if (auto* P = FindFProperty<FIntProperty>(Source->GetClass(), TEXT("currentlySelectedReply")))
    { P->SetPropertyValue_InContainer(Source.Get(), Index); ReditusQuestNPCReflection::CallNoArguments(Source.Get(), TEXT("HighlightSelectedReply")); }
}

void UReditusDialoguePresentationWidget::ChooseReply(int32 Index, UUserWidget* ExpectedReply)
{
    if (!InputAllowed() || Playback.Log || !Replies || !IsValid(ExpectedReply) || Replies->GetChildAt(Index) != ExpectedReply) return;
    SelectReply(Index, ExpectedReply); Playback.ManualInput(); CallReply(Index); RestoreSourceFocus();
}

void UReditusDialoguePresentationWidget::RefreshChoices()
{
    if (!Replies || !ChoiceRows || !ChoiceFrame) return;
    const int32 Count = Replies->GetChildrenCount();
    bool Changed = Count != ChoiceProxies.Num();
    if (!Changed) for (int32 I = 0; I < Count; ++I)
        if (ChoiceProxies[I]->OriginalReply.Get() != Replies->GetChildAt(I)) { Changed = true; break; }
    if (Changed)
    {
        ChoiceRows->ClearChildren(); ChoiceProxies.Reset();
        for (int32 I = 0; I < Count; ++I)
        {
            auto* Original = Cast<UUserWidget>(Replies->GetChildAt(I));
            if (!Original) continue;
            auto* Proxy = NewObject<UReditusDialogueChoiceProxy>(this);
            Proxy->Owner = this; Proxy->OriginalReply = Original; Proxy->Index = I;
            Proxy->Button = ReditusDialogueUI::Button(WidgetTree, FText::GetEmpty(), Proxy->Text, 21);
            Proxy->Text->SetAutoWrapText(true); Proxy->Text->SetJustification(ETextJustify::Left);
            Proxy->Button->OnClicked.AddDynamic(Proxy, &UReditusDialogueChoiceProxy::Clicked);
            Proxy->Button->OnHovered.AddDynamic(Proxy, &UReditusDialogueChoiceProxy::Hovered);
            ChoiceRows->AddChildToVerticalBox(Proxy->Button)->SetPadding(FMargin(0, 3));
            ChoiceProxies.Add(Proxy);
        }
    }
    int32 Selected = 0;
    if (Source.IsValid()) if (auto* P = FindFProperty<FIntProperty>(Source->GetClass(), TEXT("currentlySelectedReply"))) Selected = P->GetPropertyValue_InContainer(Source.Get());
    for (auto* Proxy : ChoiceProxies)
    {
        auto* Original = Proxy->OriginalReply.Get();
        UWidget* ReplyText = Original ? Cast<UWidget>(ReditusQuestNPCReflection::ReadObject(Original, TEXT("ReplyTextBlock"))) : nullptr;
        const FString Prefix = Proxy->Index == Selected ? TEXT("\u25c6   ") : TEXT("\u00b7    ");
        const FString Text = Prefix + ReditusDialogueUI::Plain(ReditusDialogueUI::TextOf(ReplyText).ToString());
        if (Proxy->Text->GetText().ToString() != Text) Proxy->Text->SetText(FText::FromString(Text));
        ReditusDialogueUI::ButtonStyle(Proxy->Button, Proxy->Index == Selected);
        Proxy->Text->SetColorAndOpacity(FSlateColor(Proxy->Index == Selected ? ReditusDialogueUI::Accent : ReditusDialogueUI::Cream));
        Proxy->Button->SetIsEnabled(InputAllowed() && !Playback.Log);
    }
    ChoiceFrame->SetVisibility(Count > 0 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    if (auto* ChoiceCanvasSlot = Cast<UCanvasPanelSlot>(ChoiceFrame->Slot))
        ChoiceCanvasSlot->SetOffsets(FMargin(0, 0, 0, FMath::Clamp(86.f + Count * 72.f, 158.f, 330.f)));
}

void UReditusDialoguePresentationWidget::RefreshControls()
{
    if (!AdvanceButton) return;
    const auto State = ReadSnapshot();
    if (SequenceControls) SequenceControls->SetVisibility(State.InLevelSequence ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    StatusLabel->SetVisibility(State.InLevelSequence ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    if (!State.InLevelSequence)
    {
        Playback.Auto = false;
        KeyHeld = false; TriggerHeld = false; MouseHeld = false;
        NeedTriggerRelease = GetOwningPlayer() && GetOwningPlayer()->GetInputAnalogKeyState(EKeys::Gamepad_RightTriggerAxis) > .2f;
        UpdateHeld();
        if (Playback.Log)
        {
            Playback.ToggleLog();
            if (LogLayer) LogLayer->SetVisibility(ESlateVisibility::Collapsed);
        }
    }
    const bool Ready = State.Active && !State.Blocked && !State.ScriptedSequence;
    AdvanceButton->SetIsEnabled(State.Active && !State.Blocked && !Playback.Log && State.ContinueReady && State.Choices == 0);
    AutoButton->SetIsEnabled(State.Active);
    LogButton->SetIsEnabled(Playback.Log || (Ready && (State.ContinueReady || State.Choices > 0)));
    SkipButton->SetIsEnabled(Ready && !Playback.Log && State.Choices == 0);
    const TCHAR* AutoState = !Playback.Auto ? TEXT("Off") : (State.Choices > 0 || Playback.Log || !Ready) ? TEXT("Paused") : TEXT("On");
    AutoLabel->SetText(FText::FromString(FString::Printf(TEXT("%s  Auto %s"), UsingGamepad ? TEXT("Y") : TEXT("F6"), AutoState)));
    AdvanceLabel->SetText(FText::FromString(UsingGamepad ? TEXT("A  Advance") : TEXT("ENTER  Advance")));
    LogLabel->SetText(FText::FromString(UsingGamepad ? TEXT("X  Log") : TEXT("F7  Log")));
    SkipLabel->SetText(FText::FromString(UsingGamepad ? TEXT("RT  Hold skip") : TEXT("SHIFT  Hold skip")));
    AutoLabel->SetColorAndOpacity(FSlateColor(Playback.Auto ? ReditusDialogueUI::Gold : ReditusDialogueUI::Cream));
    StatusLabel->SetText(FText::FromString(State.Choices > 0 ? TEXT("Awaiting your response") : Playback.IsSkipping() ? TEXT("Skipping dialogue") : TEXT("")));
}

void UReditusDialoguePresentationWidget::UpdateLog(double Delta)
{
    if (!Body || !Speaker || !LogRows || !ReditusDialogueUI::Shown(Body)) return;
    const FString Text = ReditusDialogueUI::Plain(ReditusDialogueUI::TextOf(Body).ToString());
    const FString Name = Speaker->GetText().ToString();
    if (Text.IsEmpty()) return;
    const uint64 Line = ReadSnapshot().Line;
    if (Line != CandidateLine || Name != CandidateSpeaker || (!Text.StartsWith(CandidateText) && !CandidateText.StartsWith(Text)))
    { CandidateLine = Line; CandidateSpeaker = Name; CandidateText = Text; CandidateAge = 0; }
    else { CandidateText = Text; CandidateAge += Delta; }
    if (CandidateAge < .10) return;
    if (!LastLogText || Line != LoggedLine || (!CandidateText.StartsWith(LoggedText) && !LoggedText.StartsWith(CandidateText)))
    {
        auto* Heading = ReditusDialogueUI::Label(WidgetTree, FText::FromString(Name), 17, ReditusDialogueUI::Gold, true);
        LogRows->AddChildToVerticalBox(Heading)->SetPadding(FMargin(4, 12, 4, 4));
        LastLogText = ReditusDialogueUI::Label(WidgetTree, FText::FromString(Text), 21);
        LastLogText->SetAutoWrapText(true); LogRows->AddChildToVerticalBox(LastLogText)->SetPadding(FMargin(4, 0, 15, 15));
        LoggedLine = Line;
    }
    LastLogText->SetText(FText::FromString(CandidateText)); LoggedText = CandidateText;
    if (LogRows->GetChildrenCount() > 240) { LogRows->RemoveChildAt(0); LogRows->RemoveChildAt(0); }
}

bool UReditusDialoguePresentationLibrary::ApplyReditusDialoguePresentation(UDialogueUserWidget* DialogueWidget)
{ return UReditusDialoguePresentationWidget::Install(DialogueWidget); }
