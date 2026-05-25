// Copyright Sam Bonifacio. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "RadioButton.generated.h"

class URadioButton;

// Original single-param delegate (keeps existing Blueprint contract)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRadioSelectedSignature, FString, Value);

// Optional two-param delegate that includes the sender (helps radio groups)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRadioSelectedWithSenderSignature, URadioButton*, Sender, FString, Value);

/**
 * A single generic radio button
 */
UCLASS(abstract)
class AUTOSETTINGS_API URadioButton : public UUserWidget
{
	GENERATED_BODY()
	
public:
	URadioButton(const FObjectInitializer& ObjectInitializer);

	// Event triggered when this button is selected (value only)
	UPROPERTY(BlueprintAssignable, Category = "Radio Button")
	FRadioSelectedSignature OnSelected;

	// Optional event with sender pointer
	UPROPERTY(BlueprintAssignable, Category = "Radio Button")
	FRadioSelectedWithSenderSignature OnSelectedWithSender;
	
	// Set whether the button is selected or not
	UFUNCTION(BlueprintCallable, Category = "Radio Button")
	virtual void SetSelected(bool InSelected);

	// Return the value associated with the button (Blueprint)
	UFUNCTION(BlueprintPure, Category = "Radio Button")
	FString GetValue() const { return Value; }

	// Efficient C++ getter that avoids a copy
	const FString& GetValueRef() const { return Value; }

	// Set the value associated with the button
	UFUNCTION(BlueprintCallable, Category = "Radio Button")
	void SetValue(const FString& InValue) { Value = InValue; }

	// Set the label of the button
	UFUNCTION(BlueprintCallable, Category = "Radio Button")
	void SetLabel(FText InLabel) { Label = InLabel; }

protected:
	// Allow initial value/label/selected to be provided when spawning the widget
	UPROPERTY(BlueprintReadOnly, Category = "Radio Button", meta = (ExposeOnSpawn))
	FText Label;

	UPROPERTY(BlueprintReadOnly, Category = "Radio Button", meta = (ExposeOnSpawn))
	FString Value;

	// Expose initial selected state and allow subclasses/Blueprints to read it
	UPROPERTY(BlueprintReadOnly, Category = "Radio Button", meta = (ExposeOnSpawn))
	bool Selected = false;

	UFUNCTION(BlueprintImplementableEvent, Category = "Radio Button")
	void UpdateLabel(const FText& InLabel);

	UFUNCTION(BlueprintImplementableEvent, Category = "Radio Button")
	void UpdateSelected(bool InSelected);

	// Called by the UI (e.g. OnClicked) to trigger selection behavior
	UFUNCTION(BlueprintCallable, Category = "Radio Button")
	void TriggerSelection();

	UFUNCTION(BlueprintPure, Category = "Radio Button")
	FText GetLabel() const { return Label; }

	UFUNCTION(BlueprintPure, Category = "Radio Button")
	bool GetSelected() const { return Selected; }

	virtual void NativeConstruct() override;

private:
};
