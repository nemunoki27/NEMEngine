#include "PrimitiveRenderItemExtractor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/UVTransformComponent.h>
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
		payload.materialOverrides = &renderer.parameterOverrides;

		// 描画アイテムの構築
		RenderItem item{};
		RenderItemExtract::FillCommonFields(item, world, entity, renderer, RenderItemExtract::GetWorldMatrix(world, entity));
		item.backendID = RenderBackendID::Primitive;
		item.material = renderer.material;
		// 同一形状はまとめてインスタンシングするが、マテリアル上書きを持つものは
		// バッチ先頭の上書きしか反映されないためエンティティ単位で分離して個別描画にする
		const uint64_t shapeHash = PrimitiveMeshGenerator::ComputeHash(renderer);
		item.batchKey = renderer.parameterOverrides.empty() ?
			shapeHash : (shapeHash ^ (static_cast<uint64_t>(entity.index + 1) * 0x9E3779B97F4A7C15ull));
		item.cameraDomain = RenderCameraDomain::Perspective;
		item.payload = batch.PushPayload(payload);
		batch.Add(std::move(item));
		});
}
