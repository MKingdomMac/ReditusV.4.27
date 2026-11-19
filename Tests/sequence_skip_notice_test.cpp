// Compile the real notice builder/update methods against the UE4 widget API
// used here. This checks state and layout contracts, not live Slate rendering.
#include <cassert>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#define TEXT(Value) Value
#define NSLOCTEXT(Namespace, Key, Value) FText{Value}
struct SWidget {};
template<class T> using TSharedRef = T*;
struct FLinearColor
{
    float Alpha;
    FLinearColor(float, float, float, float A) : Alpha(A) {}
    static const FLinearColor White;
};
const FLinearColor FLinearColor::White(1, 1, 1, 1);
struct FSlateColor { explicit FSlateColor(FLinearColor) {} };
struct FSlateColorBrush { explicit FSlateColorBrush(FLinearColor) {} };
struct FMargin { explicit FMargin(float) {} FMargin(float, float) {} FMargin(float, float, float, float) {} };
struct FAnchors { FAnchors(float, float) {} };
struct FVector2D { FVector2D(float, float) {} };
enum class ESlateVisibility { Visible, Collapsed, HitTestInvisible };
enum class ETextJustify { Center };
struct FText
{
    std::string Value;
    static FText AsNumber(int Value) { return {std::to_string(Value)}; }
    static FText Format(FText, FText Percent) { return {"Skipping... " + Percent.Value + "%"}; }
};
struct FMath
{
    static float Clamp(float V, float A, float B) { return std::max(A, std::min(V, B)); }
    static float Max(float A, float B) { return std::max(A, B); }
    static int RoundToInt(float V) { return int(std::round(V)); }
};
struct UPanelSlot {};
struct UWidget
{
    virtual ~UWidget() = default;
    UPanelSlot* Slot = nullptr;
    ESlateVisibility Visibility = ESlateVisibility::Visible;
    void SetVisibility(ESlateVisibility Value) { Visibility = Value; }
};
struct UCanvasPanelSlot
{
    void SetAnchors(FAnchors) {}
    void SetAlignment(FVector2D) {}
    void SetAutoSize(bool) {}
};
struct UVerticalBoxSlot { void SetPadding(FMargin) {} };
struct UTextBlock : UWidget
{
    FText Text;
    void SetText(FText Value) { Text = Value; }
    void SetFont(int) {}
    void SetColorAndOpacity(FSlateColor) {}
    void SetJustification(ETextJustify) {}
};
struct UBorder : UWidget
{
    void SetBrush(FSlateColorBrush) {}
    void SetPadding(FMargin) {}
    void SetContent(UWidget*) {}
};
struct UCanvasPanel : UWidget
{
    UCanvasPanelSlot ChildSlot;
    UCanvasPanelSlot* AddChildToCanvas(UBorder*) { return &ChildSlot; }
};
struct UVerticalBox : UWidget
{
    UVerticalBoxSlot ChildSlot;
    UVerticalBoxSlot* AddChildToVerticalBox(UWidget*) { return &ChildSlot; }
};
struct USizeBox : UWidget
{
    void SetWidthOverride(float) {}
    void SetHeightOverride(float) {}
    void SetContent(UWidget*) {}
};
struct FProgressBarStyle
{
    void SetBackgroundImage(FSlateColorBrush) {}
    void SetFillImage(FSlateColorBrush) {}
};
struct UProgressBar : UWidget
{
    FProgressBarStyle WidgetStyle; // UE4.27's public style property.
    float Percent = 0;
    void SetPercent(float Value) { Percent = Value; }
    void SetFillColorAndOpacity(FLinearColor) {}
};
struct UWidgetTree
{
    UWidget* RootWidget = nullptr;
    std::vector<std::unique_ptr<UWidget>> Children;
    template<class T> T* ConstructWidget()
    {
        auto* Child = new T;
        Children.emplace_back(Child);
        return Child;
    }
};
struct UUserWidget : UWidget
{
    UWidgetTree* WidgetTree = nullptr;
    TSharedRef<SWidget> RebuildWidget() { return nullptr; }
};
namespace FCoreStyle { int GetDefaultFontStyle(const char*, int) { return 0; } }
template<class T, class Outer> T* NewObject(Outer*) { return new T; }
struct UReditusSequenceSkipNotice : UUserWidget
{
    using Super = UUserWidget;
    FText Notice;
    UTextBlock* Caption = nullptr;
    UProgressBar* ProgressBar = nullptr;
    float HoldProgress = -1;
    void SetMessage(const FText&);
    void SetProgress(float);
    TSharedRef<SWidget> RebuildWidget();
};
// ACTUAL_NOTICE_WIDGET
int main()
{
    UWidgetTree Tree;
    UReditusSequenceSkipNotice Notice;
    Notice.WidgetTree = &Tree;
    Notice.SetMessage({"This scene can't be skipped."});
    Notice.RebuildWidget();
    assert(Notice.Caption->Text.Value == "This scene can't be skipped.");
    assert(Notice.ProgressBar->Visibility == ESlateVisibility::Collapsed);
    Notice.SetProgress(.4f);
    assert(Notice.Caption->Text.Value == "Skipping... 40%");
    assert(Notice.ProgressBar->Percent == .4f && Notice.ProgressBar->Visibility == ESlateVisibility::HitTestInvisible);
    assert(Notice.Visibility == ESlateVisibility::HitTestInvisible);
    Notice.SetProgress(2.f);
    assert(Notice.ProgressBar->Percent == 1.f);
    Notice.SetMessage({"This scene can't be skipped."});
    assert(Notice.ProgressBar->Visibility == ESlateVisibility::Collapsed);
    assert(Notice.Caption->Text.Value == "This scene can't be skipped.");
}
