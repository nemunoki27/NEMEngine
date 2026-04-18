#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/Backend/Interface/IRenderBackend.h>
#include <Engine/Core/Graphics/Render/Backend/Builtin/Sprite/SpriteBatchResources.h>
#include <Engine/Core/Graphics/Render/Backend/Common/FrameBatchResourcePool.h>
#include <Engine/Core/Graphics/Pipeline/Bind/GraphicsRootBinder.h>

namespace Engine {

	//============================================================================
	//	SpriteRenderBackend class
	//	スプライト描画を処理するクラス
	//============================================================================
	class SpriteRenderBackend :
		public IRenderBackend {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		SpriteRenderBackend() = default;
		~SpriteRenderBackend() = default;

		void BeginFrame(GraphicsCore& graphicsCore) override;

		void DrawBatch(const RenderDrawContext& context, std::span<const RenderItem* const> items) override;

		bool CanBatch(const RenderItem& first, const RenderItem& next, const GraphicsRuntimeFeatures& features) const override;

		//--------- accessor -----------------------------------------------------

		uint32_t GetID() const override { return RenderBackendID::Sprite; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// バッチ描画に使用するリソース
		FrameBatchResourcePool<SpriteBatchResources> resourcePool_;

		std::vector<GraphicsBindItem> bindScratch_{};
	};
} // Engine