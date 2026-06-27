#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetNames.h>

// c++
#include <array>

namespace Engine {

	//============================================================================
	//	PostProcessInputSources
	//	PostProcessのSRV入力へ割り当て可能な中間RT名、候補の増減はこの配列の編集で済む
	//	名前はRenderTargetNamesを参照しレジストリへ登録するアタッチメント名と一致させること
	//============================================================================
	inline constexpr std::array<const char*, 8> kPostProcessInputSources = {
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
