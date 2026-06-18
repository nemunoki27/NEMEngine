#include "SpriteRenderItemExtractor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/UVTransformComponent.h>

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
		payload.texture = renderer.texture;
		payload.size = renderer.size;
		payload.pivot = renderer.pivot;
		payload.color = renderer.color;
		payload.uvMatrix = uvMatrix;
		// 個別マテリアルパラメータはコンポーネントのmapを指す、描画時に既定値へ重ねる
		payload.materialOverrides = &renderer.parameterOverrides;
		// 描画アイテムの構築
		RenderItem item{};
		RenderItemExtract::FillCommonFields(item, world, entity, renderer, RenderItemExtract::GetWorldMatrix(world, entity));
		item.backendID = RenderBackendID::Sprite;
		item.material = renderer.material;
		// 個別マテリアルパラメータを持つアイテムは専用cbufferが要るので、エンティティ単位で一意化して単独描画にする
		item.batchKey = renderer.parameterOverrides.empty() ? renderer.texture.value :
			((static_cast<uint64_t>(entity.generation) << 32) | entity.index);
		item.cameraDomain = RenderCameraDomain::Orthographic;
		item.payload = batch.PushPayload(payload);
		// 描画アイテムをバッチに追加
		batch.Add(std::move(item));
		});
}
