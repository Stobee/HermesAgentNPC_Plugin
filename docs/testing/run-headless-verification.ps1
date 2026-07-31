<#
.SYNOPSIS
    스텁 서버를 띄우고 게임을 헤드리스로 붙여 프레임 교환을 기록한다.

.DESCRIPTION
    manual-verification-setup.md §4 의 절차 중 "사람의 입력이 필요 없는" 부분을
    자동으로 돌린다. 에디터 PIE 가 아니라 -game 모드이지만, 연결·신원·생존·
    에러 반응 경로는 GameInstance 서브시스템이 담당하므로 동일하게 지난다.

    커버하는 것   : 접속, identify/identified, 자격 증명 저장·재사용, ping/pong,
                    error 반응(재연결·정지·자격 증명 폐기), 재연결 시 재-identify
    커버 못 하는 것: 대화창 UI, chat 송신이 필요한 모든 시나리오(스트리밍·타임아웃·
                    stale_delta·interleaved_turns), move_to 의 실제 이동
                    → 이것들은 사람이 PIE 에서 E 키를 눌러야 한다

.EXAMPLE
    .\docs\testing\run-headless-verification.ps1 -Scenario happy -ResetSave

.EXAMPLE
    .\docs\testing\run-headless-verification.ps1 -Scenario session_taken_over -Seconds 70
#>
param(
    # hermes_stub_server.py --list 로 목록을 볼 수 있다.
    [string]$Scenario = "happy",
    # 게임을 몇 초 띄워 둘지. 사망 판정(PeerTimeoutSeconds, 기본 60초)을 보려면 넉넉히.
    [int]$Seconds = 60,
    # 신규 신원 발급 경로를 보려면 SaveGame 을 지운다.
    [switch]$ResetSave,
    # 맵 로드 직후 실행할 콘솔 명령. 대화가 필요한 시나리오를 사람 없이 돌린다.
    # 접속·NPC 스폰 전에 실행되지 않도록 @지연초 를 붙인다.
    #   -Exec "Hermes.Interact @3; Hermes.Chat @4 안녕하세요"
    [string]$Exec,
    [string]$Engine = "C:\Program Files\Epic Games\UE_5.8\Engine",
    [string]$OutDir
)

$ErrorActionPreference = "Stop"

$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$proj = Join-Path $repo "HermesAgentNPC.uproject"
$ue   = Join-Path $Engine "Binaries\Win64\UnrealEditor-Cmd.exe"
$stubPy = Join-Path $PSScriptRoot "hermes_stub_server.py"

if (-not $OutDir) { $OutDir = Join-Path $repo "Saved\HeadlessVerification" }
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Force $OutDir | Out-Null }

if (-not (Test-Path $ue)) { throw "엔진을 찾을 수 없다: $ue  (-Engine 으로 경로를 지정한다)" }

$stubLog = Join-Path $OutDir "stub-$Scenario.log"
$ueLog   = Join-Path $OutDir "ue-$Scenario.log"

if ($ResetSave) {
    Remove-Item (Join-Path $repo "Saved\SaveGames\HermesPlayer.sav") -ErrorAction SilentlyContinue
    Write-Host "[harness] savegame reset"
}

$stub = Start-Process -FilePath "py" -ArgumentList @($stubPy, "--scenario", $Scenario) `
        -RedirectStandardOutput $stubLog -RedirectStandardError "$stubLog.err" `
        -NoNewWindow -PassThru
Start-Sleep -Milliseconds 800
Write-Host "[harness] stub pid=$($stub.Id) scenario=$Scenario"

# Host/Port 는 ini 를 건드리지 않고 커맨드라인으로 덮는다(Task 1).
$ueArgs = @(
    "`"$proj`"", "/Game/Hermes/Maps/L_HermesTest",
    "-game", "-unattended", "-nullrhi", "-nosound", "-nosplash",
    "-HermesHost=127.0.0.1", "-HermesPort=8770",
    "-LogCmds=`"LogHermes Verbose`"", "-abslog=`"$ueLog`""
)
if ($Exec) { $ueArgs += "-ExecCmds=`"$Exec`"" }
$game = Start-Process -FilePath $ue -ArgumentList $ueArgs -NoNewWindow -PassThru
Write-Host "[harness] game pid=$($game.Id), running for $Seconds s"

$deadline = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $deadline -and -not $game.HasExited) { Start-Sleep -Seconds 2 }

if (-not $game.HasExited) {
    Stop-Process -Id $game.Id -Force -ErrorAction SilentlyContinue
    Write-Host "[harness] game stopped after $Seconds s"
} else {
    Write-Host "[harness] game exited on its own, code=$($game.ExitCode)"
}
Start-Sleep -Seconds 2
if (-not $stub.HasExited) { Stop-Process -Id $stub.Id -Force -ErrorAction SilentlyContinue }

$stubLines = @(Get-Content $stubLog -ErrorAction SilentlyContinue)

Write-Host ""
Write-Host "=============== 요약 ==============="
$connections = @($stubLines | Select-String -Pattern "\[\+\] connected").Count
$identifies  = @($stubLines | Select-String -Pattern '"type": "identify"').Count
Write-Host ("연결 수      : {0}" -f $connections)
Write-Host ("identify 수  : {0}" -f $identifies)
if ($connections -gt 0 -and $identifies -lt $connections - 1) {
    # 마지막 연결은 게임을 강제 종료하며 잘릴 수 있으므로 1 개까지는 허용한다.
    Write-Warning "identify 없는 연결이 있다. 새 연결마다 신원을 밝혀야 한다 -- HermesConnectionEdge 회귀를 의심할 것."
}

Write-Host ""
Write-Host "=============== 스텁 프레임 로그 ==============="
if ($stubLines.Count -gt 0) { $stubLines } else { Write-Host "(비어 있다)" }

Write-Host ""
Write-Host "=============== UE LogHermes ==============="
if (Test-Path $ueLog) {
    Select-String -Path $ueLog -Pattern "LogHermes:" | ForEach-Object { $_.Line }
} else {
    Write-Host "(로그 없음)"
}
Write-Host ""
Write-Host "[harness] 전체 로그: $OutDir"
