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
#include <Engine/Core/Scripting/Managed/Diagnostics/ManagedScriptExceptionStore.h>

#include <cmath>

namespace Engine {

	//============================================================================
	//	Log / Time Callbacks
	//	C#側のDebug/Timeクラスから呼び出されるネイティブ実装
	//============================================================================

	void ManagedScriptRuntime::ReportScriptExceptionCallback(const char* jsonUtf8) {
		// C#境界のGuardInstanceで捕捉した未処理例外の構造化DTOをbounded storeへ渡す、ConsoleログはC#側が出すためここではstoreへの追加のみ行う
		ManagedScriptExceptionStore::GetInstance().ReportJson(jsonUtf8);
	}

	void ManagedScriptRuntime::LogCallback(int32_t level, const char* message) {
		// ログレベルの変換、0はInfo 1はWarning 2はError
		spdlog::level::level_enum logLevel = spdlog::level::info;
		if (level == 1) {
			logLevel = spdlog::level::warn;
		}
		else if (level == 2) {
			logLevel = spdlog::level::err;
		}
		// エンジン共通のロガーを通じて出力し、エディタのコンソール等へ反映される
		Logger::Output(LogType::GameLogic, logLevel, "{}", message ? message : "");
	}

	float ManagedScriptRuntime::GetDeltaTimeCallback() {
		// 直近のライフサイクル呼び出しで保持されたコンテキストからデルタタイムつまりフレーム間秒数を取得
		const SystemContext* context = GetCurrentContext();
		return context ? context->deltaTime : 0.0f;
	}

	float ManagedScriptRuntime::GetFixedDeltaTimeCallback() {
		// 固定時間ステップつまり物理更新等の間隔を取得
		const SystemContext* context = GetCurrentContext();
		return context ? context->fixedDeltaTime : 0.0f;
	}

	//============================================================================
	//	Time拡張/ TimeScale Callbacks
	//	scaledなdeltaTimeとfixedDeltaTimeは上の既存callbackが返す、ここではunscaled系と累積値を返す
	//============================================================================
	float ManagedScriptRuntime::GetUnscaledDeltaTimeCallback() {
		return unscaledDeltaTime_;
	}

	float ManagedScriptRuntime::GetUnscaledFixedDeltaTimeCallback() {
		// 固定ステップ間隔はtime scaleに依らず一定で、scaleの影響はsubstep回数側に出る
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
		// serviceがtime scaleのauthorityで、authoringのTimeScaleComponentへは書き戻さない
		timeScale_ = (!std::isfinite(value)) ? 1.0f : (value < 0.0f ? 0.0f : value);
	}

	uint64_t ManagedScriptRuntime::GetFrameCountCallback() {
		return frameCount_;
	}

	//============================================================================
	//	AssetRef runtime resolve Callbacks
	//	UUID主体でnative resourceやGPUやfilesystem pathは返さない、表示名はstemのみ
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
			// 表示名はasset pathのstemつまり拡張子なしファイル名で、path自体はC#へ渡さない
			name = context->assetDatabase->ResolveFullPath(AssetID{ assetId }).stem().string();
		}
		return CopyStringToBuffer(name, buffer, capacity);
	}

	int32_t ManagedScriptRuntime::CopyProjectRootCallback(char* buffer, int32_t capacity) {

		// InputActions.json等のProjectSettings解決用でGameAssetsが属するgame rootをUTF-8で返す、ProjectSettingsはGameAssetsと同じ階層に置く運用
		const std::string root = RuntimePaths::GetGameRoot().string();
		return CopyStringToBuffer(root, buffer, capacity);
	}

} // Engine
