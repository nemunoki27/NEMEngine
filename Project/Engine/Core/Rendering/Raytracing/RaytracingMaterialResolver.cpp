#include "RaytracingMaterialResolver.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Materials/MaterialParameter.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>

namespace {
	const Engine::MaterialParameterValue* FindStandardMaterialParameter(
		const Engine::MaterialParameterSet* parameters,
		Engine::MaterialParameterID id,
		Engine::MaterialParameterSemantic semantic) {

		if (!parameters) {
			return nullptr;
		}
		if (const Engine::MaterialParameterValue* value = parameters->Find(id)) {
			return value;
		}
		return semantic != Engine::MaterialParameterSemantic::None ?
			parameters->Find(semantic) : nullptr;
	}
}

Engine::MeshSubMeshShaderData Engine::RaytracingMaterialResolver::BuildPrimitiveSubMeshData(
	GraphicsCore& graphicsCore, AssetDatabase& assetDatabase,
	const MaterialAsset& material, const MaterialParameterSet* materialInstance,
	const Matrix4x4& uvMatrix) {

	const auto resolveValue = [&](MaterialParameterID id,
		MaterialParameterSemantic semantic) {

		const MaterialParameterValue* value = FindStandardMaterialParameter(
			materialInstance, id, semantic);
		return value ? value : FindStandardMaterialParameter(
			&material.parameters, id, semantic);
	};
	const auto resolveColor = [&](MaterialParameterID id,
		MaterialParameterSemantic semantic, const Color4& fallback) {

		const MaterialParameterValue* value = resolveValue(id, semantic);
		const Color4* color = value ? std::get_if<Color4>(&value->value) : nullptr;
		return color ? *color : fallback;
	};
	const auto resolveFloat = [&](MaterialParameterID id,
		MaterialParameterSemantic semantic, float fallback) {

		const MaterialParameterValue* value = resolveValue(id, semantic);
		const float* number = value ? std::get_if<float>(&value->value) : nullptr;
		return number ? *number : fallback;
	};
	const auto resolveTexture = [&](MaterialParameterID id,
		MaterialParameterSemantic semantic) {

		const MaterialParameterValue* value = FindStandardMaterialParameter(
			materialInstance, id, semantic);
		const AssetID* texture = value ? std::get_if<AssetID>(&value->value) : nullptr;
		if (texture && *texture) {
			return *texture;
		}
		value = FindStandardMaterialParameter(&material.parameters, id, semantic);
		texture = value ? std::get_if<AssetID>(&value->value) : nullptr;
		return texture ? *texture : AssetID{};
	};
	const auto resolveTextureIndex = [&](MaterialParameterID id,
		MaterialParameterSemantic semantic, bool sRGB) {

		const AssetID texture = resolveTexture(id, semantic);
		return texture ? ResolveTextureDescriptorIndex(
			graphicsCore, assetDatabase, texture, sRGB) : UINT32_MAX;
	};

	MeshSubMeshShaderData data{};
	data.baseColorTextureIndex = resolveTextureIndex(
		MaterialParameterIDs::BaseColorTexture,
		MaterialParameterSemantic::BaseColorTexture, true);
	data.normalTextureIndex = resolveTextureIndex(
		MaterialParameterIDs::NormalTexture,
		MaterialParameterSemantic::NormalTexture, false);
	data.metallicRoughnessTextureIndex = resolveTextureIndex(
		MaterialParameterIDs::MetallicRoughnessTexture,
		MaterialParameterSemantic::MetallicRoughnessTexture, false);
	data.emissiveTextureIndex = resolveTextureIndex(
		MaterialParameterIDs::EmissiveTexture,
		MaterialParameterSemantic::EmissiveTexture, true);
	data.occlusionTextureIndex = resolveTextureIndex(
		MaterialParameterIDs::AmbientOcclusionTexture,
		MaterialParameterSemantic::AmbientOcclusionTexture, false);
	data.specularTextureIndex = resolveTextureIndex(
		MaterialParameterIDs::SpecularTexture,
		MaterialParameterSemantic::None, false);
	data.metallicTextureIndex = resolveTextureIndex(
		MaterialParameterIDs::MetallicTexture,
		MaterialParameterSemantic::MetallicTexture, false);
	data.roughnessTextureIndex = resolveTextureIndex(
		MaterialParameterIDs::RoughnessTexture,
		MaterialParameterSemantic::RoughnessTexture, false);

	data.importedBaseColor = Color4::White();
	data.color = resolveColor(MaterialParameterIDs::BaseColor,
		MaterialParameterSemantic::BaseColor, Color4::White());
	data.emissiveColor = resolveColor(MaterialParameterIDs::EmissiveColor,
		MaterialParameterSemantic::EmissiveColor,
		Color4(0.0f, 0.0f, 0.0f, 0.0f));
	data.emissiveColor.a = resolveFloat(
		MaterialParameterIDs::EmissiveIntensity,
		MaterialParameterSemantic::EmissiveIntensity, 1.0f);
	data.metallic = resolveFloat(MaterialParameterIDs::Metallic,
		MaterialParameterSemantic::Metallic, 0.0f);
	data.roughness = resolveFloat(MaterialParameterIDs::Roughness,
		MaterialParameterSemantic::Roughness, 0.5f);
	data.uvMatrix = uvMatrix;
	return data;
}

uint32_t Engine::RaytracingMaterialResolver::ResolveTextureDescriptorIndex(GraphicsCore& graphicsCore,
	AssetDatabase& assetDatabase, AssetID textureAssetID, bool sRGB) {

	auto& descriptorCache = sRGB ?
		sRGBTextureDescriptorIndexCache_ : textureDescriptorIndexCache_;
	if (auto it = descriptorCache.find(textureAssetID);
		it != descriptorCache.end()) {
		return it->second;
	}

	const GPUTextureResource* errorTexture = graphicsCore.GetBuiltinTextureLibrary().GetErrorTexture();
	const uint32_t errorIndex = (errorTexture && errorTexture->valid) ? errorTexture->srvIndex : 0;

	if (!textureAssetID) {
		return UINT32_MAX;
	}

	const RuntimeTextureResolver::BindlessResolveResult resolved =
		RuntimeTextureResolver::ResolveBindless(
			graphicsCore, &assetDatabase, textureAssetID,
			sRGB ? TextureColorSpace::SRGB : TextureColorSpace::Linear);
	hasPendingTextureDescriptors_ |= resolved.retry;
	const uint32_t descriptorIndex =
		resolved.srvIndex != UINT32_MAX ? resolved.srvIndex : errorIndex;
	// 失敗時のErrorTextureは保持せず、次のシーン差分更新で復旧できるようにする
	if (!resolved.retry && descriptorIndex != errorIndex) {
		descriptorCache[textureAssetID] = descriptorIndex;
	}
	return descriptorIndex;
}

void Engine::RaytracingMaterialResolver::Clear() {

	textureDescriptorIndexCache_.clear();
	sRGBTextureDescriptorIndexCache_.clear();
	hasPendingTextureDescriptors_ = false;
}
