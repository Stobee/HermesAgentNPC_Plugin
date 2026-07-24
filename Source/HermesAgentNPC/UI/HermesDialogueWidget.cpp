#include "UI/HermesDialogueWidget.h"
#include "Connection/HermesConnectionSubsystem.h"
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
		StateHandle = Connection->OnConnectionStateChanged.AddUObject(this, &UHermesDialogueWidget::HandleConnState);
	}
	AddToViewport();
}

void UHermesDialogueWidget::NativeDestruct()
{
	if (Connection)
	{
		Connection->OnChatResponse.Remove(ChatHandle);
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
	if (DialogueText)
	{
		DialogueText->SetText(FText::FromString(TEXT("생각 중...")));
	}
}

void UHermesDialogueWidget::HandleChatResponse(const FString& Text, const FString& Id)
{
	if (DialogueText)
	{
		DialogueText->SetText(FText::FromString(Text));
	}
}

void UHermesDialogueWidget::HandleConnState(bool bReady)
{
	if (!bReady && DialogueText)
	{
		DialogueText->SetText(FText::FromString(TEXT("연결 중...")));
	}
}
