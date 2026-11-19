#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <span>

namespace Engine {

	class GraphicsCore;
	class MaterialResolver;
	struct SceneExecutionContext;
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
		// 初めて表示するMeshの描画パスをシーンの準備時に作成する
		void PrepareHiddenMeshPipelines(GraphicsCore& graphicsCore, const SceneExecutionContext& scene,
			RenderAssetLibrary& assets, PipelineStateCache& pipelines, MaterialResolver& resolver);
		// 描画用AssetとPipelineを事前に生成する
		void Preload(GraphicsCore& graphicsCore, AssetDatabase& assetDatabase, RuntimeRenderPreloadContext& context,
			std::span<const AssetID> requestedAssets);
	}
}
