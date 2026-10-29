#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Styling/SlateTypes.h"
#include "Fonts/SlateFontInfo.h"
#include "ReditusBattleHUDTypes.h"
#include "ReditusBattleHUDWidget.generated.h"

class UBorder;
class UCanvasPanel;
class UFont;
class UImage;
class UHorizontalBox;
class UOverlay;
class UProgressBar;
class USizeBox;
class UTextBlock;
class UScrollBox;
class UVerticalBox;
class UTexture2D;
class USoundBase;
class IInputProcessor;
class UReditusBattleHUDWidget;

/** Internal contextual button; it does not own gameplay state. */
UCLASS()
class CHAPTER1REDITUS_API UReditusBattleHUDButton : public UButton
{
    GENERATED_BODY()
public:
    UReditusBattleHUDButton(const FObjectInitializer& Initializer);
    void Configure(UReditusBattleHUDWidget* OwnerHUD, EReditusHUDCommand InCommand, int32 InIndex = INDEX_NONE);
private:
    UPROPERTY(Transient) UReditusBattleHUDWidget* HUD = nullptr;
    EReditusHUDCommand Command = EReditusHUDCommand::Attack;
    int32 Index = INDEX_NONE;
    UFUNCTION() void Clicked();
    UFUNCTION() void Hovered();
};

/**
 * Native UE4.27 battle HUD. Builds its UMG tree in C++, with no Designer asset.
 * Party snapshots and gameplay routing remain owned by the battle system.
 */
UCLASS(BlueprintType, Blueprintable)
class CHAPTER1REDITUS_API UReditusBattleHUDWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UReditusBattleHUDWidget(const FObjectInitializer& Initializer);

    UPROPERTY(BlueprintAssignable, Category="Battle HUD") FReditusHUDCommandEvent OnCommandRequested;
    UPROPERTY(BlueprintAssignable, Category="Battle HUD") FReditusHUDSelectionEvent OnSelectedUnitChanged;
    UPROPERTY(BlueprintAssignable, Category="Battle HUD") FReditusHUDMenuEvent OnMenuEntryChosen;
    UPROPERTY(BlueprintAssignable, Category="Battle HUD") FReditusHUDSelectionEvent OnMenuCancelled;
    UPROPERTY(BlueprintAssignable, Category="Battle HUD") FReditusHUDGameplayInputEvent OnGameplayInputRequested;
    UPROPERTY(BlueprintAssignable, Category="Battle HUD") FReditusHUDDeviceEvent OnInputDeviceChanged;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Style") UFont* PixelFont = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Style") FLinearColor Gold = FLinearColor(0.82f,0.61f,0.20f,1.f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Style") FLinearColor Panel = FLinearColor(0.003f,0.006f,0.014f,0.98f);
    /** Reads the existing AmpUIcon widget's first image brush. An explicit brush overrides it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Style") TSubclassOf<UUserWidget> AmpIconWidgetClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Style") FSlateBrush AmpImageBrush;
    /** Reuses each legacy row's SelectedImage brush unless explicitly assigned. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Style") FSlateBrush ListSelectionBrush;
    /** Match the live original item/skill text font and size. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Menus") bool bMatchLegacyListFonts = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Menus", meta=(ClampMin="12",ClampMax="36")) int32 ListTextSize = 24;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Menus", meta=(ClampMin="12",ClampMax="30")) int32 ListDescriptionSize = 20;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Audio") USoundBase* NavigationSound = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Audio") bool bPlayNavigationSound = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Audio", meta=(ClampMin="0.0",ClampMax="1.0")) float NavigationSoundVolume = 0.6f;
    /** Reuse existing input icon textures. Keys are the InputCore key names. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Style") TMap<FName,UTexture2D*> GlyphTextures;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Input") FReditusBattleHUDInputActions InputActions;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Input") FReditusBattleHUDGamepadKeys GamepadKeys;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Input") FKey KeyboardMoreKey = EKeys::LeftShift;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Input") bool bCaptureBattleInput = true;
    /** A pause or unrelated modal can suspend the battle input owner explicitly. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Input") bool bInputSuspended = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Input") bool bLogInput = false;
    UPROPERTY(BlueprintReadOnly, Transient, Category="Battle HUD|Input") FString LastInputStatus;
    /** Optional existing action route; None uses the built-in ready selection. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Input") FName ReadyUnitSwitchAction = NAME_None;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Input") FText ReadyUnitSwitchText;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Style") FText ReadyUnitText;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Input") FKey KeyboardBackKey;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Input") FKey GamepadBackKey;
    /** Viewport-edge spacing, in UMG layout units. Applied when the widget is built. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Battle HUD|Layout", meta=(ClampMin="0.0")) float BottomHUDSideMargin = 6.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Battle HUD|Layout", meta=(ClampMin="0.0")) float BottomHUDInset = 4.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Battle HUD|Layout", meta=(ClampMin="0.55", ClampMax="0.70")) float PartyWidthFraction = 0.63f;
    /** Primary numbered shortcuts also select the held modifier's command layer. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Input") TMap<EReditusHUDCommand,FKey> KeyboardShortcuts;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Input") bool bAutomaticGlyphSwitching = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Input") bool bAutoFocusCommandPanel = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Commands") bool bAllowRun = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Commands") bool bAllowPartyChanges = true;
    /** Disabled Overdrive stays visible. Hack appears for capable characters. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Commands") bool bShowUnavailableOverdrive = true;

    UFUNCTION(BlueprintCallable, Category="Battle HUD") void SetPartySnapshot(const TArray<FReditusBattleHUDUnit>& Units);
    UFUNCTION(BlueprintPure, Category="Battle HUD") TArray<FReditusBattleHUDUnit> GetPartySnapshot() const { return Party; }
    UFUNCTION(BlueprintCallable, Category="Battle HUD") bool SelectReadyUnit(AActor* Unit);
    UFUNCTION(BlueprintPure, Category="Battle HUD") AActor* GetSelectedUnit() const;
    UFUNCTION(BlueprintCallable, Category="Battle HUD") void SetCommandsEnabled(bool bEnabled);
    /** Routes a shared Back/Block key to the existing Block reaction action. */
    UFUNCTION(BlueprintCallable, Category="Battle HUD|Input") void SetReactionWindow(bool bEnabled);
    /** Confirm the existing target through MenuConfirm, without starting Attack again. */
    UFUNCTION(BlueprintCallable, Category="Battle HUD|Input") void SetTargetSelectionActive(bool bEnabled);
    UFUNCTION(BlueprintPure, Category="Battle HUD|Input") bool IsTargetSelectionActive() const { return bTargetSelectionActive; }
    UFUNCTION(BlueprintCallable, Category="Battle HUD") void SetTargetedEnemy(AActor* Enemy);
    UFUNCTION(BlueprintCallable, Category="Battle HUD") void SetTargetLabel(FText Label);
    UFUNCTION(BlueprintCallable, Category="Battle HUD") void ClearTargetLabel();
    UFUNCTION(BlueprintCallable, Category="Battle HUD") void SetDisplayedBattleSpeed(float Multiplier);
    UFUNCTION(BlueprintCallable, Category="Battle HUD") void SetInputDevice(EReditusHUDInputDevice Device);
    UFUNCTION(BlueprintPure, Category="Battle HUD") EReditusHUDInputDevice GetInputDevice() const { return InputDevice; }
    UFUNCTION(BlueprintCallable, Category="Battle HUD") bool CanExecuteCommand(EReditusHUDCommand Command) const;
    UFUNCTION(BlueprintCallable, Category="Battle HUD") bool RequestCommand(EReditusHUDCommand Command);
    /** Render skills/items/party candidates using existing names and row IDs. */
    UFUNCTION(BlueprintCallable, Category="Battle HUD|Menus") void ShowListMenu(FText Title, const TArray<FReditusBattleHUDMenuEntry>& Entries);
    /** Opens the same list area immediately while its gameplay source prepares. */
    UFUNCTION(BlueprintCallable, Category="Battle HUD|Menus") void ShowListStatus(FText Title, FText Message);
    UFUNCTION(BlueprintCallable, Category="Battle HUD|Menus") void UpdateListEntries(const TArray<FReditusBattleHUDMenuEntry>& Entries);
    UFUNCTION(BlueprintCallable, Category="Battle HUD|Menus") void CloseListMenu();
    UFUNCTION(BlueprintCallable, Category="Battle HUD|Menus") void CancelListMenu();
    /** Optional host for an already-created, compact widget. */
    UFUNCTION(BlueprintCallable, Category="Battle HUD|Menus") void SetHostedMenu(UUserWidget* Menu);
    UFUNCTION(BlueprintPure, Category="Battle HUD|Menus") bool IsMenuOpen() const { return bListOpen || HostedMenu != nullptr; }
    UFUNCTION(BlueprintCallable, Category="Battle HUD|Input") void FocusCommands();
    UFUNCTION(BlueprintCallable, Category="Battle HUD|Input") void SetAlternateCommands(bool bEnabled);
    UFUNCTION(BlueprintPure, Category="Battle HUD|Input") bool IsAlternateCommands() const { return bAlternateCommands; }
    UFUNCTION(BlueprintPure, Category="Battle HUD|Menus") bool IsNativeListOpen() const { return bListOpen; }
    /** Runtime adapter only: preserves the original font settings without touching its widgets. */
    void CopyLegacyListFonts(UTextBlock* NameText, UTextBlock* DetailText, UTextBlock* DescriptionText);

    FReditusHUDCommandRouter CommandRouter;
    FReditusHUDGameplayInputRouter GameplayInputRouter;
    void HandleButton(EReditusHUDCommand Command, int32 Index);
    void HandleHover(EReditusHUDCommand Command, int32 Index);
    void ObserveKey(const FKey& Key);
    void ObserveMouseMovement(float DistanceSquared);
    bool HasGameInputFocus() const;
    bool HandleBattleKeyDown(FKey Key, bool bRepeat);
    bool HandleBattleKeyUp(FKey Key);
    bool HandleBattleAnalog(FKey Key, float Previous, float Current);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
    virtual FReply NativeOnKeyUp(const FGeometry& Geometry, const FKeyEvent& Event) override;
    virtual FReply NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;

private:
    struct FCard
    {
        UBorder* Frame = nullptr;
        UCanvasPanel* Content = nullptr;
        UReditusBattleHUDButton* Select = nullptr;
        UImage* SelectionGlow = nullptr;
        UImage* Portrait = nullptr;
        UTextBlock* Name = nullptr;
        UTextBlock* ReadyLabel = nullptr;
        UTextBlock* HP = nullptr;
        UTextBlock* MP = nullptr;
        UTextBlock* ATB = nullptr;
        UTextBlock* OD = nullptr;
        UTextBlock* Amp = nullptr;
        UHorizontalBox* AmpIcons = nullptr;
        TArray<UImage*> AmpImages;
        UProgressBar* HPBar = nullptr;
        UProgressBar* MPBar = nullptr;
        UProgressBar* ATBBar = nullptr;
        UProgressBar* ODBar = nullptr;
    };
    struct FButtonView
    {
        EReditusHUDCommand Command = EReditusHUDCommand::Attack;
        UReditusBattleHUDButton* Button = nullptr;
        UImage* Glyph = nullptr;
        USizeBox* GlyphBox = nullptr;
        UTextBlock* Key = nullptr;
        UTextBlock* Label = nullptr;
        UTextBlock* Detail = nullptr;
        UImage* Icon = nullptr;
        int32 Index = INDEX_NONE;
    };

    UPROPERTY(Transient) TArray<FReditusBattleHUDUnit> Party;
    UPROPERTY(Transient) TArray<FReditusBattleHUDMenuEntry> MenuEntries;
    UPROPERTY(Transient) UUserWidget* HostedMenu = nullptr;
    UPROPERTY(Transient) AActor* MenuOwner = nullptr;
    UPROPERTY(Transient) UCanvasPanel* DesignCanvas = nullptr;
    UPROPERTY(Transient) UCanvasPanel* PartyCanvas = nullptr;
    UPROPERTY(Transient) UOverlay* MenuHost = nullptr;
    UPROPERTY(Transient) UCanvasPanel* CommandCanvas = nullptr;
    UPROPERTY(Transient) UCanvasPanel* CommandRegion = nullptr;
    UPROPERTY(Transient) UTextBlock* TargetName = nullptr;
    UPROPERTY(Transient) UTextBlock* MenuHeader = nullptr;
    UPROPERTY(Transient) UTextBlock* MenuDescription = nullptr;
    UPROPERTY(Transient) UTextBlock* SpeedText = nullptr;
    UPROPERTY(Transient) UUserWidget* AmpIconTemplate = nullptr;
    UPROPERTY(Transient) TArray<UTexture2D*> SkinTextures;
    UPROPERTY(Transient) FSlateBrush GoldFrameBrush;
    UPROPERTY(Transient) FSlateBrush SelectedFrameBrush;
    UPROPERTY(Transient) FSlateBrush SilverFrameBrush;
    UPROPERTY(Transient) FSlateBrush SelectionHaloBrush;
    UPROPERTY(Transient) FSlateBrush GaugeBackgroundBrush;
    UPROPERTY(Transient) FSlateBrush GaugeFillBrush;
    UPROPERTY(Transient) FSlateBrush ResolvedAmpBrush;
    UPROPERTY(Transient) UCanvasPanel* ListCanvas = nullptr;
    UPROPERTY(Transient) UScrollBox* EntryScroll = nullptr;
    UPROPERTY(Transient) UVerticalBox* EntryRows = nullptr;
    UPROPERTY(Transient) FSlateFontInfo LegacyListNameFont;
    UPROPERTY(Transient) FSlateFontInfo LegacyListDetailFont;
    UPROPERTY(Transient) FSlateFontInfo LegacyListDescriptionFont;

    TArray<FCard> Cards; // Child widgets are retained by WidgetTree's root.
    TArray<FButtonView> Buttons;
    TArray<FButtonView> ListButtons;
    TArray<FButtonView> UtilityButtons;
    TSharedPtr<IInputProcessor> InputObserver;
    int32 SelectedIndex = INDEX_NONE;
    int32 SelectedCommand = 0;
    int32 SelectedMenuIndex = INDEX_NONE;
    EReditusHUDInputDevice InputDevice = EReditusHUDInputDevice::KeyboardMouse;
    bool bCommandsEnabled = false;
    bool bReactionWindow = false;
    bool bTargetSelectionActive = false;
    bool bListOpen = false;
    bool bAlternateCommands = false;
    bool bHasLegacyListFonts = false;
    TSet<FKey> PressedKeys;
    FText TargetLabel;
    FText MenuTitle;
    FText MenuStatus;
    float DisplayedSpeed = 1.f;

    void BuildLayout();
    void InitializeSkin();
    void RefreshAmpImages(UHorizontalBox* IconHost, TArray<UImage*>& Images, int32 Total, int32 Committed);
    void BuildCards();
    void Refresh();
    void RefreshGlyphs();
    void RefreshMenu();
    void BuildListRows();
    void RevealMenuSelection();
    void MoveCommandSelection(int32 Horizontal, int32 Vertical);
    void MoveMenuSelection(int32 Horizontal, int32 Vertical = 0);
    void ChangeMenuPage(int32 Direction);
    bool ChooseSelectedMenuEntry();
    bool CycleReadyAlly();
    void PlayNavigationCue();
    bool RouteGameplayInput(FName Action, FKey Key);
    bool MatchesAction(FName Action, const FKey& Key) const;
    FKey KeyForAction(FName Action) const;
    FKey KeyForCommand(EReditusHUDCommand Command) const;
    const FReditusBattleHUDUnit* Selected() const;
    bool CanChooseMenuEntry() const;
    FSlateFontInfo Font(int32 Size) const;
    UTextBlock* MakeText(FText Text, int32 Size = 20);
    UBorder* MakeFrame(UWidget* Content, FMargin FramePadding = FMargin(8.f));
    FButtonView MakeButton(EReditusHUDCommand Command, FText Label, int32 Index = INDEX_NONE);
    void Place(UCanvasPanel* Canvas, UWidget* Widget, float X, float Y, float W, float H);
    void PlaceStretchX(UCanvasPanel* Canvas, UWidget* Widget, float Left, float Y, float Right, float H, float MinAnchor = 0.f, float MaxAnchor = 1.f);
    static FText CommandLabel(EReditusHUDCommand Command);
};
