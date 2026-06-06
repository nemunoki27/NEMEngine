#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Outline/ScreenSpaceOutlineTypes.h>
#include <Engine/Core/Rendering/Renderer/Outline/ScreenSpaceOutlineGPUTypes.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/StructuredInstanceBuffer.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/ViewConstantBuffer.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <span>
#include <vector>

namespace Engine {

	// front
	class GraphicsCore;
	class RenderTexture2D;
	struct SceneExecutionContext;
	struct RenderPassPhaseBuckets;
	struct RenderPipelineDeps;
	struct ScreenSpaceOutlineViewResources;
	struct RenderItem;

	//============================================================================
	//	ScreenSpaceOutlineRenderer class
	//	Runtime ComponentとEditor選択requestで共通利用する画面空間アウトライン描画。
	//	Mask -> Dilation -> Compositeの順に、指定されたview単位resourceへ描く。
	//============================================================================
	class ScreenSpaceOutlineRenderer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ScreenSpaceOutlineRenderer();
		~ScreenSpaceOutlineRenderer();

		void Init(GraphicsCore& graphicsCore);
		void Finalize();

		void Render(GraphicsCore& graphicsCore, SceneExecutionContext& context,
			const RenderPassPhaseBuckets& passBuckets, const RenderPipelineDeps& deps,
			std::span<const ScreenSpaceOutlineRequest> requests,
			ScreenSpaceOutlineViewResources& resources);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct DrawRecord {

			ScreenSpaceOutlineRequest request{};
			uint32_t styleID = 0;
		};

		//--------- variables ----------------------------------------------------

		StructuredInstanceBuffer<ScreenSpaceOutlineStyleGPU> styleBuffer_{ "gOutlineStyles" };
		ViewConstantBuffer<ScreenSpaceOutlineDilateConstants> dilateConstants_{ "DilateConstants" };
		ViewConstantBuffer<ScreenSpaceOutlineCompositeConstants> compositeConstants_{ "CompositeConstants" };

		// Horizontal/Vertical Dilationは同じbinding schemaなのでcacheを共有する
		PipelineBindingCache dilateBindCache_{};
		PipelineBindingCache::SlotID dilateConstantsCBVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID dilateInputMaskSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID dilateStylesSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID dilateOutputUAVSlot_ = PipelineBindingCache::kInvalidSlot;

		PipelineBindingCache compositeBindCache_{};
		PipelineBindingCache::SlotID compositeConstantsCBVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID compositeMaskSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID compositeDilatedMaskSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID compositeProjectedCoverageMaskSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID compositeStylesSRVSlot_ = PipelineBindingCache::kInvalidSlot;

		std::vector<ScreenSpaceOutlineStyleGPU> styleScratch_{};
		std::vector<DrawRecord> drawScratch_{};
		std::vector<const RenderItem*> itemScratch_{};

		bool initialized_ = false;
		bool overflowLogged_ = false;

		//--------- functions ----------------------------------------------------

		bool BuildDrawRecords(std::span<const ScreenSpaceOutlineRequest> requests,
			uint32_t& outMaxRadiusPixels);
		// Visible Mask と Projected Coverage Mask の両方を Clear する
		void ClearMask(GraphicsCore& graphicsCore, ScreenSpaceOutlineViewResources& resources) const;
		// 実際に見えている表面(Visible Mask)と、Depth無視の投影範囲(Projected Coverage Mask)の2種類を描画する
		void DrawMask(GraphicsCore& graphicsCore, SceneExecutionContext& context,
			const RenderPassPhaseBuckets& passBuckets, const RenderPipelineDeps& deps,
			ScreenSpaceOutlineViewResources& resources);
		// Mask/HorizontalDilated/Dilatedが全て有効か
		static bool ValidateDilationResources(const ScreenSpaceOutlineViewResources& resources);
		bool ExecuteDilation(GraphicsCore& graphicsCore, const RenderPipelineDeps& deps,
			ScreenSpaceOutlineViewResources& resources, uint32_t maxRadiusPixels);
		// 1段ぶんのDilation Compute(input SRV -> output UAV)。bindingが揃わなければDispatchしない
		bool ExecuteDilationPass(GraphicsCore& graphicsCore, const RenderPipelineDeps& deps,
			AssetID pipelineID, ScreenSpaceOutlineViewResources& resources,
			RenderTexture2D* inputMask, RenderTexture2D* outputMask, uint32_t safeRadius,
			bool finalToPixelShader, const wchar_t* label);

		bool ExecuteComposite(GraphicsCore& graphicsCore, SceneExecutionContext& context,
			const RenderPipelineDeps& deps, ScreenSpaceOutlineViewResources& resources);
	};
} // Engine
