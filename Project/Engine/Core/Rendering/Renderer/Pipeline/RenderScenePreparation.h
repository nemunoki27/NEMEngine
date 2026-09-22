#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Queues/RenderQueue.h>
#include <Engine/Core/Rendering/Renderer/Lighting/FrameLightBatch.h>
#include <unordered_set>

namespace Engine {

	class GraphicsCore;
	class AssetDatabase;
	class MeshRenderBackend;
	class RenderExtractorRegistry;
	class LightExtractorRegistry;
	class RenderAssetReloadService;
	struct SceneInstance;
	struct ResolvedRenderView;

	//============================================================================
	//	RenderScenePreparation class
	//	描画対象の抽出とMesh要求をまとめる
	//============================================================================
	class RenderScenePreparation {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 描画対象とLightを抽出する
		void Extract(ECSWorld& world, RenderExtractorRegistry& extractors, LightExtractorRegistry& lightExtractors,
			RenderAssetReloadService* materialStates);
		// Viewに必要なMeshを要求する
		void RequestMeshes(GraphicsCore& graphicsCore, AssetDatabase* assetDatabase, MeshRenderBackend* meshBackend,
			const SceneInstance* activeScene, const ResolvedRenderView& gameView, const ResolvedRenderView& sceneView);
	private:
		//========================================================================
		//	private Methods
		//========================================================================
		friend class RenderPipelineRunner;

		RenderSceneBatch renderBatch_{};
		FrameLightBatch frameLightBatch_{};
		std::unordered_set<AssetID> visibleMeshSet_{};
		std::vector<AssetID> visibleMeshes_{};
	};
}
