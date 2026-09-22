#pragma once

//============================================================================
//	include
//============================================================================
#include "RenderingFeatureTypes.h"

namespace Engine::GraphicsPreferenceStorage {

	// 設定を読み込んで値を検証する
	void Load(GraphicsFeaturePreferences& preferences);
	// 現在の設定を保存する
	void Save(const GraphicsFeaturePreferences& preferences);
}
