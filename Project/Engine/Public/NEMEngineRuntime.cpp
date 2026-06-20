#include "NEMEngineRuntime.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Framework/EngineFramework.h>

// c++
#include <memory>

//============================================================================
//	NEMEngine public runtime API implementation
//	エディタのライフサイクルをDLL内に閉じ込め、アプリからは公開ABIだけで起動させる
//============================================================================

int NEM_RunEditor() {

	std::unique_ptr<Engine::Framework> app = std::make_unique<Engine::Framework>();
	app->Run();
	return 0;
}
