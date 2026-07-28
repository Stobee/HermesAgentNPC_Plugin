#pragma once
#include "CoreMinimal.h"

/**
 * 액션 파라미터의 범위·유한성 검증.
 * 엔진 게임플레이 타입에 의존하지 않는 순수 로직이라 단독 테스트가 가능하다.
 * 서버를 신뢰하지 않는다는 전제로, 값을 쓰기 전에 반드시 통과시킨다.
 */
namespace HermesParams
{
	/** 유한하고 |V| <= Limit 인지 검사한다. NaN/Inf 는 거부. */
	bool IsValidCoordinate(double V, float Limit);

	/**
	 * 유한한 정수이고 1 <= V <= Max 인지 검사한다.
	 * int32 캐스트 이전에 호출해야 미정의 동작을 피할 수 있다.
	 * 통과 시에만 OutQty 에 값을 채운다.
	 */
	bool IsValidQuantity(double V, int32 Max, int32& OutQty);

	/** 비어있지 않고 길이가 MaxLen 이하인지 검사한다. */
	bool IsValidItemId(const FString& Id, int32 MaxLen);
}
