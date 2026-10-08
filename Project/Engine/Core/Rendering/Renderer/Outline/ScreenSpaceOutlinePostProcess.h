#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Outline/ScreenSpaceOutlineGPUTypes.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/StructuredInstanceBuffer.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/ViewConstantBuffer.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <span>

namespace Engine {

	class GraphicsCore;
	class MultiRenderTarget;
	class RenderTexture2D;
	struct SceneExecutionContext;
	struct RenderPipelineDeps;
	struct ScreenSpaceOutlineViewResources;

	//============================================================================
	//	ScreenSpaceOutlinePostProcess class
	//	輪郭の膨張と合成に使うGPU資源を管理する
	//============================================================================
	class ScreenSpaceOutlinePostProcess {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ScreenSpaceOutlinePostProcess();
		// GPU転送先を準備する
		void Init(GraphicsCore& graphicsCore);
		// Style転送先を解放する
		void Release();
		// Styleを転送する
		void UploadStyles(std::span<const ScreenSpaceOutlineStyleGPU> styles);
		static bool ValidateDilationResources(const ScreenSpaceOutlineViewResources& resources);
		bool ExecuteDilation(GraphicsCore& graphicsCore, const RenderPipelineDeps& deps,
			ScreenSpaceOutlineViewResources& resources, uint32_t maxRadiusPixels, uint32_t styleCount);
		bool ExecuteComposite(GraphicsCore& graphicsCore, SceneExecutionContext& context,
			const RenderPipelineDeps& deps, ScreenSpaceOutlineViewResources& resources,
			MultiRenderTarget* compositeTarget, uint32_t styleCount);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

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

		//--------- functions ----------------------------------------------------

		// 入力Maskから膨張結果を書き込む
		bool ExecuteDilationPass(GraphicsCore& graphicsCore, const RenderPipelineDeps& deps,
			AssetID pipelineID, ScreenSpaceOutlineViewResources& resources,
			RenderTexture2D* inputMask, RenderTexture2D* outputMask, uint32_t safeRadius,
			bool finalToPixelShader, const wchar_t* label, uint32_t styleCount);

	};
}
