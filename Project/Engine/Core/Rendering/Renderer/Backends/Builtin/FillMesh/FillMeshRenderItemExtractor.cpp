#include "FillMeshRenderItemExtractor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/FillFaceMeshRendererComponent.h>

//============================================================================
//	FillMeshRenderItemExtractor classMethods
//============================================================================
void Engine::FillMeshRenderItemExtractor::Extract(ECSWorld& world, RenderSceneBatch& batch) {

	world.ForEach<FillMeshRendererComponent>([&](const Entity& entity, const FillMeshRendererComponent& renderer) {

		// 描画可能か
		if (!RenderItemExtract::IsVisible(world, entity, renderer.visible)) {
			return;
		}
		// 三角形が無ければ描画しない
		if (renderer.triangleIndices.empty()) {
			return;
		}

		// ペイロード構築
		FillMeshRenderPayload payload{};
		payload.positions = &renderer.facePositions;
		payload.indices = &renderer.triangleIndices;
		payload.color = renderer.color;
		payload.materialOverrides = &renderer.parameterOverrides;

		// 描画アイテムの構築
		RenderItem item{};
		RenderItemExtract::FillCommonFields(item, world, entity, renderer, RenderItemExtract::GetWorldMatrix(world, entity));
		item.backendID = RenderBackendID::FillMesh;
		item.material = renderer.material;
		// 非インスタンシングなのでエンティティ単位で一意化する
		item.batchKey = (static_cast<uint64_t>(entity.generation) << 32) | entity.index;
		item.cameraDomain = RenderCameraDomain::Perspective;
		item.payload = batch.PushPayload(payload);
		batch.Add(std::move(item));
		});
}
