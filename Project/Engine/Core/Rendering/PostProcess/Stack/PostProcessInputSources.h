#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <array>

namespace Engine {

	//============================================================================
	//	PostProcessInputSources
	//	PostProcessのSRV入力へ割り当て可能な中間RT名、候補の増減はこの配列の編集で済む
	//	名前はRenderPipelineRunnerがレジストリへ登録するアタッチメント名と一致させること
	//============================================================================
	inline constexpr std::array<const char*, 8> kPostProcessInputSources = {
		"SceneColorMain",
		"SceneNormalMain",
		"ScenePositionMain",
		"SceneMaterialMain",
		"SceneEmissiveMain",
		"SceneFlagsMain",
		"SceneDepth",
		"SceneColorFinal",
	};
} // Engine
