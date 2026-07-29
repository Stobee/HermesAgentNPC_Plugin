#include "HermesSaveGame.h"
#include "Misc/AES.h"

namespace
{
	/**
	 * 난독화 키. 바이너리 안에 있으므로 비밀이 아니다 — 이 값이 노출되는 것은
	 * 설계상 전제이고 위협 모델에 변화를 주지 않는다.
	 * FAES::AESBlockSize 배수 길이여야 한다.
	 */
	const ANSICHAR* ObfuscationKey = "HermesAgentNPCLocalObfuscationKey";

	void MakeKey(FAES::FAESKey& OutKey)
	{
		FMemory::Memzero(OutKey.Key, FAES::FAESKey::KeySize);
		const int32 Len = FMath::Min<int32>(
			FCStringAnsi::Strlen(ObfuscationKey), FAES::FAESKey::KeySize);
		FMemory::Memcpy(OutKey.Key, ObfuscationKey, Len);
	}
}

FString UHermesSaveGame::GetSessionToken() const
{
	if (ObfuscatedSessionToken.Num() == 0)
	{
		return FString();
	}

	TArray<uint8> Buffer = ObfuscatedSessionToken;
	FAES::FAESKey Key;
	MakeKey(Key);
	FAES::DecryptData(Buffer.GetData(), Buffer.Num(), Key);

	// 앞 4바이트에 원본 길이를 담아 블록 패딩을 걷어낸다.
	if (Buffer.Num() < 4)
	{
		return FString();
	}
	const int32 PlainLen =
		(int32)Buffer[0] | ((int32)Buffer[1] << 8) | ((int32)Buffer[2] << 16) | ((int32)Buffer[3] << 24);
	if (PlainLen < 0 || PlainLen > Buffer.Num() - 4)
	{
		// 손상되었거나 다른 키로 쓰인 데이터. 새 신원을 발급받게 둔다.
		return FString();
	}

	FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(Buffer.GetData() + 4), PlainLen);
	return FString(FStringView(Conv.Get(), Conv.Length()));
}

void UHermesSaveGame::SetSessionToken(const FString& PlainToken)
{
	ObfuscatedSessionToken.Reset();
	if (PlainToken.IsEmpty())
	{
		return;
	}

	FTCHARToUTF8 Utf8(*PlainToken);
	const int32 PlainLen = Utf8.Length();

	// [4바이트 길이][UTF-8 바디][블록 크기 패딩]
	const int32 Unpadded = 4 + PlainLen;
	const int32 Padded = ((Unpadded + FAES::AESBlockSize - 1) / FAES::AESBlockSize) * FAES::AESBlockSize;

	ObfuscatedSessionToken.SetNumZeroed(Padded);
	ObfuscatedSessionToken[0] = (uint8)(PlainLen & 0xFF);
	ObfuscatedSessionToken[1] = (uint8)((PlainLen >> 8) & 0xFF);
	ObfuscatedSessionToken[2] = (uint8)((PlainLen >> 16) & 0xFF);
	ObfuscatedSessionToken[3] = (uint8)((PlainLen >> 24) & 0xFF);
	FMemory::Memcpy(ObfuscatedSessionToken.GetData() + 4, Utf8.Get(), PlainLen);

	FAES::FAESKey Key;
	MakeKey(Key);
	FAES::EncryptData(ObfuscatedSessionToken.GetData(), ObfuscatedSessionToken.Num(), Key);
}
