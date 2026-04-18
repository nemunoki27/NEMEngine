#include "SpriteRenderItemExtractor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Builtin/Render/SpriteRendererComponent.h>
#include <Engine/Core/ECS/Component/Builtin/UVTransformComponent.h>

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
		if (world.HasComponent<UVTransformComponent>(entity)) {

			uvMatrix = world.GetComponent<UVTransformComponent>(entity).uvMatrix;
		}

		// ペイロード構築
		SpriteRenderPayload payload{};
		payload.texture = renderer.texture;
		payload.size = renderer.size;
		payload.pivot = renderer.pivot;
		payload.color = renderer.color;
		payload.uvMatrix = uvMatrix;
		// 描画アイテムの構築
		RenderItem item{};
		RenderItemExtract::FillCommonFields(item, world, entity, renderer, RenderItemExtract::GetWorldMatrix(world, entity));
		item.backendID = RenderBackendID::Sprite;
		item.material = renderer.material;
		item.batchKey = renderer.texture.value;
		item.cameraDomain = RenderCameraDomain::Orthographic;
		item.payload = batch.PushPayload(payload);
		// 描画アイテムをバッチに追加
		batch.Add(std::move(item));
		});
}