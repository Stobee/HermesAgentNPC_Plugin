#pragma once
#include "CoreMinimal.h"

/**
 * 프레임 트레이스 로그 정형.
 *
 * 실제 서버에 붙으면 스텁처럼 서버가 프레임을 찍어 주지 않는다. 무엇이 오갔는지
 * 클라이언트 쪽에서 보이지 않으면 어긋났을 때 추적할 방법이 없다. 그래서 송수신
 * 프레임을 Verbose 로 남기되, 로그 파일에 남으면 곤란한 값은 가린다.
 *
 * 가리는 것은 `session_token` 하나다. 이것이 세션을 대신하는 자격 증명이므로
 * 로그를 공유하는 순간 남의 세션에 붙을 수 있는 값이 된다.
 * `player_id` 는 가리지 않는다 — 비밀이 아니고, 신원 발급·재사용 흐름을 쫓을 때
 * 반드시 필요하다.
 *
 * 시간도 상태도 없는 순수 함수라 단독 테스트가 가능하다.
 */
namespace HermesTrace
{
	/** 잘리지 않는 최대 길이. 넘으면 뒤를 자르고 원래 길이를 덧붙인다. */
	inline constexpr int32 DefaultMaxChars = 512;

	FString FormatFrame(const FString& Json, int32 MaxChars = DefaultMaxChars);
}
