#pragma once

//============================================================================
//	include
//============================================================================
#include "AnimationPropertyRegistry.h"
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <utility>

namespace Engine {

	// 描画Shaderから編集可能なMaterialパラメータを集める
	std::vector<std::pair<std::string, AnimationValueType>> CollectMaterialAnimationParameters(
		const AnimationPropertyQueryContext& context, AssetID materialID, const std::string& constantBufferName);
} // Engine
