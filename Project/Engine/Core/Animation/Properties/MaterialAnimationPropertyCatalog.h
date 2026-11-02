#pragma once

//============================================================================
//	include
//============================================================================
#include "AnimationPropertyRegistry.h"
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <utility>

namespace Engine {
	class RenderAssetLibrary;
	void SetAnimationMaterialContext(ECSWorld& world, AssetDatabase* database, RenderAssetLibrary* library);
	bool ReadAnimationMaterialDefault(ECSWorld& world, AssetID material, const std::string& name,
		AnimationValueType type, AnimationPropertyValue& value);

	// 描画Shaderから編集可能なMaterialパラメータを集める
	std::vector<std::pair<std::string, AnimationValueType>> CollectMaterialAnimationParameters(
		ECSWorld& world, const AnimationPropertyQueryContext& context, AssetID materialID, const std::string& constantBufferName);
} // Engine
