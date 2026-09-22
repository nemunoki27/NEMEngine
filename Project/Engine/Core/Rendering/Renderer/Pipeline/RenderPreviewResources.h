#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Common/FrameBatchResourcePool.h>
#include <Engine/Core/Rendering/Renderer/Backends/Registry/RenderBackendRegistry.h>
#include <Engine/Core/Rendering/Renderer/Lighting/GPU/ViewLightBufferSet.h>

namespace Engine {

	class MeshRenderBackend;

	//============================================================================
	//	RenderPreviewResources class
	//	複数Preview描画で共有するBackendとGPU資源を所有する
	//============================================================================
	class RenderPreviewResources {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 最初のPreview要求でFrameを開始する
		void BeginFrame(GraphicsCore& graphicsCore);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		friend class RenderPipelineRunner;

		PerViewLightSet previewLightSet_{};
		FrameBatchResourcePool<ViewLightBufferSet> previewLightBufferPool_{};
		RenderBackendRegistry previewBackendRegistry_{};
		bool previewBackendFrameStarted_ = false;
		MeshRenderBackend* previewMeshBackend_ = nullptr;
	};
}
