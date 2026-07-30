#include "Actions/MoveToActionHandler.h"
#include "NPC/HermesNPCCharacter.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "Dom/JsonObject.h"
#include "Actions/HermesActionParams.h"
#include "Settings/HermesSettings.h"
#include "Connection/HermesConnectionSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/GameInstance.h"

void UMoveToActionHandler::Execute(const FHermesActionPayload& Payload, FHermesActionResultDelegate OnDone)
{
	if (!Npc.IsValid())
	{
		OnDone.ExecuteIfBound(false, nullptr, TEXT("npc unavailable"));
		return;
	}

	const TSharedPtr<FJsonObject>* Loc = nullptr;
	double X = 0, Y = 0, Z = 0;
	if (!Payload.Params.IsValid() ||
		!Payload.Params->TryGetObjectField(TEXT("location"), Loc) ||
		!(*Loc)->TryGetNumberField(TEXT("x"), X) ||
		!(*Loc)->TryGetNumberField(TEXT("y"), Y) ||
		!(*Loc)->TryGetNumberField(TEXT("z"), Z))
	{
		OnDone.ExecuteIfBound(false, nullptr, TEXT("invalid location"));
		return;
	}

	// FVector 생성 전에 검사한다. non-finite 벡터가 MoveToLocation 에 들어가면
	// 엔진이 check 실패하거나 네비게이션이 비정상 동작한다.
	const float CoordLimit = GetDefault<UHermesSettings>()->MaxWorldCoordinate;
	if (!HermesParams::IsValidCoordinate(X, CoordLimit) ||
		!HermesParams::IsValidCoordinate(Y, CoordLimit) ||
		!HermesParams::IsValidCoordinate(Z, CoordLimit))
	{
		OnDone.ExecuteIfBound(false, nullptr, TEXT("coordinate out of range"));
		return;
	}

	AAIController* AI = Cast<AAIController>(Npc->GetController());
	if (!AI)
	{
		OnDone.ExecuteIfBound(false, nullptr, TEXT("no ai controller"));
		return;
	}

	const FVector Dest((float)X, (float)Y, (float)Z);

	// MoveToLocation 은 이전 이동을 동기로 중단시킨다 — PathFollowingComponent::RequestMove
	// 가 새 MoveId 를 저장하기 전에 OnPathFinished(Aborted) 를 부르고, 그것이
	// ReceiveMoveCompleted 를 동기 브로드캐스트한다. 그래서 이 호출 안에서 아래
	// OnMoveCompleted 가 실행되며, 그때 PendingMoveId 는 아직 이전 이동의 id 다.
	// 이전 이동은 그 id 로 failed 통지를 받고, 새 id 는 그 뒤에 들어간다.
	const EPathFollowingRequestResult::Type R = AI->MoveToLocation(Dest);
	if (R == EPathFollowingRequestResult::Failed)
	{
		// 길찾기가 거부되면 event 를 보내지 않는다 (프로토콜 §4.5 move_to).
		OnDone.ExecuteIfBound(false, nullptr, TEXT("path blocked"));
		return;
	}

	// action_result 는 "접수했고 시작했다"까지만 말한다. 도착 여부는
	// 나중에 action_event 로 알린다 (프로토콜 §4.5, §4.10).
	// 완료를 기다렸다 회신하면 15초 예산을 넘겨 서버 타임아웃이 난다.
	TSharedPtr<FJsonObject> Res = MakeShared<FJsonObject>();
	Res->SetBoolField(TEXT("started"), true);

	if (R == EPathFollowingRequestResult::AlreadyAtGoal)
	{
		// 이미 목표 지점이면 이동이 시작되지도 않는다. AAIController::MoveTo 가
		// RequestMoveWithImmediateFinish(Success) 로 처리해 완료 콜백이 위
		// MoveToLocation 안에서 이미 끝나버렸으므로, 델리게이트를 걸어도 잡을 것이
		// 없다. 여기서 접수와 완료를 직접 순서대로 보낸다 — action_event 는 같은
		// id 의 action_result 뒤에 와야 한다 (프로토콜 §4.10).
		Res->SetNumberField(TEXT("eta_seconds"), 0.0);
		OnDone.ExecuteIfBound(true, Res, FString());

		if (UHermesConnectionSubsystem* Conn = GetConnection())
		{
			TSharedPtr<FJsonObject> Arrived = MakeShared<FJsonObject>();
			Arrived->SetBoolField(TEXT("arrived"), true);
			Conn->SendActionEvent(Payload.Id, true, Arrived, FString());
		}
		return;
	}

	PendingMoveId = Payload.Id;

	AI->ReceiveMoveCompleted.AddUniqueDynamic(this, &UMoveToActionHandler::OnMoveCompleted);

	// 직선 거리 / 최대 속도로 대략의 도착 시간을 낸다. 프로토콜이 best-effort 로
	// 규정하므로 남은 경로 길이까지 구하지 않는다. 알 수 없으면 필드를 뺀다.
	const float Speed = Npc->GetCharacterMovement() ? Npc->GetCharacterMovement()->GetMaxSpeed() : 0.f;
	if (Speed > KINDA_SMALL_NUMBER)
	{
		const float Dist = FVector::Dist(Npc->GetActorLocation(), Dest);
		Res->SetNumberField(TEXT("eta_seconds"), Dist / Speed);
	}

	OnDone.ExecuteIfBound(true, Res, FString());
}

UHermesConnectionSubsystem* UMoveToActionHandler::GetConnection() const
{
	if (!Npc.IsValid())
	{
		return nullptr;
	}
	if (UGameInstance* GI = Npc->GetGameInstance())
	{
		return GI->GetSubsystem<UHermesConnectionSubsystem>();
	}
	return nullptr;
}

void UMoveToActionHandler::OnMoveCompleted(FAIRequestID RequestID, EPathFollowingResult::Type Result)
{
	if (PendingMoveId.IsEmpty())
	{
		return;
	}

	// 프로토콜상 id 당 event 는 최대 1회다. 먼저 비워 재진입을 막는다.
	const FString Id = PendingMoveId;
	PendingMoveId.Reset();

	if (AAIController* AI = Npc.IsValid() ? Cast<AAIController>(Npc->GetController()) : nullptr)
	{
		AI->ReceiveMoveCompleted.RemoveDynamic(this, &UMoveToActionHandler::OnMoveCompleted);
	}

	UHermesConnectionSubsystem* Conn = GetConnection();
	if (!Conn)
	{
		return; // 연결이 사라졌다. 프로토콜상 event 유실은 허용된다.
	}

	if (Result == EPathFollowingResult::Success)
	{
		TSharedPtr<FJsonObject> Res = MakeShared<FJsonObject>();
		Res->SetBoolField(TEXT("arrived"), true);
		Conn->SendActionEvent(Id, true, Res, FString());
	}
	else
	{
		Conn->SendActionEvent(Id, false, nullptr, TEXT("path blocked"));
	}
}
