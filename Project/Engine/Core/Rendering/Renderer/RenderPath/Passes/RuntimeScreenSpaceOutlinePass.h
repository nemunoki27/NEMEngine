#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/DeferredRenderPath.h>
#include <Engine/Core/Rendering/Renderer/Outline/ScreenSpaceOutlineRenderer.h>

// c++
#include <span>
#include <vector>

namespace Engine {

	// front
	struct RenderPassItemList;
	class MultiRenderTarget;

	//============================================================================
	//	RuntimeScreenSpaceOutlinePass class
	//スクリーンスペースアウトラインの合成
	//============================================================================
	class RuntimeScreenSpaceOutlinePass :
		public IRenderPass {
	public:
		// 描画対象と合成先
		enum class Scope :
			uint8_t {

			Scene,
			PostProcessUI,
			ScreenUI,
		};

		//============================================================================
		//	public Methods
		//============================================================================

		explicit RuntimeScreenSpaceOutlinePass(
			const RenderPipelineDeps& deps, Scope scope = Scope::Scene) :
			deps_(deps), scope_(scope) {}
		~RuntimeScreenSpaceOutlinePass() override = default;

		void Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets,
			SceneExecutionContext& context) override;

		//--------- accessor -----------------------------------------------------

		RenderPathPassKind GetKind() const override {

			switch (scope_) {
			case Scope::PostProcessUI:
				return RenderPathPassKind::PostProcessUI;
			case Scope::ScreenUI:
				return RenderPathPassKind::ScreenUI;
			default:
				return RenderPathPassKind::RuntimeScreenSpaceOutline;
			}
		}
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// UIの描画途中へ挿入するアウトライン要求
		struct ScheduledUIRequest {

			size_t afterItemIndex = 0;
			ScreenSpaceOutlineRequest request{};
		};

		//--------- variables ----------------------------------------------------

		const RenderPipelineDeps& deps_;
		Scope scope_ = Scope::Scene;
		ScreenSpaceOutlineRenderer renderer_{};
		std::vector<ScreenSpaceOutlineRequest> requests_{};
		std::vector<ScreenSpaceOutlineRequest> requestBatchScratch_{};
		std::vector<ScheduledUIRequest> scheduledUIRequests_{};
		std::vector<const RenderItem*> uiDrawScratch_{};

		//--------- functions ----------------------------------------------------

		// UIを描画順に区切り、対象直後へアウトラインを合成する
		void ExecuteOrderedUI(GraphicsCore& graphicsCore,
			const RenderPassPhaseBuckets& passBuckets,
			SceneExecutionContext& context);
		// UIアイテムの指定範囲を通常描画する
		void DrawUIRange(GraphicsCore& graphicsCore,
			const RenderPassItemList& items,
			SceneExecutionContext& context,
			MultiRenderTarget* target,
			size_t beginIndex, size_t endIndex);
		// 指定要求をUI出力へ合成する
		void RenderUIRequests(GraphicsCore& graphicsCore,
			const RenderPassPhaseBuckets& passBuckets,
			SceneExecutionContext& context,
			RenderPhase phase, MultiRenderTarget* target,
			std::span<const ScreenSpaceOutlineRequest> requests);
		void CollectRequests(const SceneExecutionContext& context,
			const RenderPassPhaseBuckets& passBuckets,
			std::span<const RenderPhase> phases);
	};
} // Engine

