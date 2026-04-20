#include "MeshDrawPathCommon.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Logger/Assert.h>

//============================================================================
//	MeshDrawPathCommon classMethods
//============================================================================

Engine::AssetID Engine::MeshDrawPathCommon::ResolveBatchMesh(const RenderSceneBatch& batch, std::span<const RenderItem* const> items) {

	AssetID resolved{};
	for (const RenderItem* item : items) {

		if (!item) {
			continue;
		}

		const MeshRenderPayload* payload = batch.GetPayload<MeshRenderPayload>(*item);
		if (!payload || !payload->mesh) {
			continue;
		}
		if (!resolved) {

			// 先頭の有効メッシュIDを使用する
			resolved = payload->mesh;
			continue;
		}

		// バッチ内に異なるメッシュが混ざっていたらエラー
		Assert::Call(resolved == payload->mesh, "ResolveBatchMesh: mixed mesh assets in one batch.");
	}
	return resolved;
}

Engine::AssetID Engine::MeshDrawPathCommon::ResolveSubMeshBaseColorTextureAssetID(const MeshGPUResource& gpuMesh,
	const MeshRendererComponent* renderer, uint32_t subMeshIndex) {

	AssetID resolved{};

	if (subMeshIndex < gpuMesh.subMeshes.size()) {

		resolved = gpuMesh.subMeshes[subMeshIndex].defaultTextureAssets.baseColorTexture;
	}

	// レンダラーに描画テクスチャが設定されていれば使用する
	if (renderer && subMeshIndex < renderer->subMeshes.size()) {
		if (renderer->subMeshes[subMeshIndex].baseColorTexture) {

			resolved = renderer->subMeshes[subMeshIndex].baseColorTexture;
		}
	}
	return resolved;
}