#pragma once

//============================================================================
//	include
//============================================================================
#include "RenderingFeatureTypes.h"

namespace Engine::GraphicsFeatureSelection {

	// 能力と設定から実行時の機能を決定する
	GraphicsRuntimeFeatures Resolve(const GraphicsFeatureSupport& support, const GraphicsFeaturePreferences& preferences);
}
