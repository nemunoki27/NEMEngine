#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Core/BuiltinRenderBackendBase.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Text/TextBatchResources.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/FrameBatchResourcePool.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>

// c++
#include <vector>
#include <limits>

namespace Engine {

	//============================================================================
	//	TextRenderBackend class
	//	テキスト描画を処理するクラス
	//============================================================================
	class TextRenderBackend :
		public BuiltinRenderBackendBase {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		TextRenderBackend() {
			vsInstSRVSlot_ = perDrawBindCache_.AddSlot("gVSInstances",  ShaderBindingKind::SRV);
			psInstSRVSlot_ = perDrawBindCache_.AddSlot("gPSInstances",  ShaderBindingKind::SRV);
			atlasSRVSlot_  = perDrawBindCache_.AddSlot("gAtlas",        ShaderBindingKind::SRV);
		}
		~TextRenderBackend() override;

		void BeginFrame(GraphicsCore& graphicsCore) override;

		void DrawBatch(const RenderDrawContext& context, std::span<const RenderItem* const> items) override;

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

		PipelineBindingCache::SlotID vsInstSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID psInstSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID atlasSRVSlot_ = PipelineBindingCache::kInvalidSlot;
	};
} // Engine

