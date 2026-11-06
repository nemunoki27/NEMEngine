#pragma once

//============================================================================
//	include
//============================================================================
#include <string>

namespace Engine {

	class GraphicsCore;
	class MultiRenderTarget;
	struct SceneExecutionContext;
	struct RenderPipelineDeps;
	struct RenderFeaturePassSettings;
	struct RenderFeaturePassRuntimeOverride;

	// 固定反射の最終合成を同じ画素へ直接書き戻す
	bool TryCompositeCameraReflectionInPlace(GraphicsCore& graphicsCore, SceneExecutionContext& context,
		const RenderPipelineDeps& deps, const RenderFeaturePassSettings& pass,
		const RenderFeaturePassRuntimeOverride* runtimeOverride, MultiRenderTarget& sceneFinal,
		const std::string& sceneColorAlias);
}
