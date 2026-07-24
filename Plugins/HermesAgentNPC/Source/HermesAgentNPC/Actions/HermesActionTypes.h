#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/** action_request 한 건의 파싱된 페이로드. */
struct FHermesActionPayload
{
	FString Id;
	FString Command;
	TSharedPtr<FJsonObject> Params;
};

/** 핸들러가 실행 결과를 보고하는 델리게이트. bOk / result(선택) / error(선택). */
DECLARE_DELEGATE_ThreeParams(FHermesActionResultDelegate,
	bool /*bOk*/, TSharedPtr<FJsonObject> /*Result*/, FString /*Error*/);
