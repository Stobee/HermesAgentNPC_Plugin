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
		DeltaHandle = Connection->OnChatDelta.AddUObject(this, &UHermesDialogueWidget::HandleChatDelta);
		StateHandle = Connection->OnConnectionStateChanged.AddUObject(this, &UHermesDialogueWidget::HandleConnState);
	}
	AddToViewport();
}

void UHermesDialogueWidget::NativeDestruct()
{
	if (Connection)
	{
		Connection->OnChatResponse.Remove(ChatHandle);
		Connection->OnChatDelta.Remove(DeltaHandle);
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
	StreamingId.Reset();
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
	if (Id != StreamingId)
	{
		// 정상 흐름에서는 발화 전송 때 비운 빈 StreamingId 에 새 턴의 id 가 처음
		// 채워지는 경우뿐이다. 서버가 턴 직렬화(프로토콜 §3.6)를 어기고 두 턴을
		// 교차 전송했다면 두 답변이 뒤섞인 문자열을 화면에 남기지 않도록 버퍼를
		// 합치지 말고 버리고 새로 시작한다(§4.9).
		if (!StreamingId.IsEmpty())
		{
			UE_LOG(LogHermes, Warning,
				TEXT("chat_delta id changed mid-turn ('%s' -> '%s'); discarding accumulated text"),
				*StreamingId, *Id);
		}
		StreamingText.Reset();
		StreamingId = Id;
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
	// 델타를 놓치거나 중복 처리했더라도 여기서 정본으로 교체되어 자기 교정된다.
	StreamingText = Text;
	StreamingId   = Id;
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
