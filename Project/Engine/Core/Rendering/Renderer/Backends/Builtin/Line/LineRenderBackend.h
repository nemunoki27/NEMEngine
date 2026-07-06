#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Core/BuiltinRenderBackendBase.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineBatchResources.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineRenderTypes.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/FrameBatchResourcePool.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>

// c++
#include <vector>

namespace Engine {

	//============================================================================
	//	LineRenderBackend class
	//	ライン描画を処理するクラス
	//============================================================================
	class LineRenderBackend :
		public BuiltinRenderBackendBase {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		~LineRenderBackend() override;

		void BeginFrame(GraphicsCore& graphicsCore) override;

		void DrawBatch(const RenderDrawContext& context, std::span<const RenderItem* const> items) override;

		// カメラ種類が違う2Dと3Dは別バッチにする
		bool CanBatch(const RenderItem& first, const RenderItem& next, const GraphicsRuntimeFeatures& features) const override;

		//--------- accessor -----------------------------------------------------

		uint32_t GetID() const override { return RenderBackendID::Line; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// バッチ描画に使用するリソース
		FrameBatchResourcePool<LineBatchResources> resourcePool_;

		// 全アイテムのポリラインを展開した線分リスト、毎バッチ使い回す
		std::vector<LineVertex> lineScratch_{};

		//--------- functions ----------------------------------------------------

		// ポリライン1本を線分リストへ展開してlineScratch_へ積む
		void AppendPolyline(const RenderItem& item, const LineRenderPayload& payload);
	};
} // Engine
