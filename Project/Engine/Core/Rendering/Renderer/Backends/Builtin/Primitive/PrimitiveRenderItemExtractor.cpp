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
		payload.materialInstance = &renderer.materialInstance.Get();

		// 描画アイテムの構築
		RenderItem item{};
		RenderItemExtract::FillCommonFields(item, world, entity, renderer, RenderItemExtract::GetWorldMatrix(world, entity));
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
		// Plane/Ringのみ2D描画に対応し、正射投影のScreenUIフェーズへ流す
		if (IsPrimitiveScreen2D(renderer)) {
			item.cameraDomain = RenderCameraDomain::Orthographic;
			item.renderPhase = RenderPhase::ScreenUI;
		}
		item.payload = batch.PushPayload(payload);
		batch.Add(std::move(item));
		});
}
