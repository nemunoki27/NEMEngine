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

	Engine::MeshSubMeshRenderState ResolveSubMeshRenderState(
		const Engine::MeshRendererComponent& renderer,
		const Engine::SubMeshMaterial& subMesh) {

		Engine::MeshSubMeshRenderState state{};
		state.material = subMesh.material ?
			subMesh.material : renderer.material;
		state.surfaceModeOverridden =
			subMesh.surfaceMode != Engine::MaterialSurfaceMode::Auto;
		state.surfaceMode = state.surfaceModeOverridden ?
			subMesh.surfaceMode : subMesh.sourceSurfaceMode;
		if (state.surfaceMode == Engine::MaterialSurfaceMode::Auto) {
			state.surfaceMode = renderer.queue == Engine::RenderPhase::Transparent ?
				Engine::MaterialSurfaceMode::Transparent :
				Engine::MaterialSurfaceMode::Opaque;
		}
		return state;
	}

	bool IsSameRenderState(
		const Engine::MeshSubMeshRenderState& lhs,
		const Engine::MeshSubMeshRenderState& rhs) {

		return lhs.material == rhs.material &&
			lhs.surfaceMode == rhs.surfaceMode &&
			lhs.surfaceModeOverridden == rhs.surfaceModeOverridden;
	}

	// サブメッシュのテクスチャparamを取り出す、上書きが無くオーサリング確定済みならテクスチャなし扱い
	Engine::AssetID ResolveSubMeshTextureParam(
		std::span<const Engine::SubMeshMaterial> subMeshes, uint32_t subMeshIndex,
		Engine::MaterialParameterID parameterID,
		const Engine::AssetID& modelDefault) {

		if (subMeshIndex < subMeshes.size()) {

			const auto& params = subMeshes[subMeshIndex].materialInstance;
			const Engine::MaterialParameterValue* value =
				params.Find(parameterID);
			if (value &&
				std::holds_alternative<Engine::AssetID>(
					value->value)) {

				return std::get<Engine::AssetID>(
					value->value);
			}
			if (subMeshes[subMeshIndex].stableID) {
				return {};
			}
		}
		return modelDefault;
	}
}

//============================================================================
//	MeshDrawPathCommon classMethods
//============================================================================
void Engine::MeshDrawPathCommon::BuildSubMeshRenderGroups(
	const MeshRendererComponent& renderer,
	std::span<const SubMeshMaterial> subMeshes,
	std::vector<MeshSubMeshRenderState>& outGroups,
	std::vector<uint32_t>& outGroupIndices) {

	outGroups.clear();
	outGroupIndices.clear();
	outGroups.reserve(subMeshes.size());
	outGroupIndices.reserve(subMeshes.size());

	for (const SubMeshMaterial& subMesh : subMeshes) {
		const MeshSubMeshRenderState state =
			ResolveSubMeshRenderState(renderer, subMesh);
		uint32_t groupIndex = 0;
		for (; groupIndex < static_cast<uint32_t>(outGroups.size());
			++groupIndex) {

			if (IsSameRenderState(outGroups[groupIndex], state)) {
				break;
			}
		}
		if (groupIndex == outGroups.size()) {
			outGroups.emplace_back(state);
		}
		outGroupIndices.emplace_back(groupIndex);
	}
}

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
	std::span<const SubMeshMaterial> subMeshes, uint32_t subMeshIndex) {

	const AssetID modelDefault = subMeshIndex < gpuMesh.subMeshes.size() ?
		gpuMesh.subMeshes[subMeshIndex].defaultTextureAssets.baseColorTexture : AssetID{};
	return ResolveSubMeshTextureParam(subMeshes, subMeshIndex,
		MaterialParameterIDs::BaseColorTexture, modelDefault);
}

Engine::AssetID Engine::MeshDrawPathCommon::ResolveSubMeshNormalTextureAssetID(const MeshGPUResource& gpuMesh,
	std::span<const SubMeshMaterial> subMeshes, uint32_t subMeshIndex) {

	const AssetID modelDefault = subMeshIndex < gpuMesh.subMeshes.size() ?
		gpuMesh.subMeshes[subMeshIndex].defaultTextureAssets.normalTexture : AssetID{};
	return ResolveSubMeshTextureParam(subMeshes, subMeshIndex,
		MaterialParameterIDs::NormalTexture, modelDefault);
}

Engine::AssetID Engine::MeshDrawPathCommon::ResolveSubMeshMetallicRoughnessTextureAssetID(const MeshGPUResource& gpuMesh,
	std::span<const SubMeshMaterial> subMeshes, uint32_t subMeshIndex) {

	const AssetID modelDefault = subMeshIndex < gpuMesh.subMeshes.size() ?
		gpuMesh.subMeshes[subMeshIndex].defaultTextureAssets.metallicRoughnessTexture : AssetID{};
	return ResolveSubMeshTextureParam(subMeshes, subMeshIndex,
		MaterialParameterIDs::MetallicRoughnessTexture, modelDefault);
}

Engine::AssetID Engine::MeshDrawPathCommon::ResolveSubMeshMetallicTextureAssetID(
	const MeshGPUResource& gpuMesh, std::span<const SubMeshMaterial> subMeshes,
	uint32_t subMeshIndex) {

	const AssetID modelDefault = subMeshIndex < gpuMesh.subMeshes.size() ?
		gpuMesh.subMeshes[subMeshIndex].defaultTextureAssets.metallicTexture : AssetID{};
	return ResolveSubMeshTextureParam(subMeshes, subMeshIndex,
		MaterialParameterIDs::MetallicTexture, modelDefault);
}

Engine::AssetID Engine::MeshDrawPathCommon::ResolveSubMeshRoughnessTextureAssetID(
	const MeshGPUResource& gpuMesh, std::span<const SubMeshMaterial> subMeshes,
	uint32_t subMeshIndex) {

	const AssetID modelDefault = subMeshIndex < gpuMesh.subMeshes.size() ?
		gpuMesh.subMeshes[subMeshIndex].defaultTextureAssets.roughnessTexture : AssetID{};
	return ResolveSubMeshTextureParam(subMeshes, subMeshIndex,
		MaterialParameterIDs::RoughnessTexture, modelDefault);
}

Engine::AssetID Engine::MeshDrawPathCommon::ResolveSubMeshDisplacementTextureAssetID(
	const MeshGPUResource& gpuMesh, std::span<const SubMeshMaterial> subMeshes,
	uint32_t subMeshIndex) {

	const AssetID modelDefault = subMeshIndex < gpuMesh.subMeshes.size() ?
		gpuMesh.subMeshes[subMeshIndex].defaultTextureAssets.displacementTexture : AssetID{};
	return ResolveSubMeshTextureParam(subMeshes, subMeshIndex,
		MaterialParameterIDs::DisplacementTexture, modelDefault);
}

Engine::AssetID Engine::MeshDrawPathCommon::ResolveSubMeshEmissiveTextureAssetID(const MeshGPUResource& gpuMesh,
	std::span<const SubMeshMaterial> subMeshes, uint32_t subMeshIndex) {

	const AssetID modelDefault = subMeshIndex < gpuMesh.subMeshes.size() ?
		gpuMesh.subMeshes[subMeshIndex].defaultTextureAssets.emissiveTexture : AssetID{};
	return ResolveSubMeshTextureParam(subMeshes, subMeshIndex,
		MaterialParameterIDs::EmissiveTexture, modelDefault);
}

Engine::AssetID Engine::MeshDrawPathCommon::ResolveSubMeshOcclusionTextureAssetID(const MeshGPUResource& gpuMesh,
	std::span<const SubMeshMaterial> subMeshes, uint32_t subMeshIndex) {

	const AssetID modelDefault = subMeshIndex < gpuMesh.subMeshes.size() ?
		gpuMesh.subMeshes[subMeshIndex].defaultTextureAssets.occlusionTexture : AssetID{};
	return ResolveSubMeshTextureParam(subMeshes, subMeshIndex,
		MaterialParameterIDs::AmbientOcclusionTexture, modelDefault);
}

Engine::AssetID Engine::MeshDrawPathCommon::ResolveSubMeshSpecularTextureAssetID(const MeshGPUResource& gpuMesh,
	std::span<const SubMeshMaterial> subMeshes, uint32_t subMeshIndex) {

	const AssetID modelDefault = subMeshIndex < gpuMesh.subMeshes.size() ?
		gpuMesh.subMeshes[subMeshIndex].defaultTextureAssets.specularTexture : AssetID{};
	return ResolveSubMeshTextureParam(subMeshes, subMeshIndex,
		MaterialParameterIDs::SpecularTexture, modelDefault);
}

bool Engine::MeshDrawPathCommon::WasSubMeshBaseColorTextureAssigned(const MeshGPUResource& gpuMesh,
	std::span<const SubMeshMaterial> subMeshes, uint32_t subMeshIndex) {

	// オーサリング確定済みはmaterialInstanceのbaseColorTextureが権威で空ならテクスチャなし扱い
	if (subMeshIndex < subMeshes.size()) {
		if (subMeshes[subMeshIndex].stableID) {

			const auto& params = subMeshes[subMeshIndex].materialInstance;
			const MaterialParameterValue* value =
				params.Find(
					MaterialParameterIDs::
						BaseColorTexture);
			return value &&
				std::holds_alternative<AssetID>(
					value->value) &&
				static_cast<bool>(
					std::get<AssetID>(
						value->value));
		}
	}

	// インポート時にマテリアルがベースカラーテクスチャを宣言していたか(見つからなくてもtrue)
	if (subMeshIndex < gpuMesh.subMeshes.size()) {
		return gpuMesh.subMeshes[subMeshIndex].hasBaseColorTexture;
	}
	return false;
}
