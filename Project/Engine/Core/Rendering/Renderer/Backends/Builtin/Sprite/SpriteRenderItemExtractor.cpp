#include "SpriteRenderItemExtractor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/UVTransformComponent.h>
#include <Engine/Core/World/UI/UIRuntimeService.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>

//============================================================================
//	SpriteRenderItemExtractor internal
//============================================================================
namespace {

	// 同じMaterial Instance値を持つSpriteを同一バッチへまとめる
	uint64_t ResolveBatchKey(const Engine::MaterialParameterSet& parameters) {

		return parameters.GetContentHash();
	}
}

//============================================================================
//	SpriteRenderItemExtractor classMethods
//============================================================================
void Engine::SpriteRenderItemExtractor::Extract(ECSWorld& world, RenderSceneBatch& batch) {

	world.ForEach<SpriteRendererComponent>([&](const Entity& entity, const SpriteRendererComponent& renderer) {

		// 描画可能か
		if (!RenderItemExtract::IsVisible(world, entity, renderer.visible)) {
			return;
		}

		// デフォルトUV
		Matrix4x4 uvMatrix = Matrix4x4::Identity();
		if (const auto* uvTransform = world.TryGetComponent<UVTransformComponent>(entity)) {

			uvMatrix = uvTransform->uvMatrix;
		}

		// ペイロード構築
		SpriteRenderPayload payload{};
		payload.size = renderer.size;
		payload.pivot = renderer.pivot;
		payload.uvMatrix = uvMatrix;
		// Renderer固有Material Instanceを描画時に既定値へ重ねる
		payload.materialInstance = &renderer.materialInstance.Get();
		// 描画アイテムの構築
		RenderItem item{};
		const UIElementRuntime* uiRuntime = UIRuntimeService::GetInstance().Find(world, entity);
		const Matrix4x4 worldMatrix = uiRuntime ? uiRuntime->screenMatrix : RenderItemExtract::GetWorldMatrix(world, entity);
		RenderItemExtract::FillCommonFields(item, world, entity, renderer, worldMatrix);
		item.backendID = RenderBackendID::Sprite;
		item.material = renderer.material;
		item.batchKey = ResolveBatchKey(renderer.materialInstance);
		if (uiRuntime) {
			// シーンに保存されない内部UIはアクティブシーンの描画へ含める
			if (!item.sceneInstanceID) {
				const SceneInstance* activeScene = world.GetCommandServices().sceneInstances ?
					world.GetCommandServices().sceneInstances->GetActive() : nullptr;
				item.sceneInstanceID = activeScene ? activeScene->instanceID : UUID{};
			}
			item.cameraDomain = RenderCameraDomain::Screen;
			item.sortingLayer += uiRuntime->canvasSortingLayer;
			item.sortingOrder += uiRuntime->canvasOrder;
			item.orderedUI = true;
			item.hierarchyOrder = uiRuntime->hierarchyOrder;
		} else {
			item.cameraDomain = RenderCameraDomain::Orthographic;
		}
		item.payload = batch.PushPayload(payload);
		// 描画アイテムをバッチに追加
		batch.Add(std::move(item));
		});
}
