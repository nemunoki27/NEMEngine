#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Core/BuiltinRenderBackendBase.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Sprite/SpriteBatchResources.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/FrameBatchResourcePool.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>

namespace Engine {

	//============================================================================
	//	SpriteRenderBackend class
	//	スプライト描画を処理するクラス
	//============================================================================
	class SpriteRenderBackend :
		public BuiltinRenderBackendBase {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		SpriteRenderBackend() {
			vsInstSRVSlot_  = perDrawBindCache_.AddSlot("gVSInstances",  ShaderBindingKind::SRV);
			psInstSRVSlot_  = perDrawBindCache_.AddSlot("gPSInstances",  ShaderBindingKind::SRV);
			textureSRVSlot_ = perDrawBindCache_.AddSlot("gTexture",      ShaderBindingKind::SRV);
		}
		~SpriteRenderBackend() override;

		void BeginFrame(GraphicsCore& graphicsCore) override;

		void DrawBatch(const RenderDrawContext& context, std::span<const RenderItem* const> items) override;

		//--------- accessor -----------------------------------------------------

		uint32_t GetID() const override { return RenderBackendID::Sprite; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// バッチ描画に使用するリソース
		FrameBatchResourcePool<SpriteBatchResources> resourcePool_;

		PipelineBindingCache::SlotID vsInstSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID psInstSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID textureSRVSlot_ = PipelineBindingCache::kInvalidSlot;
	};
} // Engine

