#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/Backend/Interface/IRenderBackend.h>
#include <Engine/Core/Graphics/Render/Backend/Builtin/Text/TextBatchResources.h>
#include <Engine/Core/Graphics/Render/Backend/Common/FrameBatchResourcePool.h>
#include <Engine/Core/Graphics/Pipeline/Bind/GraphicsRootBinder.h>

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
		//========================================================================
		//	public Methods
		//========================================================================

		TextRenderBackend() = default;
		~TextRenderBackend() = default;

		void BeginFrame(GraphicsCore& graphicsCore) override;

		void DrawBatch(const RenderDrawContext& context, std::span<const RenderItem* const> items) override;

		bool CanBatch(const RenderItem& first, const RenderItem& next, const GraphicsRuntimeFeatures& features) const override;

		//--------- accessor -----------------------------------------------------

		uint32_t GetID() const override { return RenderBackendID::Text; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// バッチ描画に使用するリソース
		FrameBatchResourcePool<TextBatchResources> resourcePool_;

		// 毎バッチ再利用するグリフインスタンス
		std::vector<TextVSInstanceData> vsGlyphScratch_{};
		std::vector<TextPSInstanceData> psGlyphScratch_{};

		std::vector<GraphicsBindItem> bindScratch_{};
	};
} // Engine