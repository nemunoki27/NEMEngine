#include "ManagedScriptRuntime.h"
#include "ManagedScriptUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

#include <cmath>

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
		const SystemContext* context = GetCurrentContext();
		return context ? context->deltaTime : 0.0f;
	}

	float ManagedScriptRuntime::GetFixedDeltaTimeCallback() {
		// 固定時間ステップ（物理更新等）の間隔を取得
		const SystemContext* context = GetCurrentContext();
		return context ? context->fixedDeltaTime : 0.0f;
	}

	//============================================================================
	//	Time 拡張 / TimeScale Callbacks
	//	scaled な deltaTime/fixedDeltaTime は上の既存 callback が返す。ここでは unscaled 系と累積値を返す。
	//============================================================================
	float ManagedScriptRuntime::GetUnscaledDeltaTimeCallback() {
		return unscaledDeltaTime_;
	}

	float ManagedScriptRuntime::GetUnscaledFixedDeltaTimeCallback() {
		// 固定ステップ間隔は time scale に依らず一定。scale の影響は substep 回数側に出る
		return fixedDeltaTime_;
	}

	double ManagedScriptRuntime::GetTimeSinceStartupCallback() {
		return timeSinceStartup_;
	}

	double ManagedScriptRuntime::GetUnscaledTimeCallback() {
		return unscaledTime_;
	}

	float ManagedScriptRuntime::GetTimeScaleCallback() {
		return timeScale_;
	}

	void ManagedScriptRuntime::SetTimeScaleCallback(float value) {
		// service が time scale の authority。authoring の TimeScaleComponent へは書き戻さない
		timeScale_ = (!std::isfinite(value)) ? 1.0f : (value < 0.0f ? 0.0f : value);
	}

	uint64_t ManagedScriptRuntime::GetFrameCountCallback() {
		return frameCount_;
	}

	//============================================================================
	//	AssetRef runtime resolve Callbacks
	//	UUID 主体。native resource / GPU / filesystem path は返さない（表示名は stem のみ）。
	//============================================================================
	int32_t ManagedScriptRuntime::AssetExistsCallback(uint64_t assetId) {

		const SystemContext* context = GetCurrentContext();
		if (!context || !context->assetDatabase || assetId == 0) {
			return 0;
		}
		return context->assetDatabase->Find(AssetID{ assetId }) != nullptr ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::CopyAssetDisplayNameCallback(uint64_t assetId, char* buffer, int32_t capacity) {

		const SystemContext* context = GetCurrentContext();
		std::string name;
		if (context && context->assetDatabase && assetId != 0 && context->assetDatabase->Find(AssetID{ assetId })) {
			// 表示名は asset path の stem（拡張子なしファイル名）。path 自体は C# へ渡さない
			name = context->assetDatabase->ResolveFullPath(AssetID{ assetId }).stem().string();
		}
		return CopyStringToBuffer(name, buffer, capacity);
	}

	int32_t ManagedScriptRuntime::CopyProjectRootCallback(char* buffer, int32_t capacity) {

		// InputActions.json 等の ProjectSettings 解決用。GameAssets が属する game root を UTF-8 で返す
		// （ProjectSettings/ は GameAssets/ と同じ階層に置く運用）。
		const std::string root = RuntimePaths::GetGameRoot().string();
		return CopyStringToBuffer(root, buffer, capacity);
	}

} // Engine
