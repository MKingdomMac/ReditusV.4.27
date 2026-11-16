#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ReditusDialoguePresentationPolicy.h"
#include "ReditusDialoguePresentationWidget.generated.h"

class UBorder;
class UButton;
class UCanvasPanel;
class UDialogueUserWidget;
class UHorizontalBox;
class UImage;
class UPanelWidget;
class UScrollBox;
class UTextBlock;
class UVerticalBox;
class IInputProcessor;
class FReditusDialogueInputProcessor;
class UReditusDialoguePresentationWidget;
struct FKey;

UCLASS()
class REDITUSQUESTNPC_API UReditusDialogueChoiceProxy : public UObject
{
    GENERATED_BODY()
public:
    TWeakObjectPtr<UReditusDialoguePresentationWidget> Owner;
    TWeakObjectPtr<UUserWidget> OriginalReply;
    int32 Index = INDEX_NONE;
    UPROPERTY(Transient) UButton* Button = nullptr;
    UPROPERTY(Transient) UTextBlock* Text = nullptr;
    UFUNCTION() void Clicked();
    UFUNCTION() void Hovered();
};

// Presentation only. The original DemoDialogueWidget stays in the viewport and
// retains its Blueprint events, input, quest/recruitment state and sequence flow.
UCLASS()
class REDITUSQUESTNPC_API UReditusDialoguePresentationWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    static bool Install(UDialogueUserWidget* DialogueWidget);

    bool CanRouteInput() const;
    bool RouteKeyDown(const FKey& Key, bool Repeat);
    bool RouteKeyUp(const FKey& Key);
    bool RouteAnalog(const FKey& Key, float Value);
    void InputInactive();
    void SelectReply(int32 Index, UUserWidget* ExpectedReply);
    void ChooseReply(int32 Index, UUserWidget* ExpectedReply);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;

private:
    friend class FReditusDialogueInputProcessor;
    UFUNCTION() void Advance();
    UFUNCTION() void ToggleAuto();
    UFUNCTION() void ToggleLog();
    UFUNCTION() void SkipPressed();
    UFUNCTION() void SkipReleased();

    void BuildLayout();
    void ApplyTextStyle();
    void RefreshChoices();
    void RefreshControls();
    void UpdateLog(double Delta);
    void UpdateHeld();
    void RestoreSourceFocus();
    bool CallReply(int32 Index);
    bool InputAllowed() const;
    ReditusDialoguePresentationPolicy::Snapshot ReadSnapshot() const;

    TWeakObjectPtr<UDialogueUserWidget> Source;
    UPROPERTY(Transient) UWidget* OriginalRoot = nullptr;
    UPROPERTY(Transient) UWidget* Body = nullptr;
    UPROPERTY(Transient) UTextBlock* Speaker = nullptr;
    UPROPERTY(Transient) UImage* Portrait = nullptr;
    UPROPERTY(Transient) UPanelWidget* Replies = nullptr;
    UPROPERTY(Transient) UWidget* ContinueContainer = nullptr;

    UPROPERTY(Transient) UWidget* PortraitContainer = nullptr;
    UPROPERTY(Transient) UWidget* DialogueTextContainer = nullptr;
    UPROPERTY(Transient) UWidget* SequenceControls = nullptr;
    UPROPERTY(Transient) UBorder* SpeakerFrame = nullptr;
    UPROPERTY(Transient) UBorder* ChoiceFrame = nullptr;
    UPROPERTY(Transient) UVerticalBox* ChoiceRows = nullptr;
    UPROPERTY(Transient) UWidget* LogLayer = nullptr;
    UPROPERTY(Transient) UScrollBox* LogScroll = nullptr;
    UPROPERTY(Transient) UVerticalBox* LogRows = nullptr;
    UPROPERTY(Transient) UButton* AdvanceButton = nullptr;
    UPROPERTY(Transient) UButton* AutoButton = nullptr;
    UPROPERTY(Transient) UButton* LogButton = nullptr;
    UPROPERTY(Transient) UButton* SkipButton = nullptr;
    UPROPERTY(Transient) UTextBlock* AutoLabel = nullptr;
    UPROPERTY(Transient) UTextBlock* AdvanceLabel = nullptr;
    UPROPERTY(Transient) UTextBlock* LogLabel = nullptr;
    UPROPERTY(Transient) UTextBlock* SkipLabel = nullptr;
    UPROPERTY(Transient) UTextBlock* StatusLabel = nullptr;
    UPROPERTY(Transient) TArray<UReditusDialogueChoiceProxy*> ChoiceProxies;

    TSharedPtr<IInputProcessor> InputProcessor;
    ReditusDialoguePresentationPolicy::Controls Playback;
    bool KeyHeld = false;
    bool TriggerHeld = false;
    bool MouseHeld = false;
    bool NeedTriggerRelease = false;
    bool RegisteredInput = false;
    bool StyledText = false;
    bool bPresentationReady = false;
    bool UsingGamepad = false;
    FVector2D OriginalPortraitBounds = FVector2D(256.f, 256.f);
    double LastTime = 0;
    double CandidateAge = 0;
    uint64 CandidateLine = 0;
    uint64 LoggedLine = 0;
    FString CandidateSpeaker;
    FString CandidateText;
    FString LoggedText;
    UPROPERTY(Transient) UTextBlock* LastLogText = nullptr;
};

UCLASS()
class REDITUSQUESTNPC_API UReditusDialoguePresentationLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    // Optional manual entry point; call after Create Widget and before Add To Viewport.
    UFUNCTION(BlueprintCallable, Category="Reditus|Dialogue")
    static bool ApplyReditusDialoguePresentation(UDialogueUserWidget* DialogueWidget);
};
