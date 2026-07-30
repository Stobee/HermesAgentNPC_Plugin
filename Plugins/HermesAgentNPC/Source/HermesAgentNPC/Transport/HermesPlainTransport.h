#pragma once
#include "CoreMinimal.h"
#include "Transport/IHermesTransport.h"

class FSocket;

/** FSocket 기반 평문 TCP 전송. 기존 워커 코드를 그대로 옮긴 것이다. */
class FHermesPlainTransport : public IHermesTransport
{
public:
	virtual ~FHermesPlainTransport() override;

	virtual bool  Connect(const FHermesWorkerConfig& Config, const FInternetAddr& Addr) override;
	virtual void  Close() override;
	virtual bool  HasPendingData(uint32& OutBytes) override;
	virtual int32 Recv(uint8* Buf, int32 BufSize) override;
	virtual int32 Send(const uint8* Buf, int32 Num) override;

private:
	FSocket* Socket = nullptr;
};
