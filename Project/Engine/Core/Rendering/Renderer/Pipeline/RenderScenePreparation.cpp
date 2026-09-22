#include "RenderScenePreparation.h"

//============================================================================
//	include
//============================================================================
#include "RenderAssetReloadService.h"
#include "RenderPipelineUtility.h"
#include <Engine/Core/Rendering/Renderer/Backends/Registry/RenderExtractorRegistry.h>
#include <Engine/Core/Rendering/Renderer/Lighting/Registry/LightExtractorRegistry.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>

using namespace Engine;

void RenderScenePreparation::Extract(ECSWorld& world, RenderExtractorRegistry& extractors,
	LightExtractorRegistry& lightExtractors, RenderAssetReloadService* materialStates) {

	extractors.BuildBatch(world, renderBatch_);
	if (materialStates) {
		materialStates->ApplyMaterialRenderStates(renderBatch_);
		renderBatch_.Sort();
	}
	lightExtractors.BuildBatch(world, frameLightBatch_);
}

void RenderScenePreparation::RequestMeshes(GraphicsCore& graphicsCore, AssetDatabase* assetDatabase,
	MeshRenderBackend* meshBackend, const SceneInstance* activeScene,
	const ResolvedRenderView& gameView, const ResolvedRenderView& sceneView) {

	visibleMeshSet_.clear();
	visibleMeshSet_.reserve(renderBatch_.GetItems().size());

	// ビューごとに可視なメッシュアセットIDを収集
	if (meshBackend && activeScene) {
		if (gameView.valid) {
			CollectVisibleMeshAssetsForView(renderBatch_, activeScene->instanceID, gameView, visibleMeshSet_);
		}
		if (sceneView.valid) {
			CollectVisibleMeshAssetsForView(renderBatch_, activeScene->instanceID, sceneView, visibleMeshSet_);
		}
	}

	visibleMeshes_.clear();
	visibleMeshes_.reserve(visibleMeshSet_.size());
	for (const AssetID& id : visibleMeshSet_) {
		visibleMeshes_.emplace_back(id);
	}
	// GPUに可視なメッシュの情報を要求して、必要なリソースを準備
	if (meshBackend && !visibleMeshes_.empty()) {

		meshBackend->RequestMeshes(graphicsCore, *assetDatabase, visibleMeshes_);
	}

}
