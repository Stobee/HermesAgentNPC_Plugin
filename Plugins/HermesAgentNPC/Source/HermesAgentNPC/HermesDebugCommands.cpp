// 검증용 콘솔 명령. 사람의 키 입력 없이 대화 경로를 돌리기 위한 것이다.
//
// 게임에는 필요 없다. Shipping 빌드에는 아예 컴파일되지 않으므로 배포본에
// 이 표면이 남지 않는다.
//
//   Hermes.Interact [@지연초]          활성 NPC 의 Interact() 를 부른다
//   Hermes.Chat     [@지연초] <텍스트>  발화를 보낸다
//   Hermes.Status                      연결·입력 상태를 로그로 남긴다
//
// @지연초 는 맵 로드 직후 -ExecCmds 로 넣을 때 쓴다. 접속과 NPC 스폰이
// 끝나기 전에 실행되면 아무것도 못 하기 때문이다.
//   -ExecCmds="Hermes.Interact @2; Hermes.Chat @3 안녕"

// UE_BUILD_SHIPPING 은 Misc/Build.h 가 정의한다. 아래 #if 보다 먼저 들여와야
// 한다 — 그러지 않으면 매크로가 아직 보이지 않아, 정의되지 않은 매크로를
// #if 에서 쓰는 것을 오류로 다루는 빌드(플러그인 패키징)에서 컴파일이 깨진다.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "HermesLog.h"
#include "NPC/HermesNPCCharacter.h"
#include "Connection/HermesConnectionSubsystem.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Containers/Ticker.h"

namespace HermesDebugCmd
{
	/** 선두의 `@N` 을 지연 초로 떼어낸다. 없으면 0 을 돌려주고 배열은 그대로다. */
	static float TakeDelay(TArray<FString>& Args)
	{
		if (Args.Num() > 0 && Args[0].StartsWith(TEXT("@")))
		{
			const float Delay = FCString::Atof(*Args[0].RightChop(1));
			Args.RemoveAt(0);
			return FMath::Max(0.f, Delay);
		}
		return 0.f;
	}

	/** Delay 가 0 이면 즉시, 아니면 그만큼 뒤에 한 번 실행한다. */
	static void RunAfter(float Delay, TFunction<void()> Fn)
	{
		if (Delay <= 0.f)
		{
			Fn();
			return;
		}
		FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([Fn](float) { Fn(); return false; }), Delay);
	}

	static AHermesNPCCharacter* FindNpc(UWorld* World)
	{
		if (!World) return nullptr;
		for (TActorIterator<AHermesNPCCharacter> It(World); It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	static UHermesConnectionSubsystem* GetConnection(UWorld* World)
	{
		UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UHermesConnectionSubsystem>() : nullptr;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GHermesInteractCmd(
	TEXT("Hermes.Interact"),
	TEXT("활성 NPC 의 Interact() 를 부른다. 사용: Hermes.Interact [@지연초]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& InArgs, UWorld* World)
		{
			TArray<FString> Args = InArgs;
			const float Delay = HermesDebugCmd::TakeDelay(Args);
			TWeakObjectPtr<UWorld> WeakWorld(World);

			HermesDebugCmd::RunAfter(Delay, [WeakWorld]()
			{
				UWorld* W = WeakWorld.Get();
				AHermesNPCCharacter* Npc = HermesDebugCmd::FindNpc(W);
				if (!Npc)
				{
					UE_LOG(LogHermes, Error, TEXT("Hermes.Interact: 레벨에 NPC 가 없다"));
					return;
				}
				Npc->Interact();

				// 사람이 마우스로 조작할 수 있는 상태가 되었는지가 이 명령의 핵심이다.
				APlayerController* PC = UGameplayStatics::GetPlayerController(W, 0);
				UE_LOG(LogHermes, Display,
					TEXT("Hermes.Interact: 호출 완료. bShowMouseCursor=%s"),
					PC ? (PC->bShowMouseCursor ? TEXT("true") : TEXT("false")) : TEXT("(no PC)"));
			});
		}));

static FAutoConsoleCommandWithWorldAndArgs GHermesChatCmd(
	TEXT("Hermes.Chat"),
	TEXT("발화를 보낸다. 사용: Hermes.Chat [@지연초] <텍스트>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& InArgs, UWorld* World)
		{
			TArray<FString> Args = InArgs;
			const float Delay = HermesDebugCmd::TakeDelay(Args);
			const FString Text = Args.Num() > 0 ? FString::Join(Args, TEXT(" ")) : TEXT("테스트 발화");
			TWeakObjectPtr<UWorld> WeakWorld(World);

			HermesDebugCmd::RunAfter(Delay, [WeakWorld, Text]()
			{
				UHermesConnectionSubsystem* Conn = HermesDebugCmd::GetConnection(WeakWorld.Get());
				if (!Conn)
				{
					UE_LOG(LogHermes, Error, TEXT("Hermes.Chat: 연결 서브시스템이 없다"));
					return;
				}
				Conn->SendChat(Text);
				UE_LOG(LogHermes, Display, TEXT("Hermes.Chat: '%s' 전송 (id=%s)"),
					*Text, *Conn->GetLastSentChatId());
			});
		}));

static FAutoConsoleCommandWithWorldAndArgs GHermesStatusCmd(
	TEXT("Hermes.Status"),
	TEXT("연결·입력 상태를 로그로 남긴다. 사용: Hermes.Status [@지연초]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& InArgs, UWorld* World)
		{
			TArray<FString> Args = InArgs;
			const float Delay = HermesDebugCmd::TakeDelay(Args);
			TWeakObjectPtr<UWorld> WeakWorld(World);

			HermesDebugCmd::RunAfter(Delay, [WeakWorld]()
			{
				UWorld* W = WeakWorld.Get();
				APlayerController* PC = UGameplayStatics::GetPlayerController(W, 0);
				UE_LOG(LogHermes, Display,
					TEXT("Hermes.Status: NPC=%s, 연결서브시스템=%s, bShowMouseCursor=%s"),
					HermesDebugCmd::FindNpc(W) ? TEXT("있음") : TEXT("없음"),
					HermesDebugCmd::GetConnection(W) ? TEXT("있음") : TEXT("없음"),
					PC ? (PC->bShowMouseCursor ? TEXT("true") : TEXT("false")) : TEXT("(no PC)"));
			});
		}));

static FAutoConsoleCommandWithWorldAndArgs GHermesReconnectCmd(
	TEXT("Hermes.Reconnect"),
	TEXT("정지된 재연결 루프를 의도적으로 다시 돌린다. 사용: Hermes.Reconnect [@지연초]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& InArgs, UWorld* World)
		{
			TArray<FString> Args = InArgs;
			const float Delay = HermesDebugCmd::TakeDelay(Args);
			TWeakObjectPtr<UWorld> WeakWorld(World);

			HermesDebugCmd::RunAfter(Delay, [WeakWorld]()
			{
				UHermesConnectionSubsystem* Conn = HermesDebugCmd::GetConnection(WeakWorld.Get());
				if (!Conn)
				{
					UE_LOG(LogHermes, Error, TEXT("Hermes.Reconnect: 연결 서브시스템이 없다"));
					return;
				}
				const bool bResumed = Conn->Reconnect();
				UE_LOG(LogHermes, Display, TEXT("Hermes.Reconnect: 호출 완료 (성공여부=%s)"),
					bResumed ? TEXT("true") : TEXT("false"));
			});
		}));

#endif // !UE_BUILD_SHIPPING
