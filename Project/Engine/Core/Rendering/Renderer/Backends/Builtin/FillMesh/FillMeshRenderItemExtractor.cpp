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

		const std::span<const FillMeshPosition> positions =
			GetFillMeshPositions(world, entity);
		const std::span<const FillMeshTriangleIndex> indices =
			GetFillMeshTriangleIndices(world, entity);
		// 描画可能か
		if (!RenderItemExtract::IsVisible(world, entity, renderer.visible)) {
			return;
		}
		// 三角形が無ければ描画しない
		if (indices.empty()) {
			return;
		}

		// DynamicBufferの連続領域を抽出フレーム中だけ参照する
		FillMeshRenderPayload payload{};
		payload.positions = positions.data();
		payload.positionCount = static_cast<uint32_t>(positions.size());
		payload.indices = indices.data();
		payload.indexCount = static_cast<uint32_t>(indices.size());
		payload.color = renderer.color;
		payload.materialOverrides = &renderer.parameterOverrides.Get();

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
