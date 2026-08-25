#pragma once

//============================================================================
//	include
//============================================================================
#include <cstdint>
#include <string_view>

namespace Engine {

	// front
	class GraphicsCore;
	struct SceneExecutionContext;
	struct RenderPassPhaseBuckets;

	enum class RenderPathPassKind : uint8_t {

		ClearRenderTargets,
		Skybox,
		DepthPrepass,
		Opaque,
		Lighting,
		RenderFeature,
		InvertedHullOutline,
		Transparent,
		RuntimeScreenSpaceOutline,
		PostProcessUI,
		EditorSelectionScreenSpaceOutline,
		BlitToView,
		ScreenUI,
		DebugOverlay,
		EditorOverlay,
	};

	//============================================================================
	//	IRenderPass class
	//	固定RenderPathの各工程を表すインターフェース
	//============================================================================
	class IRenderPass {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		virtual ~IRenderPass() = default;

		// パス種別の取得
		virtual RenderPathPassKind GetKind() const = 0;
		// 毎フレームの実行
		virtual void Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets,
			SceneExecutionContext& context) = 0;
	};
} // Engine
