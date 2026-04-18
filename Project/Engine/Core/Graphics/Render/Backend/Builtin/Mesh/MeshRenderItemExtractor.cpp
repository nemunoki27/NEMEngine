#include "MeshRenderItemExtractor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Builtin/Render/MeshRendererComponent.h>

//============================================================================
//	MeshRenderItemExtractor classMethods
//============================================================================

void Engine::MeshRenderItemExtractor::Extract(ECSWorld& world, RenderSceneBatch& batch) {

	world.ForEach<MeshRendererComponent>([&](const Entity& entity, MeshRendererComponent& renderer) {

		// 描画可能か
		if (!RenderItemExtract::IsVisible(world, entity, renderer.visible)) {
			return;
		}

		// 行列の更新
		const Matrix4x4 entityWorldMatrix = RenderItemExtract::GetWorldMatrix(world, entity);
		MeshSubMeshRuntime::UpdateRendererRuntime(renderer, entityWorldMatrix);

		// ペイロード構築
		MeshRenderPayload payload{};
		payload.mesh = renderer.mesh;
		payload.enableZPrepass = renderer.enableZPrepass;
		// 描画アイテムの構築
		RenderItem item{};
		RenderItemExtract::FillCommonFields(item, world, entity, renderer, entityWorldMatrix);
		item.backendID = RenderBackendID::Mesh;
		item.material = renderer.material;
		item.cameraDomain = RenderCameraDomain::Perspective;
		item.batchKey = renderer.mesh.value;
		item.payload = batch.PushPayload(payload);
		// 描画アイテムをバッチに追加
		batch.Add(std::move(item));
		});
}