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
	struct DecalProjectorComponent;

	namespace DecalCollector {

		// ECS投影とGPU投影で標準Materialの解決を共有する
		bool ResolveMaterial(GraphicsCore& graphicsCore, RenderAssetLibrary& assets,
			const SceneExecutionContext& context, const DecalProjectorComponent& component, float opacity, DecalDrawItem& item);

		// 表示中のSceneから投影とMaterialを確定する
		void Collect(GraphicsCore& graphicsCore, RenderAssetLibrary& assets,
			const SceneExecutionContext& context, std::vector<DecalDrawItem>& output);
	}
}
