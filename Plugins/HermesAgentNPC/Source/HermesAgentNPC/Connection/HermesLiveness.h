#pragma once
#include "CoreMinimal.h"

/**
 * 연결 생존 판정.
 * 시간을 인자로 주입받는 순수 로직이라 단독 테스트가 가능하다.
 * 프레임 종류를 아는 게임 스레드에서 호출하고, 워커는 프로토콜을 모르는 상태로 둔다.
 */
namespace HermesLiveness
{
	enum class EDecision : uint8
	{
		Nothing,
		SendPing,
		DeclareDead
	};

	/**
	 * LastRecvTime 은 종류를 가리지 않는 마지막 수신 시각이다. pong 뿐 아니라
	 * 모든 수신이 생존 신호이므로 대화가 활발하면 ping 없이도 판정이 성립한다.
	 *
	 * 경계는 모두 이상(>=)으로 판정한다. 정확히 PeerTimeout 이면 DeclareDead,
	 * 정확히 PingInterval 이면 SendPing 이다.
	 * 죽은 연결에 ping 을 보내지 않도록 DeclareDead 가 우선한다.
	 */
	EDecision Evaluate(double Now, double LastRecvTime, double LastSendTime,
	                   float PingInterval, float PeerTimeout);
}
