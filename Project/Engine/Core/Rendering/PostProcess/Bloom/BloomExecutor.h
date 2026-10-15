#pragma once

//============================================================================
//	include
//============================================================================
#include "BloomSettings.h"

// c++
#include <string>

namespace Engine {

	class GraphicsCore;
	struct SceneExecutionContext;
	struct RenderPipelineDeps;

	//============================================================================
	//	BloomExecutor namespace
	//	1項目のBloomから縮小と拡大と合成を実行する
	//============================================================================
	namespace BloomExecutor {

		// 入力を保持したまま指定出力へHDRの光を重ねる
		bool Execute(GraphicsCore& graphicsCore, SceneExecutionContext& context, const RenderPipelineDeps& deps,
			UUID passID, const std::string& sourceAlias, const std::string& outputAlias, const BloomSettings& settings);
	}
}
