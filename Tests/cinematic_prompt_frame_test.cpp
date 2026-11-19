#include <cassert>
#include <cmath>
#include <memory>
#include <vector>

using int32 = int;
template<class T> struct TArray : std::vector<T>
{
    using std::vector<T>::vector;
    int32 Num() const { return int32(this->size()); }
};
struct FVector2D
{
    float X, Y;
    FVector2D(float InX,float InY) : X(InX),Y(InY) {}
    FVector2D operator-(FVector2D Other) const { return {X-Other.X,Y-Other.Y}; }
    float Size() const { return std::sqrt(X*X+Y*Y); }
};
struct FMath
{
    static float Atan2(float Y,float X) { return std::atan2(Y,X); }
    static float RadiansToDegrees(float Radians) { return Radians*57.2957795f; }
};
struct FLinearColor
{
    float R,G,B,A;
    FLinearColor(float Red,float Green,float Blue,float Alpha=1) : R(Red),G(Green),B(Blue),A(Alpha) {}
    static const FLinearColor Transparent;
};
const FLinearColor FLinearColor::Transparent(0,0,0,0);
struct FSlateColorBrush
{
    FLinearColor Color;
    explicit FSlateColorBrush(FLinearColor Value) : Color(Value) {}
};
struct FMargin
{
    FMargin(float=0) {}
    FMargin(float,float,float,float) {}
};
struct FAnchors
{
    float MinX,MinY,MaxX,MaxY;
    FAnchors(float X,float Y) : MinX(X),MinY(Y),MaxX(X),MaxY(Y) {}
    FAnchors(float X,float Y,float Right,float Bottom) : MinX(X),MinY(Y),MaxX(Right),MaxY(Bottom) {}
};
enum class ESlateVisibility { Visible, HitTestInvisible };
struct UCanvasPanelSlot;
struct UWidget
{
    virtual ~UWidget() = default;
    UCanvasPanelSlot* Slot = nullptr;
    ESlateVisibility Visibility = ESlateVisibility::Visible;
    float Rotation = 0;
    void SetVisibility(ESlateVisibility Value) { Visibility=Value; }
    void SetRenderTransformPivot(FVector2D) {}
    void SetRenderTransformAngle(float Value) { Rotation=Value; }
};
struct UCanvasPanelSlot
{
    FAnchors Anchors{0,0};
    FVector2D Size{0,0},Position{0,0};
    int32 Z = 0;
    void SetAnchors(FAnchors Value) { Anchors=Value; }
    void SetOffsets(FMargin) {}
    void SetZOrder(int32 Value) { Z=Value; }
    void SetPosition(FVector2D Value) { Position=Value; }
    void SetSize(FVector2D Value) { Size=Value; }
    void SetAlignment(FVector2D) {}
};
struct UBorder : UWidget
{
    FLinearColor Tint = FLinearColor::Transparent;
    UWidget* Content = nullptr;
    void SetBrush(FSlateColorBrush Value) { Tint=Value.Color; }
    void SetPadding(FMargin) {}
    void SetContent(UWidget* Value) { Content=Value; }
    UWidget* GetContent() const { return Content; }
};
struct UCanvasPanel : UWidget
{
    std::vector<UWidget*> Children;
    std::vector<std::unique_ptr<UCanvasPanelSlot>> Slots;
    UCanvasPanelSlot* AddChildToCanvas(UWidget* Value)
    {
        Children.push_back(Value);
        Slots.emplace_back(new UCanvasPanelSlot);
        Value->Slot=Slots.back().get();
        return Value->Slot;
    }
    int32 GetChildrenCount() const { return int32(Children.size()); }
    UWidget* GetChildAt(int32 Index) const { return Children.at(Index); }
};
struct UWidgetTree
{
    std::vector<std::unique_ptr<UWidget>> Widgets;
    template<class T> T* ConstructWidget()
    {
        T* Value=new T;
        Widgets.emplace_back(Value);
        return Value;
    }
};
template<class T> T* Cast(UWidget* Value) { return dynamic_cast<T*>(Value); }
const FLinearColor Gold(.87f,.64f,.22f,1);
const FLinearColor Mist(.018f,.05f,.075f,.22f);

// ACTUAL_SOLID
// ACTUAL_ANCHORED
// ACTUAL_GLASS_FRAME
// ACTUAL_ORNATE_PROMPT

int main()
{
    UWidgetTree Tree;
    UWidget Caption;
    UBorder* Holder=PromptFrame(&Tree,&Caption);
    assert(Holder->Tint.A==0);
    UCanvasPanel* Canvas=Cast<UCanvasPanel>(Holder->GetContent());
    assert(Canvas && Canvas->GetChildrenCount()>20);
    UBorder* Backdrop=Cast<UBorder>(Canvas->GetChildAt(0));
    assert(Backdrop && Backdrop->Tint.A<=.1f);
    UBorder* Face=Cast<UBorder>(Canvas->GetChildAt(1));
    assert(Face && Face->Content==&Caption && Face->Tint.A==0);
    assert(Face->Visibility==ESlateVisibility::Visible);

    int RotatedStrokes=0;
    for (int32 Index=0;Index<Canvas->GetChildrenCount();++Index)
    {
        UWidget* Element=Canvas->GetChildAt(Index);
        if (Index!=1) assert(Element->Visibility==ESlateVisibility::HitTestInvisible);
        if (Element->Rotation!=0) ++RotatedStrokes;
    }
    assert(RotatedStrokes>8); // Actual flourishes and diamonds, with open mouse hit paths.
    return 0;
}
