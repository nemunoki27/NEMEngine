#include "MaterialParameterBinder.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Materials/MaterialParameterBufferBuilder.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>

// c++
#include <algorithm>
#include <functional>

//============================================================================
//	MaterialParameterBinder classMethods
//============================================================================
namespace {

	size_t HashCombine(size_t seed, size_t value) {

		return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
	}
}

size_t Engine::MaterialParameterBinder::CacheKeyHasher::operator()(const CacheKey& key) const noexcept {

	size_t result = std::hash<uint64_t>{}(key.pipelineID);
	result = HashCombine(result, std::hash<const void*>{}(key.material));
	result = HashCombine(result, std::hash<const void*>{}(key.overrides));
	return result;
}

void Engine::MaterialParameterBinder::BeginFrame() {

	allocator_.BeginFrame();
	++frameIndex_;

	// ECS移動などで参照されなくなったアドレスのキャッシュを定期的に破棄する
	constexpr uint64_t kCacheRetainFrames = 300;
	if (frameIndex_ % kCacheRetainFrames == 0) {

		std::erase_if(bindingCache_, [&](const auto& entry) {
			return frameIndex_ - entry.second.lastUsedFrame >= kCacheRetainFrames;
			});
	}
}

void Engine::MaterialParameterBinder::Release() {

	allocator_.Release();
	layoutCache_.clear();
	bindingCache_.clear();
}

const Engine::MaterialParameterLayout& Engine::MaterialParameterBinder::ResolveLayout(
	const PipelineState& pipeline) {

	auto found = layoutCache_.find(pipeline.GetUniqueID());
	if (found == layoutCache_.end()) {

		MaterialParameterLayout layout{};
		layout.Build(pipeline.GetGraphicsReflection(), MaterialParameterCBuffer::kSurface);
		found = layoutCache_.emplace(pipeline.GetUniqueID(), std::move(layout)).first;
	}
	return found->second;
}

Engine::MaterialParameterBinder::CachedBindingData& Engine::MaterialParameterBinder::ResolveCacheEntry(
	const PipelineState& pipeline, const MaterialAsset& material,
	const std::unordered_map<std::string, MaterialParameterValue>* overrides) {

	const CacheKey key{
		.pipelineID = pipeline.GetUniqueID(),
		.material = &material,
		.overrides = overrides,
	};
	CachedBindingData& cache = bindingCache_[key];
	cache.lastUsedFrame = frameIndex_;

	const uint64_t materialHash = MaterialParameterBufferBuilder::ComputeHash(material.parameters);
	const uint64_t overridesHash = overrides ?
		MaterialParameterBufferBuilder::ComputeHash(*overrides) : 0;
	if (cache.materialHash != materialHash || cache.overridesHash != overridesHash) {

		cache.materialHash = materialHash;
		cache.overridesHash = overridesHash;
		cache.parametersValid = false;
		cache.texturesValid = false;
	}
	return cache;
}

D3D12_GPU_VIRTUAL_ADDRESS Engine::MaterialParameterBinder::ResolveAndUpload(ID3D12Device* device,
	const PipelineState& pipeline, const MaterialAsset& material) {

	const MaterialParameterLayout& layout = ResolveLayout(pipeline);
	if (!layout.IsValid()) {
		return 0;
	}

	CachedBindingData& cache = ResolveCacheEntry(pipeline, material, nullptr);
	if (!cache.parametersValid) {

		cache.packedParameters.resize((std::max)(layout.GetSizeInBytes(), 16u));
		if (!MaterialParameterBufferBuilder::BuildInto(cache.packedParameters, material, layout)) {
			cache.packedParameters.clear();
			return 0;
		}
		cache.parametersValid = true;
	}
	if (cache.packedParameters.empty()) {
		return 0;
	}

	const PostProcessConstantBufferAllocation allocation =
		allocator_.AllocateAndUploadBytes(device, cache.packedParameters);
	return allocation.gpuAddress;
}

D3D12_GPU_VIRTUAL_ADDRESS Engine::MaterialParameterBinder::ResolveAndUpload(ID3D12Device* device,
	const PipelineState& pipeline, const MaterialAsset& material,
	const std::unordered_map<std::string, MaterialParameterValue>& overrides) {

	if (overrides.empty()) {
		return ResolveAndUpload(device, pipeline, material);
	}

	const MaterialParameterLayout& layout = ResolveLayout(pipeline);
	if (!layout.IsValid()) {
		return 0;
	}

	CachedBindingData& cache = ResolveCacheEntry(pipeline, material, &overrides);
	if (!cache.parametersValid) {

		cache.packedParameters.resize((std::max)(layout.GetSizeInBytes(), 16u));
		if (!MaterialParameterBufferBuilder::BuildElementInto(
			cache.packedParameters, material.parameters, overrides, layout,
			[](const std::string&, const AssetID&) { return 0u; })) {
			cache.packedParameters.clear();
			return 0;
		}
		cache.parametersValid = true;
	}
	if (cache.packedParameters.empty()) {
		return 0;
	}

	const PostProcessConstantBufferAllocation allocation =
		allocator_.AllocateAndUploadBytes(device, cache.packedParameters);
	return allocation.gpuAddress;
}

std::span<const Engine::MaterialParameterBinder::TextureBinding>
Engine::MaterialParameterBinder::ResolveTextures(const PipelineState& pipeline,
	const MaterialAsset& material,
	const std::unordered_map<std::string, MaterialParameterValue>* overrides) {

	CachedBindingData& cache = ResolveCacheEntry(pipeline, material, overrides);
	if (cache.texturesValid) {
		return cache.textures;
	}

	cache.textures.clear();
	for (const ShaderResourceBinding& resource : pipeline.GetGraphicsReflection().resources) {

		if (resource.kind != ShaderBindingKind::SRV || resource.space != 2 ||
			resource.rawType != D3D_SIT_TEXTURE) {
			continue;
		}
		const RootBindingLocation* rootBinding =
			pipeline.FindBinding(ShaderBindingKind::SRV, resource.bindPoint, resource.space);
		if (!rootBinding) {
			continue;
		}

		AssetID textureID{};
		if (overrides) {
			const auto found = overrides->find(resource.name);
			if (found != overrides->end()) {
				if (const AssetID* id = std::get_if<AssetID>(&found->second.value)) {
					textureID = *id;
				}
			}
		}
		if (!textureID) {
			const auto found = material.parameters.find(resource.name);
			if (found != material.parameters.end()) {
				if (const AssetID* id = std::get_if<AssetID>(&found->second.value)) {
					textureID = *id;
				}
			}
		}
		cache.textures.emplace_back(TextureBinding{
			.rootBinding = rootBinding,
			.textureID = textureID,
			});
	}
	cache.texturesValid = true;
	return cache.textures;
}
