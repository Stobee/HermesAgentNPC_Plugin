#include "Misc/AutomationTest.h"
#include "Protocol/HermesFrameCodec.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesFrameCodecEncodeTest,
	"Hermes.Protocol.FrameCodec.Encode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesFrameCodecEncodeTest::RunTest(const FString& Parameters)
{
	// ASCII 바디 "{}" -> 길이 2
	TArray<uint8> Out;
	TestTrue(TEXT("encode ok"), FHermesFrameCodec::Encode(TEXT("{}"), Out));
	TestEqual(TEXT("total len = 4 + 2"), Out.Num(), 6);
	TestEqual(TEXT("prefix byte0"), (int32)Out[0], 0);
	TestEqual(TEXT("prefix byte1"), (int32)Out[1], 0);
	TestEqual(TEXT("prefix byte2"), (int32)Out[2], 0);
	TestEqual(TEXT("prefix byte3"), (int32)Out[3], 2);
	TestEqual(TEXT("body0"), (int32)Out[4], (int32)'{');
	TestEqual(TEXT("body1"), (int32)Out[5], (int32)'}');

	// UTF-8 멀티바이트: "가"는 UTF-8 3바이트
	TArray<uint8> Out2;
	TestTrue(TEXT("encode utf8 ok"), FHermesFrameCodec::Encode(TEXT("가"), Out2));
	TestEqual(TEXT("utf8 body len 3 -> total 7"), Out2.Num(), 7);
	TestEqual(TEXT("utf8 prefix == 3"), (int32)Out2[3], 3);

	// 1 MiB 초과 거부
	FString Huge = FString::ChrN(FHermesFrameCodec::MaxBodySize + 1, TEXT('a'));
	TArray<uint8> Out3;
	TestFalse(TEXT("oversize rejected"), FHermesFrameCodec::Encode(Huge, Out3));

	// 빈 바디 거부
	TArray<uint8> Out4;
	TestFalse(TEXT("empty rejected"), FHermesFrameCodec::Encode(TEXT(""), Out4));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrameAccumulatorTest,
	"Hermes.Protocol.FrameAccumulator.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFrameAccumulatorTest::RunTest(const FString& Parameters)
{
	// 두 프레임을 이어 붙인 뒤 1바이트씩 흘려넣어도 정확히 2개가 나와야 한다.
	TArray<uint8> A, B, Stream;
	FHermesFrameCodec::Encode(TEXT("{\"type\":\"ping\"}"), A);
	FHermesFrameCodec::Encode(TEXT("{\"type\":\"pong\"}"), B);
	Stream.Append(A);
	Stream.Append(B);

	FFrameAccumulator Acc;
	TArray<FString> Popped;
	for (int32 i = 0; i < Stream.Num(); ++i)
	{
		Acc.Feed(&Stream[i], 1); // 1바이트씩 (패킷 경계 무관)
		FString Json;
		while (Acc.TryPop(Json))
		{
			Popped.Add(Json);
		}
	}
	TestEqual(TEXT("two frames parsed"), Popped.Num(), 2);
	if (Popped.Num() == 2)
	{
		TestEqual(TEXT("frame0"), Popped[0], TEXT("{\"type\":\"ping\"}"));
		TestEqual(TEXT("frame1"), Popped[1], TEXT("{\"type\":\"pong\"}"));
	}
	TestFalse(TEXT("no error"), Acc.HasError());

	// len == 0 은 프로토콜 에러
	FFrameAccumulator Acc2;
	const uint8 ZeroLen[4] = {0, 0, 0, 0};
	Acc2.Feed(ZeroLen, 4);
	FString Dummy;
	TestFalse(TEXT("zero-len pops nothing"), Acc2.TryPop(Dummy));
	TestTrue(TEXT("zero-len is error"), Acc2.HasError());

	// len > 1 MiB 는 프로토콜 에러 (0x00200000 = 2 MiB)
	FFrameAccumulator Acc3;
	const uint8 BigLen[4] = {0x00, 0x20, 0x00, 0x00};
	Acc3.Feed(BigLen, 4);
	FString Dummy2;
	Acc3.TryPop(Dummy2);
	TestTrue(TEXT("oversize is error"), Acc3.HasError());

	return true;
}
