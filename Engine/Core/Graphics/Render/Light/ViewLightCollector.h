#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/Light/FrameLightBatch.h>

namespace Engine {

	// front
	struct SceneInstance;

	//============================================================================
	//	ViewLightCollector class
	//	ビューとシーンに応じて使用ライトを集める
	//============================================================================
	class ViewLightCollector {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ViewLightCollector() = default;
		~ViewLightCollector() = default;

		// ビューに対して使用ライトを集める
		static void CollectForView(const FrameLightBatch& batch, const SceneInstance* sceneInstance,
			const ResolvedRenderView& view, PerViewLightSet& outSet);
	};
} // Engine