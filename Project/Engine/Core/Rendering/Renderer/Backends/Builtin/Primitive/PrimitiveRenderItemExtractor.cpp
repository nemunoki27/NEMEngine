#include "PrimitiveRenderItemExtractor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/UVTransformComponent.h>
#include <Engine/Core/World/UI/UIRuntimeService.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/Rendering/Primitive/PrimitiveMeshGenerator.h>

//============================================================================
//	PrimitiveRenderItemExtractor classMethods
//============================================================================
void Engine::PrimitiveRenderItemExtractor::Extract(ECSWorld& world, RenderSceneBatch& batch) {

	world.ForEach<PrimitiveRendererComponent>([&](const Entity& entity, const PrimitiveRendererComponent& renderer) {

		// 描画可能か
		if (!RenderItemExtract::IsVisible(world, entity, renderer.visible)) {
			return;
		}

		// ペイロード構築、UVTransformComponentがあればUV行列を渡す
		PrimitiveRenderPayload payload{};
		payload.renderer = &renderer;
		if (const auto* uvTransform = world.TryGetComponent<UVTransformComponent>(entity)) {
			payload.uvMatrix = uvTransform->uvMatrix;
		}
		payload.materialInstance = &renderer.materialInstance.Get();

		// 描画アイテムの構築
		RenderItem item{};
		const UIElementRuntime* uiRuntime =
			UIRuntimeService::GetInstance().Find(world, entity);
		const Matrix4x4 worldMatrix = uiRuntime ?
			uiRuntime->screenMatrix : RenderItemExtract::GetWorldMatrix(world, entity);
		RenderItemExtract::FillCommonFields(
			item, world, entity, renderer, worldMatrix);
		item.backendID = RenderBackendID::Primitive;
		item.material = renderer.material;
		item.castShadows = HasMeshRenderFlag(
			renderer.renderFlags, MeshRenderFlags::CastShadow);
		item.receiveShadows = HasMeshRenderFlag(
			renderer.renderFlags, MeshRenderFlags::ReceiveShadow);
		// 同一形状と同じMaterial Instance値を同一バッチへまとめる
		const uint64_t shapeHash = PrimitiveMeshGenerator::ComputeHash(renderer);
		const uint64_t materialHash = renderer.materialInstance.GetContentHash();
		item.batchKey = shapeHash ^
			(materialHash + 0x9e3779b97f4a7c15ull +
				(shapeHash << 6) + (shapeHash >> 2));
		item.cameraDomain = RenderCameraDomain::Perspective;
		// Plane/Ringのみ2D描画に対応し、Canvas配下ではスクリーン行列と描画順を使う
		if (IsPrimitiveScreen2D(renderer)) {
			item.renderPhase = RenderPhase::ScreenUI;
			if (uiRuntime) {
				if (!item.sceneInstanceID) {
					const SceneInstance* activeScene =
						world.GetCommandServices().sceneInstances ?
						world.GetCommandServices().sceneInstances->GetActive() : nullptr;
					item.sceneInstanceID = activeScene ?
						activeScene->instanceID : UUID{};
				}
				item.cameraDomain = RenderCameraDomain::Screen;
				item.sortingLayer += uiRuntime->canvasSortingLayer;
				item.sortingOrder += uiRuntime->canvasOrder;
				item.orderedUI = true;
				item.hierarchyOrder = uiRuntime->hierarchyOrder;
			} else {
				item.cameraDomain = RenderCameraDomain::Orthographic;
			}
		}
		item.payload = batch.PushPayload(payload);
		batch.Add(std::move(item));
		});
}
