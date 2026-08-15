#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetNames.h>

// c++
#include <array>

namespace Engine {

	// ProfileのSRVへ割り当て可能な固定RenderPathの出力名
	inline constexpr std::array<const char*, 8> kRenderFeatureInputSources = {
		RenderTargetNames::kSceneColorMain,
		RenderTargetNames::kSceneNormalMain,
		RenderTargetNames::kScenePositionMain,
		RenderTargetNames::kSceneMaterialMain,
		RenderTargetNames::kSceneEmissiveMain,
		RenderTargetNames::kSceneFlagsMain,
		RenderTargetNames::kSceneDepth,
		RenderTargetNames::kSceneColorFinal,
	};
} // Engine
