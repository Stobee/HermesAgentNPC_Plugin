#pragma once
#include "CoreMinimal.h"
#include "Transport/HermesWorkerConfig.h"

class FInternetAddr;

/**
 * 연결 지향 바이트 스트림. 논블로킹 시맨틱을 따른다.
 *
 * 이 경계 덕분에 프레이밍·큐·백오프 로직은 평문과 TLS 를 구분하지 않는다.
 * 구현체는 워커 전용 스레드에서만 호출되므로 스레드 안전을 신경쓰지 않는다.
 */
class IHermesTransport
{
public:
	virtual ~IHermesTransport() = default;

	/** 연결하고, TLS 라면 핸드셰이크까지 끝낸다. 실패 시 내부 자원을 정리하고 false. */
	virtual bool Connect(const FHermesWorkerConfig& Config, const FInternetAddr& Addr) = 0;

	virtual void Close() = 0;

	/** 읽을 데이터가 대기 중인지 확인한다. */
	virtual bool HasPendingData(uint32& OutBytes) = 0;

	/** 반환: 읽은 바이트 수. 0 = 지금은 없음. 음수 = 치명적 오류(재연결). */
	virtual int32 Recv(uint8* Buf, int32 BufSize) = 0;

	/** 반환: 보낸 바이트 수. 0 = 지금은 불가(재시도). 음수 = 치명적 오류. */
	virtual int32 Send(const uint8* Buf, int32 Num) = 0;
};
