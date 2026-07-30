#include "Misc/AutomationTest.h"
#include "Connection/HermesErrorPolicy.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesErrorPolicyTest,
	"Hermes.Connection.ErrorPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesErrorPolicyTest::RunTest(const FString& Parameters)
{
	using ER = EHermesErrorReaction;

	// 프로토콜 §5 의 10개 코드 전부. 표의 "Client reaction" 열과 1:1 로 맞춘다.
	struct FCase { const TCHAR* Code; ER Expected; };
	const FCase Cases[] =
	{
		{ TEXT("not_identified"),      ER::ReIdentify           },
		{ TEXT("unknown_type"),        ER::LogOnly              },
		{ TEXT("bad_frame"),           ER::ReconnectWithBackoff },
		{ TEXT("unknown_command"),     ER::LogOnly              },
		{ TEXT("not_authorized"),      ER::DiscardCredentials   },
		{ TEXT("unsupported_version"), ER::StopReconnect        },
		{ TEXT("session_taken_over"),  ER::StopReconnect        },
		{ TEXT("rate_limited"),        ER::FailPendingTurn      },
		{ TEXT("server_busy"),         ER::FailPendingTurn      },
		{ TEXT("internal_error"),      ER::FailPendingTurn      },
	};

	for (const FCase& C : Cases)
	{
		TestTrue(FString::Printf(TEXT("%s maps correctly"), C.Code),
			HermesErrorPolicy::React(C.Code) == C.Expected);
	}

	// 목록에 없는 코드는 LogOnly 로 떨어진다. 종료성으로 취급하면 서버 한 번의
	// 오타가 클라이언트를 영구 정지시킨다.
	{
		TestTrue(TEXT("empty code => LogOnly"),
			HermesErrorPolicy::React(TEXT("")) == ER::LogOnly);
		TestTrue(TEXT("unknown code => LogOnly"),
			HermesErrorPolicy::React(TEXT("made_up_code")) == ER::LogOnly);
	}

	return true;
}
