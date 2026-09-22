#include "RaytracingSceneBuilder.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Materials/MaterialResolver.h>
#include <Engine/Core/Rendering/Materials/MaterialParameter.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/RenderBillboardUtility.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshDrawPathCommon.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/Rendering/Primitive/PrimitiveGeometryManager.h>
#include <Engine/Core/Rendering/Primitive/PrimitiveMeshGenerator.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h>

#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Rendering/Meshes/Utility/MeshNormalMatrixUtility.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <bit>
#include <cmath>
#include <cstddef>
#include <memory>
#include <span>
#include <unordered_set>
#include <variant>

//============================================================================
//	RaytracingSceneBuilder internal
//============================================================================
namespace {

	// サブメッシュごとの描画アイテムからEntity単位のTLASインスタンスへまとめるキー
	struct RaytracingEntityKey {

		const Engine::ECSWorld* world = nullptr;
		Engine::Entity entity{};

		bool operator==(const RaytracingEntityKey& other) const {
			return world == other.world && entity == other.entity;
		}
	};

	struct RaytracingEntityKeyHash {

		size_t operator()(const RaytracingEntityKey& key) const {

			uint64_t hash = reinterpret_cast<uintptr_t>(key.world);
			Engine::Algorithm::HashCombine(hash,
				static_cast<uint64_t>(key.entity.index));
			Engine::Algorithm::HashCombine(hash,
				static_cast<uint64_t>(key.entity.generation));
			return static_cast<size_t>(hash);
		}
	};

}

void Engine::RaytracingSceneBuilder::CollectSceneMeshInstances(const RenderSceneBatch& renderBatch,
	const SceneExecutionContext& context, std::vector<CollectedMeshInstance>& outInstances) {

	outInstances.clear();
	std::unordered_set<RaytracingEntityKey,
		RaytracingEntityKeyHash> collectedEntities{};

	// シーンインスタンスIDを取得する
	const UUID sceneInstanceID = context.sceneInstance ? context.sceneInstance->instanceID : UUID{};
	for (const RenderItem& item : renderBatch.GetItems()) {

		// メッシュ描画アイテムで、かつシーンインスタンスIDが一致するものを対象とする
		if (item.backendID != RenderBackendID::Mesh) {
			continue;
		}
		if (sceneInstanceID && item.sceneInstanceID != sceneInstanceID) {
			continue;
		}
		const MeshRenderPayload* payload = renderBatch.GetPayload<MeshRenderPayload>(item);
		if (!payload || !payload->mesh) {
			continue;
		}
		const RaytracingEntityKey entityKey{
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
		if (context.view) {
			instance.worldMatrix = RenderBillboard::ResolveWorldMatrix(item, *context.view);
		}
		instance.renderer = nullptr;
		instance.castShadows = item.castShadows;
		if (item.world && item.world->IsAlive(item.entity)) {
			if (item.world->HasComponent<MeshRendererComponent>(item.entity)) {

				instance.renderer = &item.world->GetComponent<MeshRendererComponent>(item.entity);
			}
		}
		outInstances.emplace_back(instance);
	}
}

void Engine::RaytracingSceneBuilder::CollectScenePrimitiveInstances(const RenderSceneBatch& renderBatch,
	const SceneExecutionContext& context, std::vector<CollectedPrimitiveInstance>& outInstances) {

	outInstances.clear();

	const UUID sceneInstanceID = context.sceneInstance ? context.sceneInstance->instanceID : UUID{};
	for (const RenderItem& item : renderBatch.GetItems()) {

		if (item.backendID != RenderBackendID::Primitive) {
			continue;
		}
		if (sceneInstanceID && item.sceneInstanceID != sceneInstanceID) {
			continue;
		}
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

		// 2D描画はスクリーン空間のUIなので影/反射の対象にしない
		if (IsPrimitiveScreen2D(renderer)) {
			continue;
		}

		CollectedPrimitiveInstance instance{};
		instance.entity = item.entity;
		instance.world = item.world;
		instance.worldMatrix = item.worldMatrix;
		if (context.view) {
			instance.worldMatrix = RenderBillboard::ResolveWorldMatrix(item, *context.view);
		}
		instance.renderer = &renderer;
		instance.materialInstance = payload->materialInstance;
		instance.material = item.material;
		instance.surfaceMode = item.surfaceMode;
		instance.uvMatrix = payload->uvMatrix;
		instance.castShadows = item.castShadows;
		// batchKeyは上書き分離を含むためBLAS共有には形状ハッシュを使う
		instance.geometryHash = PrimitiveMeshGenerator::ComputeHash(renderer);
		outInstances.emplace_back(instance);
	}
}
