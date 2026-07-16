#include "SpriteRenderItemExtractor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/UVTransformComponent.h>

//============================================================================
//	SpriteRenderItemExtractor internal
//============================================================================
namespace {

	// baseColorTextureだけを上書きするSpriteは同じテクスチャごとにまとめる
	uint64_t ResolveBatchKey(const Engine::Entity& entity,
		const std::unordered_map<std::string, Engine::MaterialParameterValue>& parameters) {

		if (parameters.empty()) {
			return 0;
		}
		if (parameters.size() == 1) {

			const auto textureIt = parameters.find("baseColorTexture");
			if (textureIt != parameters.end()) {

				if (const Engine::AssetID* textureID = std::get_if<Engine::AssetID>(&textureIt->second.value)) {
					return textureID->value;
				}
			}
		}
		return (static_cast<uint64_t>(entity.generation) << 32) | entity.index;
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
		// 個別マテリアルパラメータはコンポーネントのmapを指す、描画時に既定値へ重ねる
		payload.materialOverrides = &renderer.parameterOverrides;
		// 描画アイテムの構築
		RenderItem item{};
		RenderItemExtract::FillCommonFields(item, world, entity, renderer, RenderItemExtract::GetWorldMatrix(world, entity));
		item.backendID = RenderBackendID::Sprite;
		item.material = renderer.material;
		// baseColorTextureだけの上書きは同じテクスチャでまとめ、それ以外の上書きは単独描画にする
		item.batchKey = ResolveBatchKey(entity, renderer.parameterOverrides);
		item.cameraDomain = RenderCameraDomain::Orthographic;
		item.payload = batch.PushPayload(payload);
		// 描画アイテムをバッチに追加
		batch.Add(std::move(item));
		});
}
