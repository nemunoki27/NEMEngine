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
	result = HashCombine(result, std::hash<uint64_t>{}(key.materialHash));
	result = HashCombine(result, std::hash<uint64_t>{}(key.instanceHash));
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
	const MaterialParameterSet* overrides) {

	// 内容ハッシュをキーにしてEntityが異なっても同じMaterial Instanceを共有する
	const CacheKey key{
		.pipelineID = pipeline.GetUniqueID(),
		.materialHash = material.parameters.GetContentHash(),
		.instanceHash = overrides ? overrides->GetContentHash() : 0,
	};
	CachedBindingData& cache = bindingCache_[key];
	cache.lastUsedFrame = frameIndex_;
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
	if (cache.uploadedFrame == frameIndex_ && cache.gpuAddress != 0) {
		return cache.gpuAddress;
	}

	const PostProcessConstantBufferAllocation allocation =
		allocator_.AllocateAndUploadBytes(device, cache.packedParameters);
	cache.uploadedFrame = frameIndex_;
	cache.gpuAddress = allocation.gpuAddress;
	return cache.gpuAddress;
}

D3D12_GPU_VIRTUAL_ADDRESS Engine::MaterialParameterBinder::ResolveAndUpload(ID3D12Device* device,
	const PipelineState& pipeline, const MaterialAsset& material,
	const MaterialParameterSet& overrides) {

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
			[](MaterialParameterSemantic, const AssetID&) { return 0u; })) {
			cache.packedParameters.clear();
			return 0;
		}
		cache.parametersValid = true;
	}
	if (cache.packedParameters.empty()) {
		return 0;
	}
	if (cache.uploadedFrame == frameIndex_ && cache.gpuAddress != 0) {
		return cache.gpuAddress;
	}

	const PostProcessConstantBufferAllocation allocation =
		allocator_.AllocateAndUploadBytes(device, cache.packedParameters);
	cache.uploadedFrame = frameIndex_;
	cache.gpuAddress = allocation.gpuAddress;
	return cache.gpuAddress;
}

std::span<const Engine::MaterialParameterBinder::TextureBinding>
Engine::MaterialParameterBinder::ResolveTextures(const PipelineState& pipeline,
	const MaterialAsset& material,
	const MaterialParameterSet* overrides) {

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
			if (const MaterialParameterValue* value =
				overrides->Find(resource.parameterID)) {
				if (const AssetID* id = std::get_if<AssetID>(&value->value)) {
					textureID = *id;
				}
			}
		}
		if (!textureID) {
			if (const MaterialParameterValue* value =
				material.parameters.Find(resource.parameterID)) {
				if (const AssetID* id = std::get_if<AssetID>(&value->value)) {
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
