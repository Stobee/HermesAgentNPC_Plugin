#include "UI/HermesChatCorrelation.h"

namespace HermesChatCorrelation
{
	bool ShouldDisplayResponse(const FString& IncomingId, const FString& LastSentId)
	{
		// id 없는 응답은 자발 발화다. 서버가 먼저 거는 말이므로 대응하는
		// 발화가 없고, 상관 규칙을 그대로 적용하면 통째로 사라진다.
		if (IncomingId.IsEmpty())
		{
			return true;
		}
		return IncomingId == LastSentId;
	}

	bool ShouldDisplayDelta(const FString& IncomingId, const FString& LastSentId)
	{
		// 델타는 id 가 필수다. 비어 있으면 규격 위반이므로 버린다.
		// 두 값이 모두 비었을 때 등호로 통과시키면, 발화를 보내기도 전에
		// 도착한 id 없는 델타가 화면에 뜬다.
		if (IncomingId.IsEmpty())
		{
			return false;
		}
		return IncomingId == LastSentId;
	}
}
