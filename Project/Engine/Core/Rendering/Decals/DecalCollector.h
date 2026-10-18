#pragma once

//============================================================================
//	include
//============================================================================
#include "DecalTypes.h"

// c++
#include <vector>

namespace Engine {

	class GraphicsCore;
	class RenderAssetLibrary;
	struct SceneExecutionContext;

	namespace DecalCollector {

		// 表示中のSceneから投影とMaterialを確定する
		void Collect(GraphicsCore& graphicsCore, RenderAssetLibrary& assets,
			const SceneExecutionContext& context, std::vector<DecalDrawItem>& output);
	}
}
