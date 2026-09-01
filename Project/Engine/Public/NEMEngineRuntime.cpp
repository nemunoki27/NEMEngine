#include "NEMEngineRuntime.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Framework/EngineFramework.h>
#include <Engine/Core/Runtime/Application/GameApplication.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
// windows
#include <Windows.h>

namespace {

	int ReportRuntimeStartupFailure(const char* detail) noexcept {

		const std::string message =
			"NEMEngineの起動に失敗しました\n\n" +
			std::string(detail ? detail : "不明なエラー");
		const std::wstring wideMessage = Engine::Algorithm::ConvertString(message);
		::OutputDebugStringW((wideMessage + L"\n").c_str());
		::MessageBoxW(nullptr, wideMessage.c_str(), L"NEMEngine 起動エラー",
			MB_OK | MB_ICONERROR);
		return EXIT_FAILURE;
	}
}

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

		return ReportRuntimeStartupFailure(exception.what());
	}
	catch (...) {

		return ReportRuntimeStartupFailure("C++例外の詳細を取得できませんでした");
	}
}
