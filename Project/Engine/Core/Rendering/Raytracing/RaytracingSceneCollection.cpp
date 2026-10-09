#include "RaytracingSceneBuilder.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Common/RenderBillboardUtility.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <unordered_set>

//============================================================================
//	RaytracingSceneBuilder classMethods
//============================================================================
void Engine::RaytracingSceneBuilder::CollectSceneMeshInstances(const RenderSceneBatch& renderBatch,
	const SceneInstance& scene, const ResolvedRenderView* view, std::vector<CollectedMeshInstance>& outInstances) {

	outInstances.clear();
	std::unordered_set<SceneEntityKey, SceneEntityKeyHash> collectedEntities{};

	// シーンインスタンスIDを取得する
	const UUID sceneInstanceID = scene.instanceID;
	for (const RenderItem& item : renderBatch.GetItems()) {

		// メッシュ描画アイテムで、かつシーンインスタンスIDが一致するものを対象とする
		if (item.backendID != RenderBackendID::Mesh) {
			continue;
		}
		if (!globalIlluminationScene_ && sceneInstanceID && item.sceneInstanceID != sceneInstanceID) {
			continue;
		}
		if (globalIlluminationScene_ && view && (item.visibilityLayerMask & view->GetCullingMask(RenderCameraDomain::Perspective)) == 0u) continue;
		const MeshRenderPayload* payload = renderBatch.GetPayload<MeshRenderPayload>(item);
		if (!payload || !payload->mesh) {
			continue;
		}
		const SceneEntityKey entityKey{
			.world = item.world,
			.entity = item.entity,
		};
		if (!collectedEntities.emplace(entityKey).second) {
			continue;
		}

		// 収集したメッシュインスタンスの情報を追加する
		CollectedMeshInstance instance{};
		instance.meshAssetID = payload->mesh;
		instance.entity = item.entity;
		instance.world = item.world;
		instance.worldMatrix = item.worldMatrix;
		if (view) {
			instance.worldMatrix = RenderBillboard::ResolveWorldMatrix(item, *view);
		}
		instance.renderer = nullptr;
		instance.castShadows = item.castShadows;
		instance.viewDependent = RenderBillboard::HasBillboard(item);
		if (item.world && item.world->IsAlive(item.entity)) {
			if (item.world->HasComponent<MeshRendererComponent>(item.entity)) {

				instance.renderer = &item.world->GetComponent<MeshRendererComponent>(item.entity);
			}
		}
		outInstances.emplace_back(instance);
	}
}

void Engine::RaytracingSceneBuilder::CollectScenePrimitiveInstances(const RenderSceneBatch& renderBatch,
	const SceneInstance& scene, const ResolvedRenderView* view, std::vector<CollectedPrimitiveInstance>& outInstances) {

	outInstances.clear();

	const UUID sceneInstanceID = scene.instanceID;
	for (const RenderItem& item : renderBatch.GetItems()) {

		if (item.backendID != RenderBackendID::Primitive) {
			continue;
		}
		if (!globalIlluminationScene_ && sceneInstanceID && item.sceneInstanceID != sceneInstanceID) {
			continue;
		}
		if (globalIlluminationScene_ && (item.surfaceMode == MaterialSurfaceMode::Transparent ||
			(view && (item.visibilityLayerMask & view->GetCullingMask(RenderCameraDomain::Perspective)) == 0u))) continue;
		if (!item.world || !item.world->IsAlive(item.entity)) {
			continue;
		}
		if (!item.world->HasComponent<PrimitiveRendererComponent>(item.entity)) {
			continue;
		}

		const PrimitiveRenderPayload* payload =
			renderBatch.GetPayload<PrimitiveRenderPayload>(item);
		if (!payload || !payload->renderer) {
			continue;
		}
		const PrimitiveRendererComponent& renderer = *payload->renderer;

		// 画面座標のPrimitiveを影と反射から除外
		if (IsPrimitiveScreen2D(renderer)) {
			continue;
		}

		CollectedPrimitiveInstance instance{};
		instance.entity = item.entity;
		instance.world = item.world;
		instance.worldMatrix = item.worldMatrix;
		if (view) {
			instance.worldMatrix = RenderBillboard::ResolveWorldMatrix(item, *view);
		}
		instance.renderer = &renderer;
		instance.materialInstance = payload->materialInstance;
		instance.material = item.material;
		instance.surfaceMode = item.surfaceMode;
		instance.uvMatrix = payload->uvMatrix;
		instance.castShadows = item.castShadows;
		instance.viewDependent = RenderBillboard::HasBillboard(item);
		outInstances.emplace_back(instance);
	}
}
