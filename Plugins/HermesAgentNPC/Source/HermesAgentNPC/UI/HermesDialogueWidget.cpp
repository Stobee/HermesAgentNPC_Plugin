#include "UI/HermesDialogueWidget.h"
#include "UI/HermesChatCorrelation.h"
#include "Connection/HermesConnectionSubsystem.h"
#include "HermesLog.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

void UHermesDialogueWidget::OpenFor(UHermesConnectionSubsystem* Conn)
{
	// 이미 열려 있는데 다시 열면 델리게이트가 겹쳐 붙어 한 번 눌러도 두 번
	// 전송된다. NPC 가 위젯 인스턴스를 캐시해 재사용하므로 실제로 일어난다.
	if (bIsOpen)
	{
		return;
	}

	Connection = Conn;
	BindEvents();
	AddToViewport();

	// 이것이 없으면 창은 떠도 마우스가 뷰포트에 잡힌 채라 입력창도 버튼도
	// 누를 수 없다. 창을 띄우는 것과 입력을 넘기는 것은 한 몸이어야 한다.
	ApplyInputMode(true);

	bIsOpen = true;
}

void UHermesDialogueWidget::Close()
{
	if (!bIsOpen)
	{
		return;
	}

	UnbindEvents();
	ApplyInputMode(false);
	RemoveFromParent();
	bIsOpen = false;
}

void UHermesDialogueWidget::BindEvents()
{
	if (SendButton)
	{
		SendButton->OnClicked.AddDynamic(this, &UHermesDialogueWidget::OnSendClicked);
	}
	if (InputBox)
	{
		InputBox->OnTextCommitted.AddDynamic(this, &UHermesDialogueWidget::OnInputCommitted);
	}
	if (Connection)
	{
		ChatHandle   = Connection->OnChatResponse.AddUObject(this, &UHermesDialogueWidget::HandleChatResponse);
		DeltaHandle  = Connection->OnChatDelta.AddUObject(this, &UHermesDialogueWidget::HandleChatDelta);
		FailedHandle = Connection->OnChatFailed.AddUObject(this, &UHermesDialogueWidget::HandleChatFailed);
		StateHandle  = Connection->OnConnectionStateChanged.AddUObject(this, &UHermesDialogueWidget::HandleConnState);
	}
}

void UHermesDialogueWidget::UnbindEvents()
{
	if (SendButton)
	{
		SendButton->OnClicked.RemoveDynamic(this, &UHermesDialogueWidget::OnSendClicked);
	}
	if (InputBox)
	{
		InputBox->OnTextCommitted.RemoveDynamic(this, &UHermesDialogueWidget::OnInputCommitted);
	}
	if (Connection)
	{
		Connection->OnChatResponse.Remove(ChatHandle);
		Connection->OnChatDelta.Remove(DeltaHandle);
		Connection->OnChatFailed.Remove(FailedHandle);
		Connection->OnConnectionStateChanged.Remove(StateHandle);
		ChatHandle.Reset();
		DeltaHandle.Reset();
		FailedHandle.Reset();
		StateHandle.Reset();
	}
}

void UHermesDialogueWidget::ApplyInputMode(bool bToWidget)
{
	// GameInstance 를 Outer 로 만들어진 위젯이라 소유 컨트롤러가 비어 있을 수 있다.
	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		PC = UGameplayStatics::GetPlayerController(this, 0);
	}
	if (!PC)
	{
		UE_LOG(LogHermes, Warning,
			TEXT("no player controller; the dialogue window will not accept input"));
		return;
	}

	if (bToWidget)
	{
		// UIOnly 를 쓴다. GameAndUI 로 두면 타이핑한 글자가 게임 입력으로도
		// 흘러 이동·점프가 섞인다. 대신 나가는 길을 반드시 둬야 하므로
		// 입력창에서 Esc 를 누르면 닫히게 했다(OnInputCommitted).
		FInputModeUIOnly Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		if (InputBox)
		{
			Mode.SetWidgetToFocus(InputBox->TakeWidget());
		}
		PC->SetInputMode(Mode);
		PC->SetShowMouseCursor(true);

		if (InputBox)
		{
			// 바로 타이핑할 수 있게 캐럿을 입력창에 둔다.
			InputBox->SetKeyboardFocus();
		}
	}
	else
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetShowMouseCursor(false);
	}
}

void UHermesDialogueWidget::NativeDestruct()
{
	UnbindEvents();
	Super::NativeDestruct();
}

void UHermesDialogueWidget::OnInputCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		SendCurrentText();
		// 전송 후 포커스가 풀리면 다음 발화를 타이핑할 수 없다. 되돌려 준다.
		if (InputBox)
		{
			InputBox->SetKeyboardFocus();
		}
	}
	else if (CommitMethod == ETextCommit::OnCleared)
	{
		// Esc. UIOnly 라 게임 입력이 닿지 않으므로 여기가 유일한 퇴로다.
		Close();
	}
}

void UHermesDialogueWidget::OnSendClicked()
{
	SendCurrentText();
}

void UHermesDialogueWidget::SendCurrentText()
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
	if (!Connection || !HermesChatCorrelation::ShouldDisplayDelta(
		Id, Connection->GetLastSentChatId()))
	{
		UE_LOG(LogHermes, Verbose, TEXT("ignoring stale chat_delta for '%s'"), *Id);
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
	// id 가 없으면 자발 발화다 — action_event 이후 서버가 먼저 거는 말이며
	// 스펙 §4.4 가 id 를 optional 로 둔 이유가 이것이다. 상관 규칙을 그대로
	// 적용하면 대응하는 발화가 없어 통째로 사라진다.
	if (!Connection || !HermesChatCorrelation::ShouldDisplayResponse(
		Id, Connection->GetLastSentChatId()))
	{
		UE_LOG(LogHermes, Verbose, TEXT("ignoring stale chat_response for '%s'"), *Id);
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
