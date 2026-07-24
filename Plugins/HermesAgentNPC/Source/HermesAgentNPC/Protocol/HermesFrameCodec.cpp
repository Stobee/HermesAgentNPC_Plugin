#include "Protocol/HermesFrameCodec.h"

bool FHermesFrameCodec::Encode(const FString& JsonBody, TArray<uint8>& OutBytes)
{
	FTCHARToUTF8 Utf8(*JsonBody);
	const int32 BodyLen = Utf8.Length();
	if (BodyLen <= 0 || BodyLen > MaxBodySize)
	{
		return false;
	}

	OutBytes.Reset(4 + BodyLen);
	OutBytes.Add((uint8)((BodyLen >> 24) & 0xFF));
	OutBytes.Add((uint8)((BodyLen >> 16) & 0xFF));
	OutBytes.Add((uint8)((BodyLen >> 8) & 0xFF));
	OutBytes.Add((uint8)(BodyLen & 0xFF));
	OutBytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), BodyLen);
	return true;
}

void FFrameAccumulator::Feed(const uint8* Data, int32 Len)
{
	if (bError || Len <= 0)
	{
		return;
	}
	Buffer.Append(Data, Len);
}

bool FFrameAccumulator::TryPop(FString& OutJson)
{
	if (bError || Buffer.Num() < 4)
	{
		return false;
	}

	const uint32 BodyLen =
		((uint32)Buffer[0] << 24) | ((uint32)Buffer[1] << 16) |
		((uint32)Buffer[2] << 8)  |  (uint32)Buffer[3];

	if (BodyLen == 0 || BodyLen > (uint32)FHermesFrameCodec::MaxBodySize)
	{
		bError = true;
		return false;
	}
	if ((uint32)Buffer.Num() < 4 + BodyLen)
	{
		return false; // 바디 아직 미완성
	}

	const uint8* BodyPtr = Buffer.GetData() + 4;
	FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(BodyPtr), (int32)BodyLen);
	OutJson = FString(FStringView(Conv.Get(), Conv.Length()));

	Buffer.RemoveAt(0, 4 + BodyLen, EAllowShrinking::No);
	return true;
}
