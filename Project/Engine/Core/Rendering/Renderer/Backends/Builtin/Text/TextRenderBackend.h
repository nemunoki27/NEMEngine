#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Core/IRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Text/TextBatchResources.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/FrameBatchResourcePool.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RegistryAutoBindTable.h>

// c++
#include <vector>
#include <limits>

namespace Engine {

	//============================================================================
	//	TextRenderBackend class
	//	テキスト描画を処理するクラス
	//============================================================================
	class TextRenderBackend :
		public IRenderBackend {
	public:
		//============================================================================
		//	public Methods
		//============================================================================
		TextRenderBackend() {
			viewCBVSlot_   = perDrawBindCache_.AddSlot("ViewConstants", ShaderBindingKind::CBV);
			vsInstSRVSlot_ = perDrawBindCache_.AddSlot("gVSInstances",  ShaderBindingKind::SRV);
			psInstSRVSlot_ = perDrawBindCache_.AddSlot("gPSInstances",  ShaderBindingKind::SRV);
			atlasSRVSlot_  = perDrawBindCache_.AddSlot("gAtlas",        ShaderBindingKind::SRV);
		}
		~TextRenderBackend() override;

		void BeginFrame(GraphicsCore& graphicsCore) override;

		void DrawBatch(const RenderDrawContext& context, std::span<const RenderItem* const> items) override;

		bool CanBatch(const RenderItem& first, const RenderItem& next, const GraphicsRuntimeFeatures& features) const override;

		//--------- accessor -----------------------------------------------------

		uint32_t GetID() const override { return RenderBackendID::Text; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// バッチ描画に使用するリソース
		FrameBatchResourcePool<TextBatchResources> resourcePool_;

		// 毎バッチ再利用するグリフインスタンス
		std::vector<TextVSInstanceData> vsGlyphScratch_{};
		std::vector<TextPSInstanceData> psGlyphScratch_{};

		// バッファレジストリ → Graphicsパイプラインスロットの対応キャッシュ
		RegistryAutoBindTable registryAutoBindTable_{};
		// 描画固有バインドのパイプラインスロットキャッシュ
		PipelineBindingCache perDrawBindCache_{};
		PipelineBindingCache::SlotID viewCBVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID vsInstSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID psInstSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID atlasSRVSlot_ = PipelineBindingCache::kInvalidSlot;
	};
} // Engine

