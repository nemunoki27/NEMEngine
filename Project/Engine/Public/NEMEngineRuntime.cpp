#include "NEMEngineRuntime.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Framework/EngineFramework.h>
#include <Engine/Core/Runtime/Application/GameApplication.h>
#if defined(_DEBUG) || defined(_DEVELOPBUILD)
#include <Engine/Editor/Runtime/Application/EngineApplication.h>
#endif

// c++
#include <memory>

//============================================================================
//	NEMEngine public runtime API implementation
//	エディタのライフサイクルをDLL内に閉じ込め、アプリからは公開ABIだけで起動させる
//============================================================================

int NEM_RunEditor() {

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	return Engine::RunEditorApplication();
#else
	return NEM_RunGame();
#endif
}

int NEM_RunGame() {

	Engine::Framework framework(std::make_unique<Engine::GameApplication>());
	framework.Run();
	return 0;
}
