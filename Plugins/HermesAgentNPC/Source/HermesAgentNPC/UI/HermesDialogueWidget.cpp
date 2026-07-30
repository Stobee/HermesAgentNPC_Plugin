#include "UI/HermesDialogueWidget.h"
#include "Connection/HermesConnectionSubsystem.h"
#include "HermesLog.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"

void UHermesDialogueWidget::OpenFor(UHermesConnectionSubsystem* Conn)
{
	Connection = Conn;
	if (SendButton)
	{
		SendButton->OnClicked.AddDynamic(this, &UHermesDialogueWidget::OnSendClicked);
	}
	if (Connection)
	{
		ChatHandle  = Connection->OnChatResponse.AddUObject(this, &UHermesDialogueWidget::HandleChatResponse);
		DeltaHandle  = Connection->OnChatDelta.AddUObject(this, &UHermesDialogueWidget::HandleChatDelta);
		FailedHandle = Connection->OnChatFailed.AddUObject(this, &UHermesDialogueWidget::HandleChatFailed);
		StateHandle  = Connection->OnConnectionStateChanged.AddUObject(this, &UHermesDialogueWidget::HandleConnState);
	}
	AddToViewport();
}

void UHermesDialogueWidget::NativeDestruct()
{
	if (Connection)
	{
		Connection->OnChatResponse.Remove(ChatHandle);
		Connection->OnChatDelta.Remove(DeltaHandle);
		Connection->OnChatFailed.Remove(FailedHandle);
		Connection->OnConnectionStateChanged.Remove(StateHandle);
	}
	Super::NativeDestruct();
}

void UHermesDialogueWidget::OnSendClicked()
{
	if (!Connection || !InputBox) return;
	const FString Text = InputBox->GetText().ToString();
	if (Text.IsEmpty()) return;
	Connection->SendChat(Text);
	InputBox->SetText(FText::GetEmpty());
	StreamingText.Reset();
	// 아래에서 "생각 중..." 을 직접 넣으므로 갱신 플래그를 내린다. 남겨두면 다음
	// 틱이 빈 StreamingText 로 덮어써 안내 문구가 사라진다.
	bStreamingDirty = false;
	if (DialogueText)
	{
		DialogueText->SetText(FText::FromString(TEXT("생각 중...")));
	}
}

void UHermesDialogueWidget::HandleChatDelta(const FString& Text, const FString& Id)
{
	// 가장 최근에 보낸 발화의 응답만 화면에 반영한다. 늦게 도착한 이전
	// 발화의 응답이 현재 화면을 덮어쓰지 않게 한다. 서버가 턴 직렬화
	// (프로토콜 §3.6)를 어기고 두 턴을 교차 전송해도 다른 턴의 델타는 여기서
	// 걸러지므로 두 답변이 뒤섞인 문자열이 남지 않는다(§4.9).
	if (!Connection || Id != Connection->GetLastSentChatId())
	{
		UE_LOG(LogHermes, Verbose, TEXT("ignoring stale chat_delta for %s"), *Id);
		return;
	}

	// 누적만 하고 위젯은 건드리지 않는다. 한 틱에 델타가 여러 개 들어오면
	// SetText 를 그 횟수만큼 부르게 되는데, 마지막 한 번만 화면에 의미가 있고
	// 나머지는 Slate 텍스트 레이아웃을 헛되이 무효화한다.
	// 실제 갱신은 NativeTick 에서 틱당 1회만 수행한다.
	StreamingText += Text;
	bStreamingDirty = true;
}

void UHermesDialogueWidget::HandleChatResponse(const FString& Text, const FString& Id)
{
	if (!Connection || Id != Connection->GetLastSentChatId())
	{
		UE_LOG(LogHermes, Verbose, TEXT("ignoring stale chat_response for %s"), *Id);
		return;
	}

	// 델타를 놓치거나 중복 처리했더라도 여기서 정본으로 교체되어 자기 교정된다.
	StreamingText = Text;
	bStreamingDirty = true;
}

void UHermesDialogueWidget::HandleChatFailed(const FString& Id, const FString& Reason)
{
	if (!Connection || Id != Connection->GetLastSentChatId())
	{
		return;
	}

	StreamingText = TEXT("응답을 받지 못했습니다.");
	bStreamingDirty = true;
}

void UHermesDialogueWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (bStreamingDirty && DialogueText)
	{
		DialogueText->SetText(FText::FromString(StreamingText));
		bStreamingDirty = false;
	}
}

void UHermesDialogueWidget::HandleConnState(bool bReady)
{
	if (!bReady && DialogueText)
	{
		// OnSendClicked 과 같은 이유로 갱신 플래그를 내린다. 스트리밍 도중 연결이
		// 끊기면 다음 틱이 누적 텍스트로 이 안내 문구를 덮어쓴다.
		bStreamingDirty = false;
		DialogueText->SetText(FText::FromString(TEXT("연결 중...")));
	}
}
