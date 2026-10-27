#pragma once

//============================================================================
//	include
//============================================================================
#include "RenderingFeatureTypes.h"
#include <json.hpp>

namespace Engine::GraphicsPreferenceStorage {

	// 設定を読み込んで値を検証する
	void Load(GraphicsFeaturePreferences& preferences);
	// 現在の設定を保存する
	void Save(const GraphicsFeaturePreferences& preferences);
	// 設定文書と実行用の値を相互変換する
	void Read(const nlohmann::json& data, GraphicsFeaturePreferences& preferences);
	nlohmann::json Write(const GraphicsFeaturePreferences& preferences);
}
