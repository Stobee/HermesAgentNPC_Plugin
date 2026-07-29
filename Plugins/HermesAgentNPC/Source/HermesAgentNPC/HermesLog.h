#pragma once
#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

/**
 * 이 플러그인의 모든 로그가 쓰는 카테고리.
 *
 * LogTemp 를 쓰면 도입 프로젝트가 Hermes 로그만 걸러내거나 verbosity 를
 * 조절할 수 없다. 전용 카테고리를 두면 콘솔에서 다음이 가능해진다:
 *
 *     log LogHermes Verbose
 *     log LogHermes off
 *
 * 카테고리 이름이 이미 출처를 밝히므로 메시지에 "[Hermes]" 접두사를 붙이지 않는다.
 */
DECLARE_LOG_CATEGORY_EXTERN(LogHermes, Log, All);
