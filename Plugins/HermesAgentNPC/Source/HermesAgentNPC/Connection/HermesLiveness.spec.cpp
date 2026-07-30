#include "Misc/AutomationTest.h"
#include "Connection/HermesLiveness.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesLivenessTest,
	"Hermes.Liveness.Evaluate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesLivenessTest::RunTest(const FString& Parameters)
{
	using HermesLiveness::EDecision;
	const float PingInterval = 20.f;
	const float PeerTimeout  = 60.f;

	// 최근 수신·송신이 모두 있으면 아무것도 하지 않는다.
	{
		const EDecision D = HermesLiveness::Evaluate(
			/*Now*/ 100.0, /*LastRecv*/ 99.0, /*LastSend*/ 99.0, PingInterval, PeerTimeout);
		TestTrue(TEXT("idle => Nothing"), D == EDecision::Nothing);
	}

	// 송신 침묵이 PingInterval 을 넘으면 ping 을 보낸다.
	{
		const EDecision D = HermesLiveness::Evaluate(
			/*Now*/ 100.0, /*LastRecv*/ 95.0, /*LastSend*/ 79.0, PingInterval, PeerTimeout);
		TestTrue(TEXT("send silence => SendPing"), D == EDecision::SendPing);
	}

	// 수신 침묵이 PeerTimeout 을 넘으면 죽은 것으로 판정한다.
	{
		const EDecision D = HermesLiveness::Evaluate(
			/*Now*/ 100.0, /*LastRecv*/ 39.0, /*LastSend*/ 99.0, PingInterval, PeerTimeout);
		TestTrue(TEXT("recv silence => DeclareDead"), D == EDecision::DeclareDead);
	}

	// 둘 다 넘었으면 DeclareDead 가 우선한다. 죽은 연결에 ping 을 보내지 않는다.
	{
		const EDecision D = HermesLiveness::Evaluate(
			/*Now*/ 100.0, /*LastRecv*/ 30.0, /*LastSend*/ 30.0, PingInterval, PeerTimeout);
		TestTrue(TEXT("both => DeclareDead wins"), D == EDecision::DeclareDead);
	}

	// 경계: 정확히 PingInterval 이면 SendPing.
	{
		const EDecision D = HermesLiveness::Evaluate(
			/*Now*/ 100.0, /*LastRecv*/ 99.0, /*LastSend*/ 80.0, PingInterval, PeerTimeout);
		TestTrue(TEXT("exactly ping interval => SendPing"), D == EDecision::SendPing);
	}

	// 경계: PingInterval 직전이면 Nothing.
	{
		const EDecision D = HermesLiveness::Evaluate(
			/*Now*/ 100.0, /*LastRecv*/ 99.0, /*LastSend*/ 80.1, PingInterval, PeerTimeout);
		TestTrue(TEXT("just under ping interval => Nothing"), D == EDecision::Nothing);
	}

	// 경계: 정확히 PeerTimeout 이면 DeclareDead.
	{
		const EDecision D = HermesLiveness::Evaluate(
			/*Now*/ 100.0, /*LastRecv*/ 40.0, /*LastSend*/ 99.0, PingInterval, PeerTimeout);
		TestTrue(TEXT("exactly peer timeout => DeclareDead"), D == EDecision::DeclareDead);
	}

	// 경계: PeerTimeout 직전이면 죽지 않는다.
	{
		const EDecision D = HermesLiveness::Evaluate(
			/*Now*/ 100.0, /*LastRecv*/ 40.1, /*LastSend*/ 99.0, PingInterval, PeerTimeout);
		TestTrue(TEXT("just under peer timeout => Nothing"), D == EDecision::Nothing);
	}

	return true;
}
