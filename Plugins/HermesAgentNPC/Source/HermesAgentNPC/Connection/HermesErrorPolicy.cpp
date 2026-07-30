#include "Connection/HermesErrorPolicy.h"

EHermesErrorReaction HermesErrorPolicy::React(const FString& Code)
{
	// 프로토콜 §5 표의 "Client reaction" 열을 그대로 옮긴 것이다.
	// 코드를 추가할 때는 저 표를 먼저 고치고 여기를 맞춘다.

	// 연결이 유지되는 코드들
	if (Code == TEXT("not_identified"))      return EHermesErrorReaction::ReIdentify;
	if (Code == TEXT("unknown_type"))        return EHermesErrorReaction::LogOnly;
	if (Code == TEXT("unknown_command"))     return EHermesErrorReaction::LogOnly;

	// 진행 중인 턴을 끝내는 코드들. 재전송은 클라이언트가 결정하지 않는다.
	if (Code == TEXT("rate_limited"))        return EHermesErrorReaction::FailPendingTurn;
	if (Code == TEXT("server_busy"))         return EHermesErrorReaction::FailPendingTurn;
	if (Code == TEXT("internal_error"))      return EHermesErrorReaction::FailPendingTurn;

	// 연결이 닫히는 코드들
	if (Code == TEXT("bad_frame"))           return EHermesErrorReaction::ReconnectWithBackoff;
	if (Code == TEXT("not_authorized"))      return EHermesErrorReaction::DiscardCredentials;

	// 재시도로 낫지 않는 코드들. 두 게임 인스턴스가 서로를 영원히 걷어내지
	// 않도록, 그리고 버전 불일치를 무한 재접속으로 덮지 않도록 멈춘다.
	if (Code == TEXT("unsupported_version")) return EHermesErrorReaction::StopReconnect;
	if (Code == TEXT("session_taken_over"))  return EHermesErrorReaction::StopReconnect;

	// 목록에 없는 코드. 종료성으로 취급하지 않는다 — 서버 한 번의 오타가
	// 클라이언트를 영구 정지시키면 안 된다.
	return EHermesErrorReaction::LogOnly;
}
