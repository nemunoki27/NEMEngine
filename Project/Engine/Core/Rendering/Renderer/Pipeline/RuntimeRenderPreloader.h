#pragma once

namespace Engine {

	class GraphicsCore;
	class AssetDatabase;
	class RenderAssetLibrary;
	class PostProcessAssetGenerator;
	class RenderBackendRegistry;
	class MeshRenderBackend;
	class ParticleRenderBackend;
	class ViewportRenderService;
	class RenderPathResources;
	class PipelineStateCache;
	class RaytracingPipelineStateCache;

	struct RuntimeRenderPreloadContext {

		RenderAssetLibrary& assetLibrary;
		PostProcessAssetGenerator& assetGenerator;
		RenderBackendRegistry& backends;
		MeshRenderBackend* meshBackend;
		ParticleRenderBackend* particleBackend;
		ViewportRenderService& viewport;
		RenderPathResources& gameResources;
		PipelineStateCache& pipelines;
		RaytracingPipelineStateCache& raytracingPipelines;
	};

	namespace RuntimeRenderPreloader {
		// 描画用AssetとPipelineを事前に生成する
		void Preload(GraphicsCore& graphicsCore, AssetDatabase& assetDatabase, RuntimeRenderPreloadContext& context);
	}
}
