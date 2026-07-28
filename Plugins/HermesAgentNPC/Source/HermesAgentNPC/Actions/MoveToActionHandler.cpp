#include "Actions/MoveToActionHandler.h"
#include "NPC/HermesNPCCharacter.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "Dom/JsonObject.h"
#include "Actions/HermesActionParams.h"
#include "Settings/HermesSettings.h"

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
	const EPathFollowingRequestResult::Type R = AI->MoveToLocation(Dest);
	if (R == EPathFollowingRequestResult::Failed)
	{
		OnDone.ExecuteIfBound(false, nullptr, TEXT("path blocked"));
		return;
	}

	// 최소 동작: 요청 성공을 도착으로 간주해 즉시 회신 (확장 시 OnMoveCompleted 델리게이트로 대체)
	TSharedPtr<FJsonObject> Res = MakeShared<FJsonObject>();
	Res->SetBoolField(TEXT("arrived"), true);
	OnDone.ExecuteIfBound(true, Res, FString());
}
