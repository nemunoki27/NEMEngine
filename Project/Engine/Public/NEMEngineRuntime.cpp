#include "NEMEngineRuntime.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Framework/EngineFramework.h>
#include <Engine/Core/Runtime/Application/GameApplication.h>

// c++
#include <cstdlib>
#include <exception>
#include <memory>

//============================================================================
//	NEMEngine public runtime API implementation
//	ゲームランタイムのライフサイクルをDLL内に閉じ込める
//============================================================================

int NEM_RunGame() {

	try {

		Engine::Framework framework(std::make_unique<Engine::GameApplication>());
		framework.Run();
		return EXIT_SUCCESS;
	}
	catch (const std::exception& exception) {

		return Engine::Framework::ReportFailure(exception.what());
	}
	catch (...) {

		return Engine::Framework::ReportFailure("C++例外の詳細を取得できませんでした");
	}
}
