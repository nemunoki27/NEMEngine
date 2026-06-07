#include "ManagedScriptRuntime.h"
#include "ManagedScriptUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>

namespace Engine {

	//============================================================================
	//	Log / Time Callbacks
	//	C#側のDebug/Timeクラスから呼び出されるネイティブ実装
	//============================================================================

	void ManagedScriptRuntime::LogCallback(int32_t level, const char* message) {
		// ログレベルの変換。0: Info, 1: Warning, 2: Error
		spdlog::level::level_enum logLevel = spdlog::level::info;
		if (level == 1) {
			logLevel = spdlog::level::warn;
		}
		else if (level == 2) {
			logLevel = spdlog::level::err;
		}
		// エンジン共通のロガーを通じて出力。エディタのコンソール等へ反映される
		Logger::Output(LogType::GameLogic, logLevel, "{}", message ? message : "");
	}

	float ManagedScriptRuntime::GetDeltaTimeCallback() {
		// 直近のライフサイクル呼び出しで保持されたコンテキストからデルタタイム（フレーム間秒数）を取得
		const SystemContext* context = GetInstance().currentContext_;
		return context ? context->deltaTime : 0.0f;
	}

	float ManagedScriptRuntime::GetFixedDeltaTimeCallback() {
		// 固定時間ステップ（物理更新等）の間隔を取得
		const SystemContext* context = GetInstance().currentContext_;
		return context ? context->fixedDeltaTime : 0.0f;
	}

} // Engine
