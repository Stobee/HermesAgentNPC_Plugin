#pragma once
#include "CoreMinimal.h"

/**
 * error 프레임의 code 에 대한 클라이언트 반응.
 * 프로토콜 §5 의 표와 1:1 로 대응한다.
 */
enum class EHermesErrorReaction : uint8
{
	/** unknown_type, unknown_command — 로그만 남기고 계속한다. */
	LogOnly,

	/** not_identified — 연결은 살아 있다. identify 를 다시 보낸다. */
	ReIdentify,

	/** not_authorized — 저장된 자격 증명을 버리고 신규 발급을 요청한다. */
	DiscardCredentials,

	/** session_taken_over, unsupported_version — 재연결 루프를 멈춘다. */
	StopReconnect,

	/** rate_limited, server_busy, internal_error — 진행 중인 턴을 실패시킨다. */
	FailPendingTurn,

	/** bad_frame — 평소대로 백오프 재연결한다. */
	ReconnectWithBackoff
};

/**
 * 코드 → 반응 매핑. 분기 외 로직이 없는 순수 함수라 단독 테스트가 가능하다.
 * 이 판정을 서브시스템 안에 if 사슬로 흩뿌리면 테스트할 수 없고 코드가 늘 때마다
 * 누락이 생긴다.
 */
namespace HermesErrorPolicy
{
	/**
	 * 프로토콜 §5 에 없는 code 는 모두 LogOnly 다.
	 *
	 * **미지의 코드를 종료성으로 취급하면 서버 한 번의 오타가 클라이언트를 영구
	 * 정지시킨다.** 목록이 닫혀 있다는 것은 서버가 지켜야 할 규약이지, 클라이언트가
	 * 위반을 치명적으로 다뤄야 한다는 뜻이 아니다.
	 *
	 * 비교는 FString 기본 의미대로 대소문자를 구분하지 않는다. §5 의 코드는 모두
	 * 소문자이므로 이 관용은 오검출을 만들지 않는다.
	 */
	EHermesErrorReaction React(const FString& Code);
}
