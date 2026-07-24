#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HermesDialogueWidget.generated.h"

class UHermesConnectionSubsystem;
class UEditableTextBox;
class UTextBlock;
class UButton;

UCLASS()
class HERMESAGENTNPC_API UHermesDialogueWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void OpenFor(UHermesConnectionSubsystem* Conn);

	virtual void NativeDestruct() override;

protected:
	UPROPERTY(meta=(BindWidget))
	UEditableTextBox* InputBox = nullptr;

	UPROPERTY(meta=(BindWidget))
	UTextBlock* DialogueText = nullptr;

	UPROPERTY(meta=(BindWidget))
	UButton* SendButton = nullptr;

	UFUNCTION()
	void OnSendClicked();

private:
	void HandleChatResponse(const FString& Text, const FString& Id);
	void HandleConnState(bool bReady);

	UPROPERTY()
	UHermesConnectionSubsystem* Connection = nullptr;

	FDelegateHandle ChatHandle;
	FDelegateHandle StateHandle;
};
