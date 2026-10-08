#pragma once

//============================================================================
//	include
//============================================================================
#include "ScreenSpaceOutlinePostProcess.h"
#include <Engine/Core/Rendering/Renderer/Outline/ScreenSpaceOutlineTypes.h>
#include <Engine/Core/Rendering/Renderer/Outline/ScreenSpaceOutlineGPUTypes.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/StructuredInstanceBuffer.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/ViewConstantBuffer.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderQueue.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <span>
#include <vector>

namespace Engine {

	// front
	class GraphicsCore;
	class DepthTexture2D;
	class MultiRenderTarget;
	class RenderTexture2D;
	struct SceneExecutionContext;
	struct RenderPassPhaseBuckets;
	struct RenderPipelineDeps;
	struct ScreenSpaceOutlineViewResources;
	struct RenderItem;

	//============================================================================
	//	ScreenSpaceOutlineRenderer class
	// Runtime ComponentとEditor選択requestで共通利用する画面空間アウトライン描画
	// Mask -> Dilation -> Compositeの順に、指定されたview単位resourceへ描く
	//============================================================================
	class ScreenSpaceOutlineRenderer {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		ScreenSpaceOutlineRenderer();
		~ScreenSpaceOutlineRenderer();

		void Init(GraphicsCore& graphicsCore);
		void Finalize();

		void Render(GraphicsCore& graphicsCore, SceneExecutionContext& context,
			const RenderPassPhaseBuckets& passBuckets, const RenderPipelineDeps& deps,
			std::span<const ScreenSpaceOutlineRequest> requests,
			ScreenSpaceOutlineViewResources& resources,
			std::span<const RenderPhase> phases,
			MultiRenderTarget* compositeTarget,
			DepthTexture2D* depthOverride);
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		struct DrawRecord {

			ScreenSpaceOutlineRequest request{};
			uint32_t styleID = 0;
		};

		//--------- variables ----------------------------------------------------

		ScreenSpaceOutlinePostProcess postProcess_{};

		std::vector<ScreenSpaceOutlineStyleGPU> styleScratch_{};
		std::vector<DrawRecord> drawScratch_{};
		std::vector<const RenderItem*> itemScratch_{};

		bool initialized_ = false;
		bool overflowLogged_ = false;

		//--------- functions ----------------------------------------------------

		// 要求ごとのStyleと描画対象を確定する
		bool BuildDrawRecords(std::span<const ScreenSpaceOutlineRequest> requests,
			uint32_t& outMaxRadiusPixels);
		// Visible MaskとProjected Coverage Maskの両方をClearする
		void ClearMask(GraphicsCore& graphicsCore, ScreenSpaceOutlineViewResources& resources) const;
		// 実際に見えている表面(Visible Mask)と、Depth無視の投影範囲(Projected Coverage Mask)の2種類を描画する
		void DrawMask(GraphicsCore& graphicsCore, SceneExecutionContext& context,
			const RenderPassPhaseBuckets& passBuckets, const RenderPipelineDeps& deps,
			ScreenSpaceOutlineViewResources& resources,
			std::span<const RenderPhase> phases,
			DepthTexture2D* depthOverride);

	};
} // Engine
