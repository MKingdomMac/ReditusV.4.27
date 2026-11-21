#include "ReditusMovementPrompt.h"

#include "ReditusTutorialSkin.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"
#include "UObject/ConstructorHelpers.h"

namespace ReditusTutorialWidgets
{
    const FLinearColor Gold(1.f, .78f, .40f);
    const FLinearColor WarmWhite(1.f, .95f, .85f);

    FSlateBrush MakeBrush(const ReditusHUDSkin::Image& Art, FMargin Slice = FMargin(0.f), float Scale = 1.f)
    {
        UTexture2D* Texture = UTexture2D::CreateTransient(Art.Width, Art.Height, PF_B8G8R8A8);
        if (!Texture || !Texture->PlatformData || Texture->PlatformData->Mips.Num() == 0)
        {
            return FSlateColorBrush(FLinearColor(.012f, .025f, .045f, .9f));
        }

        Texture->SRGB = true;
        Texture->NeverStream = true;
        Texture->Filter = TF_Bilinear;
        Texture->AddressX = TA_Clamp;
        Texture->AddressY = TA_Clamp;
        Texture->LODGroup = TEXTUREGROUP_UI;
        FTexture2DMipMap& Mip = Texture->PlatformData->Mips[0];
        FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
        if (Pixels)
        {
            for (int32 Index = 0; Index < Art.Width * Art.Height; ++Index)
            {
                const auto& Ink = Art.Pixels[Index];
                Pixels[Index] = FColor(Ink.R, Ink.G, Ink.B, Ink.A);
            }
        }
        Mip.BulkData.Unlock();
        if (!Pixels)
        {
            return FSlateColorBrush(FLinearColor(.012f, .025f, .045f, .9f));
        }
        Texture->UpdateResource();

        FSlateBrush Brush;
        Brush.SetResourceObject(Texture);
        Brush.ImageSize = FVector2D(Art.Width * Scale, Art.Height * Scale);
        Brush.Margin = Slice;
        Brush.DrawAs = Slice.Left > 0.f ? ESlateBrushDrawType::Box : ESlateBrushDrawType::Image;
        return Brush;
    }

    UTextBlock* MakeText(UWidgetTree& Tree, int32 Size, const FLinearColor& Color, UFont* PixelFont = nullptr,
        const TCHAR* Typeface = TEXT("Bold"))
    {
        UTextBlock* TextBlock = Tree.ConstructWidget<UTextBlock>();
        TextBlock->SetFont(PixelFont ? FSlateFontInfo(PixelFont, Size) : FCoreStyle::GetDefaultFontStyle(Typeface, Size));
        TextBlock->SetColorAndOpacity(FSlateColor(Color));
        TextBlock->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, .85f));
        TextBlock->SetShadowOffset(FVector2D(1.f, 2.f));
        return TextBlock;
    }

    USizeBox* MakeArt(UWidgetTree& Tree, const FSlateBrush& Brush, float Width, float Height)
    {
        UImage* Artwork = Tree.ConstructWidget<UImage>();
        Artwork->SetBrush(Brush);
        USizeBox* Bounds = Tree.ConstructWidget<USizeBox>();
        Bounds->SetWidthOverride(Width);
        Bounds->SetHeightOverride(Height);
        Bounds->SetContent(Artwork);
        return Bounds;
    }

    UButton* MakeTutorialButton(UWidgetTree& Tree)
    {
        UButton* Button = Tree.ConstructWidget<UButton>();
        FButtonStyle Style;
        Style.SetNormal(FSlateColorBrush(FLinearColor(.067f, .12f, .18f, 0.f)));
        Style.SetHovered(FSlateColorBrush(FLinearColor(.24f, .16f, .055f, .72f)));
        Style.SetPressed(FSlateColorBrush(FLinearColor(.38f, .25f, .07f, .82f)));
        Style.NormalPadding = Style.PressedPadding = FMargin(8.f, 4.f);
        Button->SetStyle(Style);
        Button->IsFocusable = false;
        return Button;
    }

    void AddControl(UWidgetTree& Tree, UHorizontalBox& Row, const FSlateBrush& KeyBrush, float Size,
        USizeBox*& GlyphBox, UImage*& Glyph, UBorder*& Cap, UTextBlock*& Key)
    {
        GlyphBox = Tree.ConstructWidget<USizeBox>();
        GlyphBox->SetWidthOverride(Size);
        GlyphBox->SetHeightOverride(Size);
        Glyph = Tree.ConstructWidget<UImage>();
        GlyphBox->SetContent(Glyph);
        UHorizontalBoxSlot* GlyphSlot = Row.AddChildToHorizontalBox(GlyphBox);
        GlyphSlot->SetVerticalAlignment(VAlign_Center);
        GlyphSlot->SetPadding(FMargin(12.f, 0.f, 0.f, 0.f));

        Cap = Tree.ConstructWidget<UBorder>();
        Cap->SetBrush(KeyBrush);
        Cap->SetPadding(FMargin(13.f, 6.f));
        Key = MakeText(Tree, 17, WarmWhite);
        Cap->SetContent(Key);
        UHorizontalBoxSlot* KeySlot = Row.AddChildToHorizontalBox(Cap);
        KeySlot->SetVerticalAlignment(VAlign_Center);
        KeySlot->SetPadding(FMargin(12.f, 0.f, 0.f, 0.f));
    }

    void RefreshControl(USizeBox& Box, UImage& Glyph, UBorder& Cap, UTextBlock& Key,
        UTexture2D* Texture, const FText& Label, bool bShowFallback)
    {
        Box.SetVisibility(Texture ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
        Glyph.SetBrushFromTexture(Texture);
        Cap.SetVisibility(!Texture && bShowFallback ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
        Key.SetText(Label);
    }
}

UReditusMovementPromptWidget::UReditusMovementPromptWidget(const FObjectInitializer& Initializer)
    : Super(Initializer)
{
    static ConstructorHelpers::FObjectFinderOptional<UFont> ExistingFont(
        TEXT("/Game/DialogueSystem/Font/final_fantasy_36_font_Font.final_fantasy_36_font_Font"));
    HeadingFont = ExistingFont.Get();
    bIsFocusable = true;
}

void UReditusMovementPromptWidget::SetGlyph(UTexture2D* Texture, bool bGamepad)
{
    CurrentGlyph = Texture;
    bGamepadGlyph = bGamepad;
    Card = ECard::Movement;
    RefreshCard();
}

void UReditusMovementPromptWidget::SetBlockPrompt(UTexture2D* Texture, const FText& KeyText)
{
    CurrentGlyph = Texture;
    KeyLabel = KeyText;
    Card = ECard::Block;
    RefreshCard();
}

void UReditusMovementPromptWidget::SetGuidedPrompt(const FText& Caption, UTexture2D* Texture,
    const FText& KeyText, bool bTargetSelection)
{
    Card = ECard::Guided;
    GuidedCaption = Caption;
    CurrentGlyph = Texture;
    KeyLabel = KeyText;
    bSelectTarget = bTargetSelection;
    RefreshCard();
}

void UReditusMovementPromptWidget::SetDialogue(const FText& Title, const FText& Message, bool bShowShield,
    const FText& ActionCaption, int32 StaminaExampleMax)
{
    Card = ECard::Dialogue;
    TitleText = Title;
    BodyText = Message;
    DialogueActionCaption = ActionCaption;
    bDialogueShield = bShowShield;
    if (EnemyStaminaExampleMax != StaminaExampleMax)
    {
        EnemyStaminaExampleMax = StaminaExampleMax;
        RefreshStaminaExample();
    }
    RefreshCard();
}

void UReditusMovementPromptWidget::RefreshStaminaExample()
{
    using namespace ReditusTutorialWidgets;
    if (!StaminaExample)
    {
        return;
    }

    StaminaExample->ClearChildren();
    const bool bShow = EnemyStaminaExampleMax > 0;
    StaminaExample->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    if (!bShow)
    {
        return;
    }

    UTextBlock* Label = MakeText(*WidgetTree, 17, Gold, HeadingFont);
    Label->SetText(NSLOCTEXT("ReditusBattleTutorial", "EnemyStamina", "Enemy Stamina"));
    StaminaExample->AddChildToVerticalBox(Label)->SetHorizontalAlignment(HAlign_Center);

    UHorizontalBox* Examples = WidgetTree->ConstructWidget<UHorizontalBox>();
    UVerticalBoxSlot* ExamplesSlot = StaminaExample->AddChildToVerticalBox(Examples);
    ExamplesSlot->SetHorizontalAlignment(HAlign_Center);
    ExamplesSlot->SetPadding(FMargin(0.f, 5.f, 0.f, 0.f));
    const bool bGauge = EnemyStaminaExampleMax > 6;
    for (bool bFull : {true, false})
    {
        UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
        Examples->AddChildToHorizontalBox(Column)->SetPadding(FMargin(12.f, 0.f, 12.f, 0.f));
        const auto Art = ReditusTutorialSkin::StaminaSample(EnemyStaminaExampleMax, bFull);
        USizeBox* Bar = MakeArt(*WidgetTree, MakeBrush(Art), bGauge ? 118.f : Art.Width * 1.25f,
            bGauge ? 14.f : 20.f);
        Column->AddChildToVerticalBox(Bar)->SetHorizontalAlignment(HAlign_Center);
        UTextBlock* State = MakeText(*WidgetTree, 14, WarmWhite);
        State->SetText(bFull ? NSLOCTEXT("ReditusBattleTutorial", "StaminaFull", "Full") :
            NSLOCTEXT("ReditusBattleTutorial", "StaminaEmpty", "Empty (Staggered)"));
        UVerticalBoxSlot* StatePlacement = Column->AddChildToVerticalBox(State);
        StatePlacement->SetHorizontalAlignment(HAlign_Center);
        StatePlacement->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
    }
}

void UReditusMovementPromptWidget::SetContinueGlyph(UTexture2D* Texture, const FText& KeyText)
{
    ContinueTexture = Texture;
    ContinueLabel = KeyText;
    if (ContinueGlyphBox)
    {
        ReditusTutorialWidgets::RefreshControl(*ContinueGlyphBox, *ContinueGlyph, *ContinueKeyCap,
            *ContinueKeyText, ContinueTexture, ContinueLabel, true);
    }
}

void UReditusMovementPromptWidget::OnContinueClicked()
{
    ContinueRequested.ExecuteIfBound();
}

void UReditusMovementPromptWidget::OnPreviousTargetClicked()
{
    PreviousTargetRequested.ExecuteIfBound();
}

void UReditusMovementPromptWidget::OnNextTargetClicked()
{
    NextTargetRequested.ExecuteIfBound();
}

void UReditusMovementPromptWidget::RefreshCard()
{
    if (!PromptPlacement)
    {
        return;
    }

    const bool bDialogue = Card == ECard::Dialogue;
    const bool bBlock = Card == ECard::Block;
    const bool bGuided = Card == ECard::Guided;
    SetVisibility(bDialogue || bGuided ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::HitTestInvisible);
    Scrim->SetVisibility(bDialogue || bGuided ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    Scrim->SetRenderOpacity(bGuided ? .24f : 1.f);
    DialogueLayout->SetVisibility(bDialogue ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    PromptRow->SetVisibility(bDialogue ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
    PromptButton->SetVisibility(bGuided ? ESlateVisibility::Visible : ESlateVisibility::HitTestInvisible);
    const ESlateVisibility ArrowVisibility = bGuided && bSelectTarget ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
    PreviousTargetButton->SetVisibility(ArrowVisibility);
    NextTargetButton->SetVisibility(ArrowVisibility);
    PromptPlacement->SetAutoSize(!bDialogue);
    if (bDialogue)
    {
        PromptPlacement->SetAnchors(FAnchors(.065f, .21f, .935f, .88f));
        PromptPlacement->SetAlignment(FVector2D(0.f, 0.f));
        PromptPlacement->SetOffsets(FMargin(0.f));
        DialogueShield->SetVisibility(bDialogueShield ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
        DialogueHeading->SetText(TitleText);
        DialogueBody->SetText(BodyText);
        DialogueAction->SetText(DialogueActionCaption.IsEmpty() ?
            NSLOCTEXT("ReditusMovementPrompt", "Continue", "Continue") : DialogueActionCaption);
        SetContinueGlyph(ContinueTexture, ContinueLabel);
        return;
    }

    const float GlyphSize = bBlock || bGuided ? 38.f : 57.f;
    GlyphBounds->SetWidthOverride(GlyphSize);
    GlyphBounds->SetHeightOverride(GlyphSize);
    Shield->SetVisibility(bBlock ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    PromptPlacement->SetAnchors(FAnchors(.5f, bBlock ? .66f : bGuided ? .73f : 1.f));
    PromptPlacement->SetAlignment(FVector2D(.5f, bBlock || bGuided ? .5f : 1.f));
    PromptPlacement->SetPosition(FVector2D(0.f, bBlock || bGuided ? 0.f : -65.f));
    ReditusTutorialWidgets::RefreshControl(*GlyphBounds, *GlyphImage, *KeyCap, *KeyCaption,
        CurrentGlyph, KeyLabel, bBlock || bGuided);

    FText Caption = NSLOCTEXT("ReditusMovementPrompt", "Move", "Move");
    if (bBlock)
    {
        Caption = NSLOCTEXT("ReditusMovementPrompt", "BlockNow", "Block now");
    }
    else if (bGuided)
    {
        Caption = GuidedCaption;
    }
    else if (!CurrentGlyph)
    {
        Caption = bGamepadGlyph ? NSLOCTEXT("ReditusMovementPrompt", "MoveWithStick", "Move  [Left stick]") :
            NSLOCTEXT("ReditusMovementPrompt", "MoveWithKeyboard", "Move  [WASD]");
    }
    PromptCaption->SetText(Caption);
}

TSharedRef<SWidget> UReditusMovementPromptWidget::RebuildWidget()
{
    using namespace ReditusTutorialWidgets;
    if (!WidgetTree)
    {
        WidgetTree = NewObject<UWidgetTree>(this);
    }
    if (WidgetTree->RootWidget)
    {
        RefreshCard();
        return Super::RebuildWidget();
    }

    UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
    WidgetTree->RootWidget = Canvas;
    UBorder* Shade = WidgetTree->ConstructWidget<UBorder>();
    Shade->SetBrush(FSlateColorBrush(FLinearColor(.0025f, .006f, .014f, .47f)));
    UCanvasPanelSlot* ShadeSlot = Canvas->AddChildToCanvas(Shade);
    ShadeSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
    ShadeSlot->SetOffsets(FMargin(0.f));
    Scrim = Shade;

    const FSlateBrush Frame = MakeBrush(ReditusTutorialSkin::Plate(), FMargin(1.f / 6.f));
    const FSlateBrush KeyBrush = MakeBrush(ReditusHUDSkin::Frame(false, true), FMargin(1.f / 6.f), .54f);
    const FSlateBrush ShieldBrush = MakeBrush(ReditusTutorialSkin::Shield());
    UScaleBox* Scale = WidgetTree->ConstructWidget<UScaleBox>();
    Scale->SetStretch(EStretch::ScaleToFit);
    Scale->SetStretchDirection(EStretchDirection::DownOnly);
    PromptPlacement = Canvas->AddChildToCanvas(Scale);
    UBorder* Backplate = WidgetTree->ConstructWidget<UBorder>();
    Backplate->SetBrush(Frame);
    Backplate->SetPadding(FMargin(24.f, 14.f, 24.f, 15.f));
    Scale->SetContent(Backplate);
    UVerticalBox* CardStack = WidgetTree->ConstructWidget<UVerticalBox>();
    Backplate->SetContent(CardStack);

    UHorizontalBox* PromptControls = WidgetTree->ConstructWidget<UHorizontalBox>();
    PromptRow = PromptControls;
    CardStack->AddChildToVerticalBox(PromptControls);
    UButton* PreviousTarget = MakeTutorialButton(*WidgetTree);
    UTextBlock* PreviousLabel = MakeText(*WidgetTree, 24, Gold);
    PreviousLabel->SetText(NSLOCTEXT("ReditusMovementPrompt", "PreviousArrow", "<"));
    PreviousTarget->SetContent(PreviousLabel);
    PreviousTarget->OnClicked.AddDynamic(this, &UReditusMovementPromptWidget::OnPreviousTargetClicked);
    PromptControls->AddChildToHorizontalBox(PreviousTarget)->SetVerticalAlignment(VAlign_Center);
    PreviousTargetButton = PreviousTarget;

    UButton* MainPrompt = MakeTutorialButton(*WidgetTree);
    MainPrompt->OnClicked.AddDynamic(this, &UReditusMovementPromptWidget::OnContinueClicked);
    PromptControls->AddChildToHorizontalBox(MainPrompt)->SetVerticalAlignment(VAlign_Center);
    PromptButton = MainPrompt;
    UHorizontalBox* Prompt = WidgetTree->ConstructWidget<UHorizontalBox>();
    MainPrompt->SetContent(Prompt);
    Shield = MakeArt(*WidgetTree, ShieldBrush, 49.f, 49.f);
    UHorizontalBoxSlot* ShieldSlot = Prompt->AddChildToHorizontalBox(Shield);
    ShieldSlot->SetVerticalAlignment(VAlign_Center);
    ShieldSlot->SetPadding(FMargin(0.f, 0.f, 13.f, 0.f));
    PromptCaption = MakeText(*WidgetTree, 24, Gold, HeadingFont);
    Prompt->AddChildToHorizontalBox(PromptCaption)->SetVerticalAlignment(VAlign_Center);
    AddControl(*WidgetTree, *Prompt, KeyBrush, 44.f, GlyphBounds, GlyphImage, KeyCap, KeyCaption);

    UButton* NextTarget = MakeTutorialButton(*WidgetTree);
    UTextBlock* NextLabel = MakeText(*WidgetTree, 24, Gold);
    NextLabel->SetText(NSLOCTEXT("ReditusMovementPrompt", "NextArrow", ">"));
    NextTarget->SetContent(NextLabel);
    NextTarget->OnClicked.AddDynamic(this, &UReditusMovementPromptWidget::OnNextTargetClicked);
    PromptControls->AddChildToHorizontalBox(NextTarget)->SetVerticalAlignment(VAlign_Center);
    NextTargetButton = NextTarget;

    USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>();
    Width->SetWidthOverride(550.f);
    DialogueLayout = Width;
    CardStack->AddChildToVerticalBox(Width);
    UVerticalBox* DialogueStack = WidgetTree->ConstructWidget<UVerticalBox>();
    Width->SetContent(DialogueStack);
    DialogueShield = MakeArt(*WidgetTree, ShieldBrush, 70.f, 70.f);
    DialogueStack->AddChildToVerticalBox(DialogueShield)->SetHorizontalAlignment(HAlign_Center);

    DialogueHeading = MakeText(*WidgetTree, 28, Gold, HeadingFont);
    DialogueHeading->SetJustification(ETextJustify::Center);
    UVerticalBoxSlot* HeadingSlot = DialogueStack->AddChildToVerticalBox(DialogueHeading);
    HeadingSlot->SetPadding(FMargin(12.f, 3.f, 12.f, 5.f));
    DialogueHeading->SetAutoWrapText(true);

    USizeBox* Rule = MakeArt(*WidgetTree, MakeBrush(ReditusTutorialSkin::Rule()), 230.f, 14.f);
    DialogueStack->AddChildToVerticalBox(Rule)->SetHorizontalAlignment(HAlign_Center);
    StaminaExample = WidgetTree->ConstructWidget<UVerticalBox>();
    UVerticalBoxSlot* StaminaPlacement = DialogueStack->AddChildToVerticalBox(StaminaExample);
    StaminaPlacement->SetHorizontalAlignment(HAlign_Center);
    StaminaPlacement->SetPadding(FMargin(0.f, 7.f, 0.f, 0.f));
    RefreshStaminaExample();
    DialogueBody = MakeText(*WidgetTree, 22, WarmWhite, nullptr, TEXT("Regular"));
    DialogueBody->SetJustification(ETextJustify::Center);
    DialogueBody->SetAutoWrapText(true);
    DialogueStack->AddChildToVerticalBox(DialogueBody)->SetPadding(FMargin(32.f, 12.f, 32.f, 15.f));

    UButton* Continue = WidgetTree->ConstructWidget<UButton>();
    FButtonStyle Style;
    Style.SetNormal(FSlateColorBrush(FLinearColor(.067f, .12f, .18f, .50f)));
    Style.SetHovered(FSlateColorBrush(FLinearColor(.24f, .16f, .055f, .72f)));
    Style.SetPressed(FSlateColorBrush(FLinearColor(.38f, .25f, .07f, .82f)));
    Style.NormalPadding = Style.PressedPadding = FMargin(12.f, 4.f);
    Continue->SetStyle(Style);
    Continue->IsFocusable = false;
    Continue->OnClicked.AddDynamic(this, &UReditusMovementPromptWidget::OnContinueClicked);
    DialogueStack->AddChildToVerticalBox(Continue)->SetHorizontalAlignment(HAlign_Center);
    UHorizontalBox* ContinueRow = WidgetTree->ConstructWidget<UHorizontalBox>();
    Continue->SetContent(ContinueRow);
    DialogueAction = MakeText(*WidgetTree, 19, Gold, HeadingFont);
    ContinueRow->AddChildToHorizontalBox(DialogueAction)->SetVerticalAlignment(VAlign_Center);
    AddControl(*WidgetTree, *ContinueRow, KeyBrush, 34.f, ContinueGlyphBox, ContinueGlyph, ContinueKeyCap, ContinueKeyText);

    RefreshCard();
    return Super::RebuildWidget();
}
