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

	// オーサリングが確定済みのサブメッシュはその値を権威として使う（空=テクスチャなし）
	if (renderer && subMeshIndex < renderer->subMeshes.size()) {
		if (renderer->subMeshes[subMeshIndex].stableID) {
			return renderer->subMeshes[subMeshIndex].baseColorTexture;
		}
	}

	if (subMeshIndex < gpuMesh.subMeshes.size()) {
		return gpuMesh.subMeshes[subMeshIndex].defaultTextureAssets.baseColorTexture;
	}
	return {};
}

Engine::AssetID Engine::MeshDrawPathCommon::ResolveSubMeshNormalTextureAssetID(const MeshGPUResource& gpuMesh,
	const MeshRendererComponent* renderer, uint32_t subMeshIndex) {

	if (renderer && subMeshIndex < renderer->subMeshes.size()) {
		if (renderer->subMeshes[subMeshIndex].stableID) {
			return renderer->subMeshes[subMeshIndex].normalTexture;
		}
	}

	if (subMeshIndex < gpuMesh.subMeshes.size()) {
		return gpuMesh.subMeshes[subMeshIndex].defaultTextureAssets.normalTexture;
	}
	return {};
}

Engine::AssetID Engine::MeshDrawPathCommon::ResolveSubMeshMetallicRoughnessTextureAssetID(const MeshGPUResource& gpuMesh,
	const MeshRendererComponent* renderer, uint32_t subMeshIndex) {

	if (renderer && subMeshIndex < renderer->subMeshes.size()) {
		if (renderer->subMeshes[subMeshIndex].stableID) {
			return renderer->subMeshes[subMeshIndex].metallicRoughnessTexture;
		}
	}

	if (subMeshIndex < gpuMesh.subMeshes.size()) {
		return gpuMesh.subMeshes[subMeshIndex].defaultTextureAssets.metallicRoughnessTexture;
	}
	return {};
}

Engine::AssetID Engine::MeshDrawPathCommon::ResolveSubMeshEmissiveTextureAssetID(const MeshGPUResource& gpuMesh,
	const MeshRendererComponent* renderer, uint32_t subMeshIndex) {

	if (renderer && subMeshIndex < renderer->subMeshes.size()) {
		if (renderer->subMeshes[subMeshIndex].stableID) {
			return renderer->subMeshes[subMeshIndex].emissiveTexture;
		}
	}

	if (subMeshIndex < gpuMesh.subMeshes.size()) {
		return gpuMesh.subMeshes[subMeshIndex].defaultTextureAssets.emissiveTexture;
	}
	return {};
}

Engine::AssetID Engine::MeshDrawPathCommon::ResolveSubMeshOcclusionTextureAssetID(const MeshGPUResource& gpuMesh,
	const MeshRendererComponent* renderer, uint32_t subMeshIndex) {

	if (renderer && subMeshIndex < renderer->subMeshes.size()) {
		if (renderer->subMeshes[subMeshIndex].stableID) {
			return renderer->subMeshes[subMeshIndex].occlusionTexture;
		}
	}

	if (subMeshIndex < gpuMesh.subMeshes.size()) {
		return gpuMesh.subMeshes[subMeshIndex].defaultTextureAssets.occlusionTexture;
	}
	return {};
}

Engine::AssetID Engine::MeshDrawPathCommon::ResolveSubMeshSpecularTextureAssetID(const MeshGPUResource& gpuMesh,
	const MeshRendererComponent* renderer, uint32_t subMeshIndex) {

	if (renderer && subMeshIndex < renderer->subMeshes.size()) {
		if (renderer->subMeshes[subMeshIndex].stableID) {
			return renderer->subMeshes[subMeshIndex].specularTexture;
		}
	}

	if (subMeshIndex < gpuMesh.subMeshes.size()) {
		return gpuMesh.subMeshes[subMeshIndex].defaultTextureAssets.specularTexture;
	}
	return {};
}

bool Engine::MeshDrawPathCommon::WasSubMeshBaseColorTextureAssigned(const MeshGPUResource& gpuMesh,
	const MeshRendererComponent* renderer, uint32_t subMeshIndex) {

	// オーサリング確定済みはAssetIDが権威。空ならユーザーがテクスチャなしにした扱い
	if (renderer && subMeshIndex < renderer->subMeshes.size()) {
		if (renderer->subMeshes[subMeshIndex].stableID) {
			return static_cast<bool>(renderer->subMeshes[subMeshIndex].baseColorTexture);
		}
	}

	// インポート時にマテリアルがベースカラーテクスチャを宣言していたか(見つからなくてもtrue)
	if (subMeshIndex < gpuMesh.subMeshes.size()) {
		return gpuMesh.subMeshes[subMeshIndex].hasBaseColorTexture;
	}
	return false;
}