#pragma once
#include "CoreMinimal.h"

/**
 * Hermes 프레이밍 코덱: 4바이트 big-endian 길이 프리픽스 + UTF-8 JSON 바디.
 * 엔진 게임플레이 타입에 의존하지 않는 순수 로직으로, 자동화 테스트로 단독 검증한다.
 */
class FHermesFrameCodec
{
public:
	static constexpr int32 MaxBodySize = 1048576; // 1 MiB

	/** JsonBody(UTF-8) 앞에 4바이트 big-endian 길이를 붙여 OutBytes에 채운다. 빈 바디/초과 시 false. */
	static bool Encode(const FString& JsonBody, TArray<uint8>& OutBytes);
};

/**
 * 수신 바이트 스트림을 누적하며, 길이 프리픽스 기준으로만 완성 프레임을 하나씩 꺼낸다.
 * 패킷 경계에 의존하지 않는다. len==0 또는 len>1MiB는 프로토콜 에러(연결 종료 유발).
 */
class FFrameAccumulator
{
public:
	void Feed(const uint8* Data, int32 Len);
	bool TryPop(FString& OutJson);
	bool HasError() const { return bError; }

private:
	TArray<uint8> Buffer;
	bool bError = false;
};
