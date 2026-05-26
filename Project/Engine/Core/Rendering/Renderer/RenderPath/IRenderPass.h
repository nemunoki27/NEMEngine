#pragma once

//============================================================================
//	include
//============================================================================
#include <string_view>

namespace Engine {

	// front
	class GraphicsCore;
	struct SceneExecutionContext;
	struct RenderPassPhaseBuckets;

	//============================================================================
	//	IRenderPass class
	//	固定RenderPathの各工程を表すインターフェース
	//============================================================================
	class IRenderPass {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		virtual ~IRenderPass() = default;

		// パス名の取得
		virtual std::string_view GetName() const = 0;
		// 毎フレームの実行
		virtual void Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets,
			SceneExecutionContext& context) = 0;
	};
} // Engine
