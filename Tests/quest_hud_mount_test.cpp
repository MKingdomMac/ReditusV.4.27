#include <cassert>
#include <memory>
#include <vector>

template<class T> struct TArray : std::vector<T>
{
    int Num() const { return int(this->size()); }
    void Reset() { this->clear(); }
};
template<class T> struct TWeakObjectPtr
{
    T* Object = nullptr;
    T* Get() const { return Object; }
    void Reset() { Object = nullptr; }
    void operator=(T* Value) { Object = Value; }
};
template<class T> using TSharedRef = std::shared_ptr<T>;
template<class T> struct TWeakPtr
{
    struct Pinned
    {
        TSharedRef<T> Value;
        explicit operator bool() const { return bool(Value); }
        TSharedRef<T> ToSharedRef() const { return Value; }
    };
    std::weak_ptr<T> Weak;
    Pinned Pin() const { return {Weak.lock()}; }
    bool IsValid() const { return !Weak.expired(); }
    void Reset() { Weak.reset(); }
    void operator=(const TSharedRef<T>& Value) { Weak = Value; }
};

enum class EVisibility { Visible, HitTestInvisible };
struct SWidget { EVisibility Visibility = EVisibility::Visible; TSharedRef<SWidget> Content; };
struct SBox : SWidget {};
struct SlateBoxBuilder
{
    TSharedRef<SBox> Box = std::make_shared<SBox>();
    SlateBoxBuilder& Visibility(EVisibility Mode) { Box->Visibility = Mode; return *this; }
    TSharedRef<SWidget> operator[](TSharedRef<SWidget> Content) { Box->Content = Content; return Box; }
};
#define SNew(WidgetType) SlateBoxBuilder()
using int32 = int;
struct UGameViewportClient
{
    const void* Viewport = nullptr;
    TSharedRef<SWidget> Attached;
    int Mounts = 0;
    int Removes = 0;
    void AddViewportWidgetContent(TSharedRef<SWidget> Widget, int)
    {
        assert(!Attached && Widget->Visibility == EVisibility::HitTestInvisible);
        Attached = Widget;
        ++Mounts;
    }
    void RemoveViewportWidgetContent(TSharedRef<SWidget> Widget)
    {
        assert(Attached == Widget);
        Attached.reset();
        ++Removes;
    }
};
struct APlayerController;
struct UWorld { bool bIsTearingDown = false; APlayerController* Player = nullptr; };
struct APlayerController
{
    UWorld* World = nullptr;
    bool IsLocalController() const { return true; }
};
bool IsValid(APlayerController* Player) { return Player != nullptr; }
namespace UGameplayStatics
{ APlayerController* GetPlayerController(UWorld* World, int) { return World->Player; } }
struct UGameInstance
{
    UGameViewportClient* Viewport = nullptr;
    UGameViewportClient* GetGameViewportClient() const { return Viewport; }
};
struct UReditusQuestNotificationsWidget
{
    APlayerController* Player;
    std::vector<int> Notices;
    UWorld* GetWorld() const { return Player->World; }
    APlayerController* GetOwningPlayer() const { return Player; }
    TSharedRef<SWidget> TakeWidget() { return std::make_shared<SWidget>(); }
    void Enqueue(int Notice) { Notices.push_back(Notice); }
};
std::vector<std::unique_ptr<UReditusQuestNotificationsWidget>> Widgets;
template<class T> T* CreateWidget(APlayerController* Player)
{
    auto* Widget = new T{Player, {}};
    Widgets.emplace_back(Widget);
    return Widget;
}

struct UReditusQuestSubsystem
{
    bool ShowQuestNotifications = true;
    UGameInstance Game;
    TArray<int> Timers;
    TArray<int> PendingHUDNotices;
    TWeakObjectPtr<UReditusQuestNotificationsWidget> NotificationView;
    TWeakObjectPtr<UGameViewportClient> NotificationViewport;
    TWeakPtr<SWidget> NotificationLayer;
    UGameInstance* GetGameInstance() { return &Game; }
    const TArray<int>& GetQuestTimers() const { return Timers; }
    void UpdateQuestHUD(UWorld* World);
    void RemoveQuestHUD();
};
// ACTUAL_HUD_LIFECYCLE

int main()
{
    UReditusQuestSubsystem System;
    UGameViewportClient Viewport;
    UWorld World;
    APlayerController Odin{&World};
    System.Game.Viewport = &Viewport;
    System.PendingHUDNotices.push_back(1);
    System.UpdateQuestHUD(&World);
    assert(!Viewport.Attached && System.PendingHUDNotices.Num() == 1);
    World.Player = &Odin;
    Viewport.Viewport = &Viewport;
    System.UpdateQuestHUD(&World);
    assert(Viewport.Mounts == 1 && Viewport.Attached->Content);
    assert(System.NotificationView.Get()->Notices == std::vector<int>{1});
    assert(System.PendingHUDNotices.Num() == 0);
    System.UpdateQuestHUD(&World);
    assert(Viewport.Mounts == 1);

    UWorld NextWorld;
    APlayerController NextPlayer{&NextWorld};
    NextWorld.Player = &NextPlayer;
    System.ShowQuestNotifications = false;
    System.Timers.push_back(1);
    System.PendingHUDNotices.push_back(2);
    System.UpdateQuestHUD(&NextWorld);
    assert(Viewport.Mounts == 2 && Viewport.Removes == 1);
    assert(System.NotificationView.Get()->GetOwningPlayer() == &NextPlayer);
    assert(System.PendingHUDNotices.Num() == 0 && System.NotificationView.Get()->Notices.empty());
    Viewport.Attached.reset(); // The engine can clear viewport overlays itself.
    System.UpdateQuestHUD(&NextWorld);
    assert(Viewport.Mounts == 3 && Viewport.Removes == 1);
    System.RemoveQuestHUD();
    assert(!Viewport.Attached && !System.NotificationView.Get() && Viewport.Removes == 2);
}
