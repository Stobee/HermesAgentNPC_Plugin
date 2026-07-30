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
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

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
	void HandleChatDelta(const FString& Text, const FString& Id);
	void HandleChatResponse(const FString& Text, const FString& Id);
	void HandleConnState(bool bReady);

	UPROPERTY()
	UHermesConnectionSubsystem* Connection = nullptr;

	FDelegateHandle ChatHandle;
	FDelegateHandle DeltaHandle;
	FDelegateHandle StateHandle;

	/** 현재 표시 중인 응답의 누적 텍스트. chat_response 가 오면 정본으로 교체된다. */
	FString StreamingText;

	/** 누적 중인 턴의 chat.id. 델타의 id 가 이와 달라지면 버퍼를 버린다. */
	FString StreamingId;

	/** 이번 틱에 표시를 갱신해야 하는지. 틱당 SetText 1회로 묶기 위한 플래그. */
	bool bStreamingDirty = false;
};
