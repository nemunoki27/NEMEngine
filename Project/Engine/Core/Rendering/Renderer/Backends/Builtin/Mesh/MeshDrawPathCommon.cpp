#include "MeshDrawPathCommon.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>

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

Engine::AssetID Engine::MeshDrawPathCommon::ResolveSubMeshNormalTextureAssetID(const MeshGPUResource& gpuMesh,
	const MeshRendererComponent* renderer, uint32_t subMeshIndex) {

	AssetID resolved{};

	if (subMeshIndex < gpuMesh.subMeshes.size()) {

		resolved = gpuMesh.subMeshes[subMeshIndex].defaultTextureAssets.normalTexture;
	}

	if (renderer && subMeshIndex < renderer->subMeshes.size()) {
		if (renderer->subMeshes[subMeshIndex].normalTexture) {

			resolved = renderer->subMeshes[subMeshIndex].normalTexture;
		}
	}
	return resolved;
}

Engine::AssetID Engine::MeshDrawPathCommon::ResolveSubMeshMetallicRoughnessTextureAssetID(const MeshGPUResource& gpuMesh,
	const MeshRendererComponent* renderer, uint32_t subMeshIndex) {

	AssetID resolved{};

	if (subMeshIndex < gpuMesh.subMeshes.size()) {

		resolved = gpuMesh.subMeshes[subMeshIndex].defaultTextureAssets.metallicRoughnessTexture;
	}

	if (renderer && subMeshIndex < renderer->subMeshes.size()) {
		if (renderer->subMeshes[subMeshIndex].metallicRoughnessTexture) {

			resolved = renderer->subMeshes[subMeshIndex].metallicRoughnessTexture;
		}
	}
	return resolved;
}

Engine::AssetID Engine::MeshDrawPathCommon::ResolveSubMeshEmissiveTextureAssetID(const MeshGPUResource& gpuMesh,
	const MeshRendererComponent* renderer, uint32_t subMeshIndex) {

	AssetID resolved{};

	if (subMeshIndex < gpuMesh.subMeshes.size()) {

		resolved = gpuMesh.subMeshes[subMeshIndex].defaultTextureAssets.emissiveTexture;
	}

	if (renderer && subMeshIndex < renderer->subMeshes.size()) {
		if (renderer->subMeshes[subMeshIndex].emissiveTexture) {

			resolved = renderer->subMeshes[subMeshIndex].emissiveTexture;
		}
	}
	return resolved;
}

Engine::AssetID Engine::MeshDrawPathCommon::ResolveSubMeshOcclusionTextureAssetID(const MeshGPUResource& gpuMesh,
	const MeshRendererComponent* renderer, uint32_t subMeshIndex) {

	AssetID resolved{};

	if (subMeshIndex < gpuMesh.subMeshes.size()) {

		resolved = gpuMesh.subMeshes[subMeshIndex].defaultTextureAssets.occlusionTexture;
	}

	if (renderer && subMeshIndex < renderer->subMeshes.size()) {
		if (renderer->subMeshes[subMeshIndex].occlusionTexture) {

			resolved = renderer->subMeshes[subMeshIndex].occlusionTexture;
		}
	}
	return resolved;
}

Engine::AssetID Engine::MeshDrawPathCommon::ResolveSubMeshSpecularTextureAssetID(const MeshGPUResource& gpuMesh,
	const MeshRendererComponent* renderer, uint32_t subMeshIndex) {

	AssetID resolved{};

	if (subMeshIndex < gpuMesh.subMeshes.size()) {

		resolved = gpuMesh.subMeshes[subMeshIndex].defaultTextureAssets.specularTexture;
	}

	if (renderer && subMeshIndex < renderer->subMeshes.size()) {
		if (renderer->subMeshes[subMeshIndex].specularTexture) {

			resolved = renderer->subMeshes[subMeshIndex].specularTexture;
		}
	}
	return resolved;
}