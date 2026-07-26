#include "NEMEngineRuntime.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Framework/EngineFramework.h>
#include <Engine/Core/Runtime/Application/GameApplication.h>

// c++
#include <memory>

//============================================================================
//	NEMEngine public runtime API implementation
//	ゲームランタイムのライフサイクルをDLL内に閉じ込める
//============================================================================

int NEM_RunGame() {

	Engine::Framework framework(std::make_unique<Engine::GameApplication>());
	framework.Run();
	return 0;
}
