#pragma once

//============================================================================
//	include
//============================================================================
#include "RenderingFeatureTypes.h"

namespace Engine::GraphicsFeatureSelection {

	// 組み込み反射のlib_6_6とヒット地点のRayQueryを使用できるか
	bool SupportsMaterialReflections(const GraphicsFeatureSupport& support);

	// 能力と設定から実行時の機能を決定する
	GraphicsRuntimeFeatures Resolve(const GraphicsFeatureSupport& support, const GraphicsFeaturePreferences& preferences);
}
