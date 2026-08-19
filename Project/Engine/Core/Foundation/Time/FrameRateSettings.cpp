#include "FrameRateSettings.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

//============================================================================
//	FrameRateSettings classMethods
//============================================================================
Engine::FrameRateSettings& Engine::FrameRateSettings::GetInstance() {

	static FrameRateSettings instance;
	return instance;
}

void Engine::FrameRateSettings::Load(const std::string& configPath) {

	configPath_ = configPath;

	// ファイルが無ければ既定値のままにする
	if (!JsonAdapter::Check(configPath_, false)) {
		return;
	}

	const nlohmann::json data = JsonAdapter::Load(configPath_, false);
	if (!data.is_object()) {
		return;
	}

	// 0は制限なし、保存値はそのまま採用する
	targetFps_ = data.value("targetFps", targetFps_);
	editorTargetFps_ = data.value("editorTargetFps", editorTargetFps_);
}

void Engine::FrameRateSettings::Save() const {

	// パス未設定なら保存しない
	if (configPath_.empty()) {
		return;
	}

	nlohmann::json data = nlohmann::json::object();
	data["targetFps"] = targetFps_;
	data["editorTargetFps"] = editorTargetFps_;
	JsonAdapter::Save(configPath_, data);
}
