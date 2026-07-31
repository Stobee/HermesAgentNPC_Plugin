#include "Connection/HermesTrace.h"

namespace
{
	const TCHAR* const TokenKey = TEXT("session_token");
	const TCHAR* const TokenMask = TEXT("***");

	int32 SkipWhitespace(const FString& S, int32 Index)
	{
		while (Index < S.Len() && FChar::IsWhitespace(S[Index]))
		{
			++Index;
		}
		return Index;
	}
}

namespace HermesTrace
{
	FString FormatFrame(const FString& Json, int32 MaxChars)
	{
		FString Out = Json;

		// JSON 을 파싱하지 않고 문자열로 훑는다. 트레이스는 원본을 그대로 보여주는
		// 것이 목적이라, 파싱해서 다시 직렬화하면 서버가 실제로 보낸 형태가 사라진다.
		int32 Search = 0;
		while (true)
		{
			const int32 KeyPos = Out.Find(TokenKey, ESearchCase::CaseSensitive,
				ESearchDir::FromStart, Search);
			if (KeyPos == INDEX_NONE)
			{
				break;
			}

			// 키 이름 뒤로 `"` `:` `"` 를 차례로 넘어간다. 사이의 공백은 허용한다 —
			// 어떤 형태로 직렬화해 보낼지는 서버가 정하는 것이지 우리가 아니다.
			int32 i = KeyPos + FCString::Strlen(TokenKey);
			i = SkipWhitespace(Out, i);
			if (i < Out.Len() && Out[i] == TEXT('"'))
			{
				++i;
			}
			i = SkipWhitespace(Out, i);
			if (i >= Out.Len() || Out[i] != TEXT(':'))
			{
				Search = KeyPos + 1;   // 값이 아닌 곳에 나온 이름이다. 넘어간다.
				continue;
			}
			i = SkipWhitespace(Out, i + 1);
			if (i >= Out.Len() || Out[i] != TEXT('"'))
			{
				Search = KeyPos + 1;   // 문자열이 아니다(null 등). 가릴 것이 없다.
				continue;
			}

			const int32 ValueStart = i + 1;
			int32 ValueEnd = ValueStart;
			while (ValueEnd < Out.Len() && Out[ValueEnd] != TEXT('"'))
			{
				++ValueEnd;
			}
			if (ValueEnd >= Out.Len())
			{
				// 따옴표가 닫히지 않았다. 잘린 프레임이므로 손대지 않는다 —
				// 어설프게 자르면 원래 무엇이 왔는지 알 수 없게 된다.
				break;
			}

			Out = Out.Left(ValueStart) + TokenMask + Out.Mid(ValueEnd);
			Search = ValueStart + FCString::Strlen(TokenMask);
		}

		// 1 MiB 프레임 하나가 로그를 덮으면 그 앞뒤 맥락을 잃는다. 다만 원래 크기는
		// 남긴다 — 크기 자체가 단서인 경우가 있다.
		if (MaxChars > 0 && Out.Len() > MaxChars)
		{
			Out = Out.Left(MaxChars) + FString::Printf(TEXT("... (총 %d자)"), Json.Len());
		}
		return Out;
	}
}
