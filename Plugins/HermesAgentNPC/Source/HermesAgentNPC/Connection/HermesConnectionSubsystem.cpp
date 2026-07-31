#include "Connection/HermesConnectionSubsystem.h"
#include "Connection/HermesUtil.h"
#include "Connection/HermesLiveness.h"
#include "Connection/HermesConnectionEdge.h"
#include "Connection/HermesTrace.h"
#include "Connection/HermesErrorPolicy.h"
#include "HAL/PlatformTime.h"
#include "Transport/HermesSocketWorker.h"
#include "Protocol/HermesMessages.h"
#include "Actions/HermesActionDispatcher.h"
#include "Actions/MoveToActionHandler.h"
#include "Actions/FollowPlayerActionHandler.h"
#include "Actions/InventoryActionHandler.h"
#include "Actions/ItemTransferActionHandler.h"
#include "HermesSaveGame.h"
#include "HermesLog.h"
#include "Transport/HermesTlsPolicy.h"
#include "Misc/Paths.h"
#include "Kismet/GameplayStatics.h"
#include "Dom/JsonObject.h"
#include "Settings/HermesSettings.h"
#include "Transport/HermesWorkerConfig.h"

void UHermesConnectionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UHermesSettings* Settings = GetDefault<UHermesSettings>();

	LoadCredentials();

	// PeerTimeout 이 PingInterval 에 비해 너무 짧으면 정상 연결이 죽은 것으로 오판된다.
	// 값을 강제로 교정하지는 않는다 — 설정자의 의도를 덮어쓰기보다 문제를 드러낸다.
	if (Settings->PeerTimeoutSeconds < Settings->KeepAlivePingIntervalSeconds * 2.f)
	{
		UE_LOG(LogHermes, Warning,
			TEXT("PeerTimeoutSeconds (%.1f) should be at least 2x "
			     "KeepAlivePingIntervalSeconds (%.1f); false disconnects are likely"),
			Settings->PeerTimeoutSeconds, Settings->KeepAlivePingIntervalSeconds);
	}

	Dispatcher = NewObject<UHermesActionDispatcher>(this);

	// 워커는 전용 스레드에서 돌므로 UObject를 넘기지 않고 값만 복사해 전달한다.
	FHermesWorkerConfig Cfg;
	Settings->GetResolvedEndpoint(Cfg.Host, Cfg.Port);
	Cfg.InitialReconnectDelay = Settings->InitialReconnectDelay;
	Cfg.MaxReconnectDelay     = Settings->MaxReconnectDelay;
	Cfg.HealthyConnectionSeconds = Settings->HealthyConnectionSeconds;
	Cfg.MaxInboundQueueSize   = Settings->MaxInboundQueueSize;
	Cfg.MaxOutboundQueueSize  = Settings->MaxOutboundQueueSize;

	// Shipping 에서는 설정이 false 여도 TLS 를 강제한다. 배포된 게임이
	// 설정 실수로 평문 통신하는 상황을 만들 수 없게 한다.
#if UE_BUILD_SHIPPING
	constexpr bool bShipping = true;
#else
	constexpr bool bShipping = false;
#endif
	const bool bRequestedTls = Settings->GetResolvedUseTLS();   // ini + 커맨드라인
	Cfg.Tls.bUseTLS = HermesTls::ResolveUseTls(bRequestedTls, bShipping);
	if (bRequestedTls != Cfg.Tls.bUseTLS)
	{
		UE_LOG(LogHermes, Error,
			TEXT("bUseTLS=false is ignored in Shipping builds; TLS is enforced"));
	}

	Cfg.Tls.ServerName              = Settings->TlsServerName;
	Cfg.Tls.PinnedPublicKeyHashes   = Settings->TlsPinnedPublicKeyHashes;
	Cfg.Tls.HandshakeTimeoutSeconds = Settings->TlsHandshakeTimeoutSeconds;

	// 워커 스레드가 경로 API 를 쓰지 않도록 게임 스레드에서 절대 경로로 바꾼다.
	if (!Settings->TlsPrivateCaPath.IsEmpty())
	{
		Cfg.Tls.PrivateCaPath =
			FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Settings->TlsPrivateCaPath);
	}

	Worker = MakeUnique<FHermesSocketWorker>(Cfg);
	Worker->Start();

	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UHermesConnectionSubsystem::Tick), 0.f);
}

void UHermesConnectionSubsystem::Deinitialize()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	}
	if (Worker)
	{
		Worker->RequestStop();
		Worker.Reset(); // 소멸자에서 스레드 Stop/Kill 처리
	}

	// Worker 가 사라지면 재연결 루프 자체가 없다 — 재개할 대상도, 정지시킬
	// 대상도 없다. 여기서 지우지 않으면 (예: 블루프린트 위젯이 소멸 후에도
	// 낡은 서브시스템 참조를 들고 질의하는 경우) IsReconnectSuspended() 가
	// "정지 중"을 영원히 답하게 된다. Worker 유무와 무관하게 항상 일관된
	// "정지 아님" 을 보장하려고 여기서 지운다.
	SuspendState.Reset();

	Super::Deinitialize();
}

void UHermesConnectionSubsystem::LoadCredentials()
{
	const FString SaveSlot = GetDefault<UHermesSettings>()->SaveSlotName;

	// 신원은 서버가 발급한다. 여기서 만들어내지 않는다 — 클라이언트가 자기
	// 신원을 주장하는 구조면 임의 UUID 로 남의 세션에 접근할 수 있다.
	if (UHermesSaveGame* SG = Cast<UHermesSaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlot, 0)))
	{
		PlayerId     = SG->PlayerId;
		SessionToken = SG->GetSessionToken();   // 난독화 해제

		// 토큰만 손상되었다면 반쪽 자격 증명으로 접속을 시도하지 않는다.
		// MakeIdentify 가 둘 다 있어야 실어 보내므로 자연히 재발급 경로가 된다.
		if (PlayerId.IsEmpty() || SessionToken.IsEmpty())
		{
			PlayerId.Reset();
			SessionToken.Reset();
		}
	}
}

void UHermesConnectionSubsystem::SaveCredentials()
{
	const FString SaveSlot = GetDefault<UHermesSettings>()->SaveSlotName;

	UHermesSaveGame* SG = Cast<UHermesSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UHermesSaveGame::StaticClass()));
	if (!SG)
	{
		UE_LOG(LogHermes, Warning, TEXT("failed to create save game object"));
		return;
	}
	SG->PlayerId = PlayerId;
	SG->SetSessionToken(SessionToken);   // 난독화해 보관

	if (!UGameplayStatics::SaveGameToSlot(SG, SaveSlot, 0))
	{
		// 다음 실행에서 새 신원을 발급받게 된다. 데이터 손실이지 보안 결함은 아니다.
		UE_LOG(LogHermes, Warning, TEXT("failed to save credentials to slot '%s'"), *SaveSlot);
	}
}

void UHermesConnectionSubsystem::RegisterNpc(AHermesNPCCharacter* Npc)
{
	if (!Npc || !Dispatcher) return;

	if (ActiveNpc.IsValid() && ActiveNpc.Get() != Npc)
	{
		// 레벨에 NPC 를 둘 이상 배치했을 때 조용히 이상해지는 것을 막는다.
		// 어느 쪽이 대상인지 로그로 분명히 남긴다.
		UE_LOG(LogHermes, Warning,
			TEXT("활성 NPC 교체: %s -> %s. 이 플러그인은 NPC 한 명만 대상으로 한다."),
			*ActiveNpc->GetName(), *Npc->GetName());
	}

	// 옛 핸들러를 남기면 Dispatch 의 first-match 규칙 때문에 액션이 계속
	// 옛 NPC 로 가고 새 NPC 는 아무것도 받지 못한다.
	Dispatcher->ResetHandlers();

	UMoveToActionHandler* H1 = NewObject<UMoveToActionHandler>(this); H1->Init(Npc); Dispatcher->RegisterHandler(H1);
	UFollowPlayerActionHandler* H2 = NewObject<UFollowPlayerActionHandler>(this); H2->Init(Npc); Dispatcher->RegisterHandler(H2);
	UInventoryActionHandler* H3 = NewObject<UInventoryActionHandler>(this); H3->Init(Npc); Dispatcher->RegisterHandler(H3);
	UItemTransferActionHandler* H4 = NewObject<UItemTransferActionHandler>(this); H4->Init(Npc); Dispatcher->RegisterHandler(H4);

	ActiveNpc = Npc;
}

void UHermesConnectionSubsystem::UnregisterNpc(AHermesNPCCharacter* Npc)
{
	// 활성이 아닌 NPC 가 사라지는 것은 현재 배선과 무관하다. 여기서 무조건
	// 비우면 다른 NPC 의 EndPlay 가 활성 NPC 의 배선을 끊어버린다.
	if (!Npc || ActiveNpc.Get() != Npc) return;

	if (Dispatcher) Dispatcher->ResetHandlers();
	ActiveNpc.Reset();
}

void UHermesConnectionSubsystem::SendJson(const FString& Json)
{
	if (Worker)
	{
		// 실제 서버에는 프레임을 찍어 주는 스텁 콘솔이 없다. 어긋났을 때
		// 무엇을 보냈는지 여기서만 알 수 있다.
		UE_LOG(LogHermes, Verbose, TEXT(">> %s"), *HermesTrace::FormatFrame(Json));
		Worker->EnqueueOutbound(Json);
		LastSendTime = FPlatformTime::Seconds();
	}
}

void UHermesConnectionSubsystem::SendIdentify()
{
	// 자격 증명이 없으면 MakeIdentify 가 신규 발급 요청 형태로 만든다.
	SendJson(HermesJson::MakeIdentify(PlayerId, SessionToken, FString()));
}

void UHermesConnectionSubsystem::SendChat(const FString& Text)
{
	const FString Id = FString::Printf(TEXT("c-%04d"), ++ChatCounter);
	const FString Json = HermesJson::MakeChat(Id, Text);

	// identified 이전이라 보류되더라도 추적은 시작한다. 서버에 닿지 못한
	// 발화도 타임아웃으로 사용자에게 알려야 "생각 중..." 이 남지 않는다.
	InFlightChats.Add(Id, FPlatformTime::Seconds());
	LastSentChatId = Id;

	if (bIdentified)
	{
		SendJson(Json);
		return;
	}

	// identified 를 영영 보내지 않는 피어에 붙으면 여기가 무한히 자란다.
	const int32 Dropped = HermesUtil::PushBounded(
		PendingChats, Json, GetDefault<UHermesSettings>()->MaxPendingChats);
	if (Dropped > 0)
	{
		UE_LOG(LogHermes, Warning, TEXT("pending chat overflow, dropped %d oldest"), Dropped);
	}
}

void UHermesConnectionSubsystem::SendActionEvent(const FString& Id, bool bCompleted,
	const TSharedPtr<FJsonObject>& Result, const FString& Error)
{
	SendJson(HermesJson::MakeActionEvent(Id, bCompleted, Result, Error));
}

void UHermesConnectionSubsystem::FlushPendingChats()
{
	for (const FString& J : PendingChats) SendJson(J);
	PendingChats.Reset();
}

void UHermesConnectionSubsystem::HandleConnectionLost(double NowSeconds)
{
	bIdentified = false;

	// 이 연결이 얼마나 살았는지가 사다리를 되돌릴지 가른다.
	SuspendState.NoteConnectionClosed(
		NowSeconds - ConnectionOpenedAt,
		GetDefault<UHermesSettings>()->HealthyConnectionSeconds);

	// 진행 중이던 발화를 모두 실패 처리한다. 재연결 후 서버가 이전 발화를
	// 이어서 응답하더라도 그 id 는 추적 대상이 아니므로 위젯의 상관 규칙이 무시한다.
	TArray<FString> Abandoned;
	InFlightChats.CollectTimedOut(NowSeconds, 0.f, Abandoned);
	for (const FString& Id : Abandoned)
	{
		OnChatFailed.Broadcast(Id, TEXT("disconnected"));
	}
	InFlightChats.Clear();

	OnConnectionStateChanged.Broadcast(false);
}

void UHermesConnectionSubsystem::HandleConnectionEstablished(double NowSeconds)
{
	bIdentified  = false;
	LastRecvTime = NowSeconds;   // 초기화하지 않으면 연결 직후 즉시 사망 판정이 난다
	LastSendTime = NowSeconds;
	ConnectionOpenedAt = NowSeconds;
	SuspendState.NoteConnectionOpened();
	SendIdentify();
}

bool UHermesConnectionSubsystem::Tick(float DeltaTime)
{
	if (!Worker) return true;

	// 연결 전이 감지. 불린만 보면 두 틱 사이에 끝난 재연결을 놓치므로
	// 워커의 연결 세대를 함께 본다. 판정은 HermesConnectionEdge 가 한다.
	const bool bNow = Worker->IsConnected();
	const uint32 CurGen = Worker->GetConnectionGeneration();
	const double NowSeconds = FPlatformTime::Seconds();

	using HermesConnectionEdge::EDecision;
	const EDecision Edge = HermesConnectionEdge::Evaluate(
		bWasConnected, SeenConnectionGeneration, bNow, CurGen);

	bWasConnected = bNow;
	SeenConnectionGeneration = CurGen;

	// 재연결은 끊김 정리와 새 연결 처리를 모두 해야 한다. 순서가 중요하다 —
	// 정리를 뒤에 하면 방금 보낸 identify 를 스스로 지운다.
	if (Edge == EDecision::Closed || Edge == EDecision::Reopened)
	{
		HandleConnectionLost(NowSeconds);
	}
	if (Edge == EDecision::Opened || Edge == EDecision::Reopened)
	{
		HandleConnectionEstablished(NowSeconds);
	}

	// 한 틱이 무한정 길어지지 않게 예산을 둔다. 남은 프레임은 다음 틱에서
	// 처리되고, 유입이 예산을 계속 초과하면 워커의 큐 상한이 연결을 끊는다.
	int32 Budget = GetDefault<UHermesSettings>()->MaxInboundFramesPerTick;
	FString Json;
	while (Budget-- > 0 && Worker->DequeueInbound(Json))
	{
		UE_LOG(LogHermes, Verbose, TEXT("<< %s"), *HermesTrace::FormatFrame(Json));

		TSharedPtr<FJsonObject> Obj;
		if (HermesJson::Parse(Json, Obj))
		{
			HandleFrame(Obj);
		}
		else
		{
			// 파싱 실패는 조용히 넘어가면 안 된다. 실서버 연동에서 가장 먼저
			// 의심할 지점이고, 위의 트레이스와 짝을 이뤄야 원인이 보인다.
			UE_LOG(LogHermes, Warning, TEXT("failed to parse inbound frame"));
		}
	}

	// 연결이 성립한 동안에만 평가한다. 재연결 대기 중에는 수신이 없는 것이 정상이다.
	if (bNow)
	{
		const UHermesSettings* Settings = GetDefault<UHermesSettings>();
		const HermesLiveness::EDecision D = HermesLiveness::Evaluate(
			NowSeconds, LastRecvTime, LastSendTime,
			Settings->KeepAlivePingIntervalSeconds, Settings->PeerTimeoutSeconds);

		if (D == HermesLiveness::EDecision::SendPing)
		{
			SendJson(HermesJson::MakePing(FString::Printf(TEXT("k-%04d"), ++PingCounter)));
		}
		else if (D == HermesLiveness::EDecision::DeclareDead)
		{
			// 조용히 끊긴 연결은 다음 송신 시점까지 드러나지 않는다. 송신은 플레이어가
			// 말을 걸 때 일어나므로, 그때 재연결이 시작되면 가장 나쁜 타이밍이 된다.
			UE_LOG(LogHermes, Warning,
				TEXT("peer silent for %.0fs, treating connection as dead"),
				Settings->PeerTimeoutSeconds);
			Worker->RequestReconnect();
		}
	}

	// 응답 없는 발화를 만료시킨다. 델타가 오는 동안에는 Touch 로 갱신되므로
	// 긴 생성은 살아남고 멈춘 생성만 잡힌다. 연결 여부와 무관하게 평가한다 —
	// 연결이 끊긴 동안에도 발화는 만료되어야 한다.
	{
		TArray<FString> TimedOut;
		InFlightChats.CollectTimedOut(NowSeconds,
			GetDefault<UHermesSettings>()->ChatResponseTimeoutSeconds, TimedOut);
		for (const FString& Id : TimedOut)
		{
			UE_LOG(LogHermes, Warning, TEXT("chat %s timed out"), *Id);
			OnChatFailed.Broadcast(Id, TEXT("timeout"));
		}
	}
	return true; // 계속 틱
}

void UHermesConnectionSubsystem::HandleFrame(const TSharedPtr<FJsonObject>& Obj)
{
	// 종류를 가리지 않고 모든 수신이 생존 신호다.
	LastRecvTime = FPlatformTime::Seconds();

	FString Type;
	if (!Obj->TryGetStringField(TEXT("type"), Type)) return;

	if (Type == HermesMsg::Identified)
	{
		FString NewPid, NewTok, ChatId;
		if (!HermesJson::ParseIdentified(Obj, NewPid, NewTok, ChatId))
		{
			// v1 호환 모드를 두지 않는다. 호환 모드는 곧 "인증 없이도 동작하는
			// 경로"라 신원 발급의 목적을 무력화한다. 조용히 동작하느니 크게 실패한다.
			UE_LOG(LogHermes, Error,
				TEXT("server did not return session credentials. "
				     "This client requires protocol v2. Closing connection."));
			if (Worker)
			{
				Worker->RequestReconnect();
			}
			return;
		}

		// 최초 발급이거나 서버가 값을 바꿔 준 경우에만 저장한다.
		if (NewPid != PlayerId || NewTok != SessionToken)
		{
			PlayerId     = NewPid;
			SessionToken = NewTok;
			SaveCredentials();
		}

		bIdentified = true;
		FlushPendingChats();
		OnConnectionStateChanged.Broadcast(true);
	}
	else if (Type == HermesMsg::ChatDelta)
	{
		// seq 는 읽지 않는다. 델타는 표시용이고 최종 텍스트가 정본이라 순번 검증이
		// 없어도 화면이 자기 교정된다. 순번을 쓰기 시작하면 유실·재정렬 처리 로직이
		// 따라붙는데 그만한 이득이 없다.
		FString Text, Id;
		Obj->TryGetStringField(TEXT("text"), Text);
		Obj->TryGetStringField(TEXT("id"), Id);
		// 진행 신호. 이것이 긴 생성을 타임아웃에서 살려준다.
		InFlightChats.Touch(Id, FPlatformTime::Seconds());
		OnChatDelta.Broadcast(Text, Id);
	}
	else if (Type == HermesMsg::ChatResponse)
	{
		FString Text, Id;
		Obj->TryGetStringField(TEXT("text"), Text);
		Obj->TryGetStringField(TEXT("id"), Id);
		InFlightChats.Remove(Id);
		OnChatResponse.Broadcast(Text, Id);
	}
	else if (Type == HermesMsg::ActionRequest)
	{
		FHermesActionPayload P;
		Obj->TryGetStringField(TEXT("id"), P.Id);
		Obj->TryGetStringField(TEXT("command"), P.Command);
		const TSharedPtr<FJsonObject>* ParamsObj = nullptr;
		if (Obj->TryGetObjectField(TEXT("params"), ParamsObj)) P.Params = *ParamsObj;
		else P.Params = MakeShared<FJsonObject>();

		Dispatcher->Dispatch(P, [this](const FString& ResultJson){ SendJson(ResultJson); });
	}
	else if (Type == HermesMsg::Ping)
	{
		FString Id; Obj->TryGetStringField(TEXT("id"), Id);
		SendJson(HermesJson::MakePong(Id));
	}
	else if (Type == HermesMsg::Error)
	{
		FString Code, Msg, ErrId;
		Obj->TryGetStringField(TEXT("code"), Code);
		Obj->TryGetStringField(TEXT("message"), Msg);
		Obj->TryGetStringField(TEXT("id"), ErrId);
		ApplyErrorReaction(HermesErrorPolicy::React(Code), Code, Msg, ErrId);
	}
}

void UHermesConnectionSubsystem::ApplyErrorReaction(EHermesErrorReaction Reaction,
	const FString& Code, const FString& Message, const FString& Id)
{
	switch (Reaction)
	{
	case EHermesErrorReaction::LogOnly:
		UE_LOG(LogHermes, Warning, TEXT("server error %s: %s"), *Code, *Message);
		break;

	case EHermesErrorReaction::ReIdentify:
		// 연결은 살아 있다. 다시 신원을 밝히면 세션이 이어진다.
		UE_LOG(LogHermes, Warning,
			TEXT("server error %s: %s -- re-identifying"), *Code, *Message);
		bIdentified = false;
		SendIdentify();
		break;

	case EHermesErrorReaction::DiscardCredentials:
		// 서버가 이 자격 증명을 거부했다. 들고 있으면 재접속마다 같은 거부를
		// 반복하므로 버리고 신규 발급을 요청한다. 지운 상태를 저장까지 해야
		// 프로세스를 재시작해도 같은 실패로 돌아가지 않는다.
		UE_LOG(LogHermes, Warning,
			TEXT("server rejected stored credentials (%s: %s); "
			     "discarding them and asking for a new identity"), *Code, *Message);
		PlayerId.Reset();
		SessionToken.Reset();
		SaveCredentials();
		bIdentified = false;
		if (Worker)
		{
			Worker->RequestReconnect();
		}
		break;

	case EHermesErrorReaction::ReconnectWithBackoff:
		UE_LOG(LogHermes, Warning,
			TEXT("server error %s: %s -- reconnecting"), *Code, *Message);
		if (Worker)
		{
			Worker->RequestReconnect();
		}
		break;

	case EHermesErrorReaction::StopReconnect:
		// 조용히 멈추면 원인 추적이 불가능하다. 사유와 재개 방법을 같이 남긴다.
		UE_LOG(LogHermes, Error,
			TEXT("fatal server error %s: %s -- stopping the reconnect loop. "
			     "Retrying does not heal this; call Reconnect() to attempt it deliberately."),
			*Code, *Message);
		if (Worker)
		{
			Worker->SuspendReconnect();
		}
		SuspendState.NoteSuspended(FPlatformTime::Seconds());
		// 게임이 재접속 UI 를 띄울 시점이다. 이것이 없으면 게임은 정지 사실을
		// 알 방법이 없어 버튼을 언제 보여야 할지 모른다.
		OnReconnectSuspended.Broadcast(Message);
		break;

	case EHermesErrorReaction::FailPendingTurn:
		UE_LOG(LogHermes, Warning,
			TEXT("server error %s: %s (id '%s')"), *Code, *Message, *Id);

		// id 가 없는 에러는 어떤 턴도 실패시키지 않는다 — 어느 턴인지 모르는 채로
		// 아무거나 실패시키면 무관한 대화가 끊긴다. 그 경우 해당 턴은 평소대로
		// ChatResponseTimeoutSeconds 로 만료된다.
		//
		// id 가 있으면 60초를 기다릴 이유가 없다. 즉시 실패시켜 UI 가
		// "생각 중..." 에 머무는 시간을 없앤다.
		if (!Id.IsEmpty() && InFlightChats.FailById(Id))
		{
			OnChatFailed.Broadcast(Id, Code);
		}
		break;
	}
}

bool UHermesConnectionSubsystem::Reconnect()
{
	if (!Worker)
	{
		return false;
	}

	const UHermesSettings* Settings = GetDefault<UHermesSettings>();
	const double Now = FPlatformTime::Seconds();

	if (!SuspendState.IsSuspended())
	{
		UE_LOG(LogHermes, Verbose,
			TEXT("Reconnect() ignored: the reconnect loop is not suspended"));
		return false;
	}

	if (!SuspendState.TryResume(Now, Settings->ReconnectCooldownSeconds,
	                            Settings->MaxReconnectCooldownSeconds))
	{
		// 거부를 조용히 하면 게임은 버튼이 먹통이 된 것으로 보인다.
		UE_LOG(LogHermes, Warning,
			TEXT("Reconnect() refused: %.1fs of cooldown remaining"),
			GetReconnectCooldownRemaining());
		return false;
	}

	UE_LOG(LogHermes, Log, TEXT("deliberate reconnect requested by game code"));
	Worker->ResumeReconnect();
	return true;
}

bool UHermesConnectionSubsystem::IsReconnectSuspended() const
{
	return SuspendState.IsSuspended();
}

float UHermesConnectionSubsystem::GetReconnectCooldownRemaining() const
{
	const UHermesSettings* Settings = GetDefault<UHermesSettings>();
	return SuspendState.CooldownRemaining(
		FPlatformTime::Seconds(),
		Settings->ReconnectCooldownSeconds,
		Settings->MaxReconnectCooldownSeconds);
}
