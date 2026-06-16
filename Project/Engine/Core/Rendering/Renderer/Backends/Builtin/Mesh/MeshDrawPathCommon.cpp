#include "MeshDrawPathCommon.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>

// c++
#include <variant>

//============================================================================
//	MeshDrawPathCommon internal
//============================================================================
namespace {

	// サブメッシュのテクスチャparamを取り出す、上書きが無くオーサリング確定済みならテクスチャなし扱い
	Engine::AssetID ResolveSubMeshTextureParam(const Engine::MeshRendererComponent* renderer, uint32_t subMeshIndex,
		const char* paramName, const Engine::AssetID& modelDefault) {

		if (renderer && subMeshIndex < renderer->subMeshes.size()) {

			const auto& params = renderer->subMeshes[subMeshIndex].parameterOverrides;
			auto it = params.find(paramName);
			if (it != params.end() && std::holds_alternative<Engine::AssetID>(it->second.value)) {
				return std::get<Engine::AssetID>(it->second.value);
			}
			if (renderer->subMeshes[subMeshIndex].stableID) {
				return {};
			}
		}
		return modelDefault;
	}
}

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

	const AssetID modelDefault = subMeshIndex < gpuMesh.subMeshes.size() ?
		gpuMesh.subMeshes[subMeshIndex].defaultTextureAssets.baseColorTexture : AssetID{};
	return ResolveSubMeshTextureParam(renderer, subMeshIndex, "baseColorTexture", modelDefault);
}

Engine::AssetID Engine::MeshDrawPathCommon::ResolveSubMeshNormalTextureAssetID(const MeshGPUResource& gpuMesh,
	const MeshRendererComponent* renderer, uint32_t subMeshIndex) {

	const AssetID modelDefault = subMeshIndex < gpuMesh.subMeshes.size() ?
		gpuMesh.subMeshes[subMeshIndex].defaultTextureAssets.normalTexture : AssetID{};
	return ResolveSubMeshTextureParam(renderer, subMeshIndex, "normalTexture", modelDefault);
}

Engine::AssetID Engine::MeshDrawPathCommon::ResolveSubMeshMetallicRoughnessTextureAssetID(const MeshGPUResource& gpuMesh,
	const MeshRendererComponent* renderer, uint32_t subMeshIndex) {

	const AssetID modelDefault = subMeshIndex < gpuMesh.subMeshes.size() ?
		gpuMesh.subMeshes[subMeshIndex].defaultTextureAssets.metallicRoughnessTexture : AssetID{};
	return ResolveSubMeshTextureParam(renderer, subMeshIndex, "metallicRoughnessTexture", modelDefault);
}

Engine::AssetID Engine::MeshDrawPathCommon::ResolveSubMeshEmissiveTextureAssetID(const MeshGPUResource& gpuMesh,
	const MeshRendererComponent* renderer, uint32_t subMeshIndex) {

	const AssetID modelDefault = subMeshIndex < gpuMesh.subMeshes.size() ?
		gpuMesh.subMeshes[subMeshIndex].defaultTextureAssets.emissiveTexture : AssetID{};
	return ResolveSubMeshTextureParam(renderer, subMeshIndex, "emissiveTexture", modelDefault);
}

Engine::AssetID Engine::MeshDrawPathCommon::ResolveSubMeshOcclusionTextureAssetID(const MeshGPUResource& gpuMesh,
	const MeshRendererComponent* renderer, uint32_t subMeshIndex) {

	const AssetID modelDefault = subMeshIndex < gpuMesh.subMeshes.size() ?
		gpuMesh.subMeshes[subMeshIndex].defaultTextureAssets.occlusionTexture : AssetID{};
	return ResolveSubMeshTextureParam(renderer, subMeshIndex, "occlusionTexture", modelDefault);
}

Engine::AssetID Engine::MeshDrawPathCommon::ResolveSubMeshSpecularTextureAssetID(const MeshGPUResource& gpuMesh,
	const MeshRendererComponent* renderer, uint32_t subMeshIndex) {

	const AssetID modelDefault = subMeshIndex < gpuMesh.subMeshes.size() ?
		gpuMesh.subMeshes[subMeshIndex].defaultTextureAssets.specularTexture : AssetID{};
	return ResolveSubMeshTextureParam(renderer, subMeshIndex, "specularTexture", modelDefault);
}

bool Engine::MeshDrawPathCommon::WasSubMeshBaseColorTextureAssigned(const MeshGPUResource& gpuMesh,
	const MeshRendererComponent* renderer, uint32_t subMeshIndex) {

	// オーサリング確定済みはparameterOverridesのbaseColorTextureが権威で空ならテクスチャなし扱い
	if (renderer && subMeshIndex < renderer->subMeshes.size()) {
		if (renderer->subMeshes[subMeshIndex].stableID) {

			const auto& params = renderer->subMeshes[subMeshIndex].parameterOverrides;
			auto it = params.find("baseColorTexture");
			return it != params.end() && std::holds_alternative<AssetID>(it->second.value) &&
				static_cast<bool>(std::get<AssetID>(it->second.value));
		}
	}

	// インポート時にマテリアルがベースカラーテクスチャを宣言していたか(見つからなくてもtrue)
	if (subMeshIndex < gpuMesh.subMeshes.size()) {
		return gpuMesh.subMeshes[subMeshIndex].hasBaseColorTexture;
	}
	return false;
}