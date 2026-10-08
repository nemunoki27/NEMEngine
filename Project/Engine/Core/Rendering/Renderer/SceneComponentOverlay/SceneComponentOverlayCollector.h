#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/SceneComponentOverlay/SceneComponentOverlayRegistry.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>

namespace Engine {

	// 前方宣言
	class ECSWorld;

	//============================================================================
	//	SceneComponentOverlayCollector class
	//	SceneView専用コンポーネント表示の可視アイテムをCPU側で集める
	//============================================================================
	class SceneComponentOverlayCollector {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		SceneComponentOverlayCollector() = default;
		~SceneComponentOverlayCollector() = default;

		// カメラと照明の表示位置を集める
		void Collect(const ECSWorld& world, const ResolvedRenderView& view, const SceneComponentOverlayRegistry& registry,
			const SceneComponentOverlaySettings& settings, SceneComponentOverlayItemList& outItems) const;
	};
}
