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
	/**
	 * 대화창을 띄우고 입력을 이 위젯으로 넘긴다. 이미 열려 있으면 아무것도 하지
	 * 않는다 — 다시 열면 델리게이트가 중복으로 붙어 한 번 눌러도 두 번 전송된다.
	 */
	void OpenFor(UHermesConnectionSubsystem* Conn);

	/** 대화창을 닫고 입력을 게임으로 되돌린다. */
	void Close();

	bool IsOpen() const { return bIsOpen; }

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

	/** 입력창에서 Enter 로 전송, Esc 로 닫기. 마우스 없이도 조작할 수 있게 한다. */
	UFUNCTION()
	void OnInputCommitted(const FText& Text, ETextCommit::Type CommitMethod);

private:
	/** 현재 발화를 전송하고 입력창을 비운다. 버튼과 Enter 가 공유한다. */
	void SendCurrentText();

	/** 입력을 대화창으로 넘기거나(true) 게임으로 되돌린다(false). */
	void ApplyInputMode(bool bToWidget);

	void BindEvents();
	void UnbindEvents();

	void HandleChatDelta(const FString& Text, const FString& Id);
	void HandleChatResponse(const FString& Text, const FString& Id);
	void HandleChatFailed(const FString& Id, const FString& Reason);
	void HandleConnState(bool bReady);

	UPROPERTY()
	UHermesConnectionSubsystem* Connection = nullptr;

	FDelegateHandle ChatHandle;
	FDelegateHandle DeltaHandle;
	FDelegateHandle FailedHandle;
	FDelegateHandle StateHandle;

	/** 현재 표시 중인 응답의 누적 텍스트. chat_response 가 오면 정본으로 교체된다. */
	FString StreamingText;

	/** 이번 틱에 표시를 갱신해야 하는지. 틱당 SetText 1회로 묶기 위한 플래그. */
	bool bStreamingDirty = false;

	bool bIsOpen = false;
};
