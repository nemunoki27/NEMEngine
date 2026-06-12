#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/SceneComponentOverlay/SceneComponentOverlayRegistry.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>

namespace Engine {

	// front
	class ECSWorld;

	//============================================================================
	//	SceneComponentOverlayCollector class
	//	SceneView専用コンポーネント表示の可視アイテムをCPU側で集める
	//============================================================================
	class SceneComponentOverlayCollector {
	public:
		SceneComponentOverlayCollector() = default;
		~SceneComponentOverlayCollector() = default;

		void Collect(ECSWorld& world, const ResolvedRenderView& view,
			const SceneComponentOverlayRegistry& registry,
			const SceneComponentOverlaySettings& settings,
			SceneComponentOverlayItemList& outItems) const;
	private:
		// 射影時の0除算や極小wを避けるための余白
		static constexpr float kProjectionEpsilon = 0.0001f;
	};
}
