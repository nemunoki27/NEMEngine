#include "RaytracingMaterialResolver.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Assets/ShaderAsset.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterBufferBuilder.h>
#include <Engine/Core/Rendering/Materials/MaterialParameter.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>

// c++
#include <algorithm>

namespace {

	// 名前IDを優先して標準Parameterを取得
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

Engine::MeshSubMeshShaderData Engine::RaytracingMaterialResolver::BuildSubMeshData(
	GraphicsCore& graphicsCore, AssetDatabase& assetDatabase,
	const MaterialAsset& material, const MaterialParameterSet* materialInstance,
	const Matrix4x4& uvMatrix) {

	// Instanceの上書きを優先してMaterial値を解決
	const auto resolveValue = [&](MaterialParameterID id,
		MaterialParameterSemantic semantic) {

		const MaterialParameterValue* value = FindStandardMaterialParameter(materialInstance, id, semantic);
		return value ? value : FindStandardMaterialParameter(&material.parameters, id, semantic);
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

		const MaterialParameterValue* value = resolveValue(id, semantic);
		const AssetID* texture = value ? std::get_if<AssetID>(&value->value) : nullptr;
		// 空の上書きはTextureなしとして扱う
		return texture ? *texture : AssetID{};
	};
	const auto resolveTextureIndex = [&](MaterialParameterID id,
		MaterialParameterSemantic semantic, bool sRGB) {

		const AssetID texture = resolveTexture(id, semantic);
		return texture ? ResolveTextureDescriptorIndex(
			graphicsCore, assetDatabase, texture, sRGB, semantic == MaterialParameterSemantic::NormalTexture) : UINT32_MAX;
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
	data.opacityTextureIndex = resolveTextureIndex(
		MaterialParameterIDs::OpacityTexture,
		MaterialParameterSemantic::OpacityTexture, false);
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

uint32_t Engine::RaytracingMaterialResolver::AppendHitParameters(GraphicsCore& graphicsCore,
	AssetDatabase& assetDatabase, const ShaderAsset& shader, const MaterialAsset& material,
	const MaterialParameterSet* overrides, std::vector<uint32_t>& words) {

	auto [entry, inserted] = hitLayouts_.try_emplace(shader.guid);
	if (inserted) {
		// 展開済みグラフと同じ宣言順で構造体の配置を作る
		ShaderReflectionInfo reflection;
		auto& buffer = reflection.structuredBuffers.emplace_back();
		buffer.name = MaterialParameterCBuffer::kMesh;
		for (const auto& parameter : shader.parameters) {
			if (!parameter.componentCount) continue;
			const uint32_t count = parameter.componentCount;
			auto& var = buffer.variables.emplace_back();
			var.name = parameter.displayName;
			var.parameterID = parameter.id;
			var.semantic = parameter.semantic;
			var.offset = buffer.stride;
			var.size = var.declaredByteSize = count * sizeof(uint32_t);
			var.valueType = parameter.valueType;
			var.valueClass = count == 1 ? D3D_SVC_SCALAR : D3D_SVC_VECTOR;
			var.rows = 1;
			var.columns = var.declaredComponentCount = count;
			var.isColor = parameter.isColor;
			var.isTexture = parameter.isTexture;
			buffer.stride += var.size;
		}
		buffer.stride = (std::max)(buffer.stride, 4u);
		entry->second.Build(reflection, buffer.name);
	}
	const auto& layout = entry->second;
	const uint32_t count = (layout.GetSizeInBytes() + 15u) / 16u * 4u;
	if (words.size() + count > UINT32_MAX / sizeof(uint32_t)) throw std::length_error("反射用Materialが上限を超えました");
	const uint32_t offset = static_cast<uint32_t>(words.size());
	words.resize(words.size() + count);
	std::span<uint8_t> destination(reinterpret_cast<uint8_t*>(words.data() + offset), count * sizeof(uint32_t));
	static const MaterialParameterSet empty{};
	MaterialParameterBufferBuilder::BuildElementInto(destination, material.parameters, overrides ? *overrides : empty,
		layout, [&](MaterialParameterSemantic semantic, const AssetID& id) {
			return MaterialParameterBufferBuilder::TextureResolveResult{.index = ResolveTextureDescriptorIndex(
				graphicsCore, assetDatabase, id, IsSRGBMaterialTexture(semantic), semantic == MaterialParameterSemantic::NormalTexture)};
		});
	return offset * sizeof(uint32_t);
}

uint32_t Engine::RaytracingMaterialResolver::ResolveTextureDescriptorIndex(GraphicsCore& graphicsCore,
	AssetDatabase& assetDatabase, AssetID textureAssetID, bool sRGB, bool normalMap) {

	if (!textureAssetID) return UINT32_MAX;
	// Camera出力はサイズ変更とGPU状態をその都度解決する
	const auto* meta = assetDatabase.Find(textureAssetID);
	if (meta && meta->type == AssetType::RenderTexture) {

		const auto resolved = RuntimeTextureResolver::ResolveBindless(graphicsCore, &assetDatabase, textureAssetID,
			sRGB ? TextureColorSpace::SRGB : TextureColorSpace::Linear, normalMap);
		return resolved.srvIndex;
	}
	auto& descriptorCache = normalMap ? normalTextureDescriptorIndexCache_ :
		(sRGB ? sRGBTextureDescriptorIndexCache_ : textureDescriptorIndexCache_);
	if (auto it = descriptorCache.find(textureAssetID);
		it != descriptorCache.end()) {
		it->second.used = true;
		return it->second.index;
	}

	const GPUTextureResource* errorTexture = graphicsCore.GetBuiltinTextureLibrary().GetErrorTexture();
	const uint32_t errorIndex = (errorTexture && errorTexture->valid) ? errorTexture->srvIndex : UINT32_MAX;


	const auto resolve = [&]() {
		return RuntimeTextureResolver::ResolveBindless(graphicsCore, &assetDatabase, textureAssetID,
			sRGB ? TextureColorSpace::SRGB : TextureColorSpace::Linear, normalMap);
	};
	auto resolved = resolve();
	if (prepareTextures_ && resolved.retry) {
		graphicsCore.GetTextureUploadService().WaitAll();
		resolved = resolve();
	}
	hasPendingTextureDescriptors_ |= resolved.retry;
	// 読込途中のエラー色を反射へ渡さない
	if (resolved.retry) return UINT32_MAX;
	const uint32_t descriptorIndex =
		resolved.srvIndex != UINT32_MAX ? resolved.srvIndex : errorIndex;
	// 読み込み失敗はキャッシュせず次回再取得
	if (!resolved.retry && descriptorIndex != errorIndex) {
		descriptorCache[textureAssetID] = { descriptorIndex, resolved.ownership, true };
	}
	return descriptorIndex;
}

void Engine::RaytracingMaterialResolver::Clear() {

	hitLayouts_.clear();
	textureDescriptorIndexCache_.clear();
	sRGBTextureDescriptorIndexCache_.clear();
	normalTextureDescriptorIndexCache_.clear();
	hasPendingTextureDescriptors_ = false;
}

void Engine::RaytracingMaterialResolver::ResetPending(bool prepareTextures) {

	hasPendingTextureDescriptors_ = false;
	prepareTextures_ = prepareTextures;
	// 再構築中は旧SceneのTextureも保持する
	const auto reset = [](auto& cache) {

		for (auto& entry : cache) entry.second.used = false;
	};
	reset(textureDescriptorIndexCache_);
	reset(sRGBTextureDescriptorIndexCache_);
	reset(normalTextureDescriptorIndexCache_);
}

void Engine::RaytracingMaterialResolver::CommitTextures() {

	// 新しいSceneの公開後に不要な所有を解除する
	const auto collect = [](auto& cache) {

		std::erase_if(cache, [](const auto& entry) { return !entry.second.used; });
	};
	collect(textureDescriptorIndexCache_);
	collect(sRGBTextureDescriptorIndexCache_);
	collect(normalTextureDescriptorIndexCache_);
}
