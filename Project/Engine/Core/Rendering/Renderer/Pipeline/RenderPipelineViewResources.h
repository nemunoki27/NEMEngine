#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingViewBufferSet.h>
#include <Engine/Core/Rendering/Renderer/Lighting/GPU/ViewLightBufferSet.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetRegistry.h>

namespace Engine {

	class GraphicsCore;
	//============================================================================
	//	RenderPipelineViewResources class
	//	描画Viewに属するGPU資源を所有する
	//============================================================================
	class RenderPipelineViewResources {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// Light転送先を生成する
		void EnsureLightBuffers(GraphicsCore& graphicsCore);
		// View用のRaytracing転送先を生成する
		void EnsureRaytracingBuffers(GraphicsCore& graphicsCore);
	private:
		//========================================================================
		//	private Methods
		//========================================================================
		friend class RenderPipelineRunner;

			ResolvedRenderView view{};
			RenderPathResources resources{};
			RaytracingViewBufferSet raytracingBuffers{};
			RenderTargetRegistry targetRegistry{};
			PerViewLightSet lightSet{};
			ViewLightBufferSet lightBuffers{};

	};
}
