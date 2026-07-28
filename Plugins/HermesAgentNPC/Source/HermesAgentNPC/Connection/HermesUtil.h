#pragma once
#include "CoreMinimal.h"

/** 연결 계층에서 쓰는 작은 순수 헬퍼. */
namespace HermesUtil
{
	/**
	 * Item 을 Array 뒤에 넣되 길이가 MaxNum 을 넘으면 앞에서부터 버린다.
	 * 버린 개수를 반환한다. MaxNum 이 1 미만이면 1로 취급한다.
	 */
	int32 PushBounded(TArray<FString>& Array, const FString& Item, int32 MaxNum);
}
