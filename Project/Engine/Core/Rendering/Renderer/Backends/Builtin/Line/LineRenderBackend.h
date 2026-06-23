#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Core/IRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineBatchResources.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineRenderTypes.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/FrameBatchResourcePool.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RegistryAutoBindTable.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterBinder.h>

// c++
#include <vector>

namespace Engine {

	//============================================================================
	//	LineRenderBackend class
	//	ライン描画を処理するクラス
	//============================================================================
	class LineRenderBackend :
		public IRenderBackend {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		LineRenderBackend() {
			viewCBVSlot_ = perDrawBindCache_.AddSlot("ViewConstants", ShaderBindingKind::CBV);
			materialParamsCBVSlot_ = perDrawBindCache_.AddSlot(MaterialParameterCBuffer::kSurface, ShaderBindingKind::CBV);
		}
		~LineRenderBackend() override;

		void BeginFrame(GraphicsCore& graphicsCore) override;

		void DrawBatch(const RenderDrawContext& context, std::span<const RenderItem* const> items) override;

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

		// バッファレジストリ→ Graphicsパイプラインスロットの対応キャッシュ
		RegistryAutoBindTable registryAutoBindTable_{};
		// 描画固有バインドのパイプラインスロットキャッシュ
		PipelineBindingCache perDrawBindCache_{};
		PipelineBindingCache::SlotID viewCBVSlot_ = PipelineBindingCache::kInvalidSlot;
		// reflection駆動のマテリアルパラメータcbuffer、カスタムマテリアル用でBuiltinには存在しない
		PipelineBindingCache::SlotID materialParamsCBVSlot_ = PipelineBindingCache::kInvalidSlot;
		MaterialParameterBinder materialParamBinder_{};

		// 全アイテムのポリラインを展開した線分リスト、毎バッチ使い回す
		std::vector<LineVertex> lineScratch_{};

		//--------- functions ----------------------------------------------------

		// ポリライン1本を線分リストへ展開してlineScratch_へ積む
		void AppendPolyline(const RenderItem& item, const LineRenderPayload& payload);
	};
} // Engine
