#include "MaterialParameterBinder.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/HashUtility.h>
#include "MaterialParameterLookup.h"
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

	// TextureのIDと用途と名前で値を探す
	const Engine::MaterialParameterValue* FindTextureParameter(
		const Engine::MaterialParameterSet& parameters,
		const Engine::ShaderResourceBinding& resource,
		const Engine::MaterialParameterSet* defaults = nullptr) {

		return Engine::MaterialParameterLookup::Find(parameters, resource.parameterID, resource.semantic, resource.name, defaults);
	}
}

size_t Engine::MaterialParameterBinder::CacheKeyHasher::operator()(const CacheKey& key) const noexcept {

	// Pipelineと値と描画先でCacheのHashを作る
	size_t result = std::hash<uint64_t>{}(key.pipelineID);
	result = Algorithm::MixHash(result, std::hash<uint64_t>{}(key.materialHash));
	result = Algorithm::MixHash(result, std::hash<uint64_t>{}(key.instanceHash));
	result = Algorithm::MixHash(result, std::hash<AssetID>{}(key.renderTextureTarget));
	return result;
}

void Engine::MaterialParameterBinder::BeginFrame() {

	allocator_.BeginFrame();
	++frameIndex_;

	// 使用されなくなった解決結果を定期的に破棄する
	constexpr uint64_t kCacheRetainFrames = 300;
	if (frameIndex_ % kCacheRetainFrames == 0) {

		std::erase_if(bindingCache_, [&](const auto& entry) {
			return frameIndex_ - entry.second.lastUsedFrame >= kCacheRetainFrames;
			});
	}
}

void Engine::MaterialParameterBinder::Release() {

	// 転送資源を回収へ渡す
	allocator_.Release();
	// CPUの解決結果を解除する
	layoutCache_.clear();
	bindingCache_.clear();
}

const Engine::MaterialParameterLayout& Engine::MaterialParameterBinder::ResolveLayout(
	const PipelineState& pipeline) {

	auto found = layoutCache_.find(pipeline.GetUniqueID());
	if (found == layoutCache_.end()) {

		// Pipelineの不変reflectionから配置を構築する
		MaterialParameterLayout layout{};
		layout.Build(pipeline.GetGraphicsReflection(), MaterialParameterCBuffer::kSurface);
		found = layoutCache_.emplace(pipeline.GetUniqueID(), std::move(layout)).first;
	}
	return found->second;
}

Engine::MaterialParameterBinder::CachedBindingData& Engine::MaterialParameterBinder::ResolveCacheEntry(
	const PipelineState& pipeline, const MaterialAsset& material,
	const MaterialParameterSet* overrides) {

	// 内容Hashと描画対象で解決結果を共有する
	const bool referencesTarget = MaterialParameterLookup::ReferencesAsset(material.parameters, renderTextureTarget_) ||
		(overrides && MaterialParameterLookup::ReferencesAsset(*overrides, renderTextureTarget_));
	const CacheKey key{
		.pipelineID = pipeline.GetUniqueID(),
		.materialHash = material.parameters.GetContentHash(),
		.instanceHash = overrides ? overrides->GetContentHash() : 0,
		.renderTextureTarget = referencesTarget ? renderTextureTarget_ : AssetID{},
	};
	CachedBindingData& cache = bindingCache_[key];
	cache.lastUsedFrame = frameIndex_;
	return cache;
}

D3D12_GPU_VIRTUAL_ADDRESS Engine::MaterialParameterBinder::ResolveAndUpload(
	GraphicsResourceRetirement& retirement, ID3D12Device* device, const PipelineState& pipeline,
	const MaterialAsset& material, const MaterialParameterBufferBuilder::TextureResolver& resolveTexture) {

	return ResolveAndUploadParameters(retirement, device, pipeline, material, nullptr, resolveTexture);
}

D3D12_GPU_VIRTUAL_ADDRESS Engine::MaterialParameterBinder::ResolveAndUpload(
	GraphicsResourceRetirement& retirement, ID3D12Device* device, const PipelineState& pipeline,
	const MaterialAsset& material, const MaterialParameterSet& overrides,
	const MaterialParameterBufferBuilder::TextureResolver& resolveTexture) {

	// 空の上書きは既定値だけのCacheへ揃える
	return ResolveAndUploadParameters(retirement, device, pipeline, material,
		overrides.empty() ? nullptr : &overrides, resolveTexture);
}

D3D12_GPU_VIRTUAL_ADDRESS Engine::MaterialParameterBinder::ResolveAndUploadParameters(
	GraphicsResourceRetirement& retirement, ID3D12Device* device, const PipelineState& pipeline,
	const MaterialAsset& material, const MaterialParameterSet* overrides,
	const MaterialParameterBufferBuilder::TextureResolver& resolveTexture) {

	const MaterialParameterLayout& layout = ResolveLayout(pipeline);
	if (!layout.IsValid()) {
		return 0;
	}

	CachedBindingData& cache = ResolveCacheEntry(pipeline, material, overrides);
	if (!cache.parametersValid && cache.packedFrame != frameIndex_) {

		cache.packedParameters.resize((std::max)(layout.GetSizeInBytes(), 16u));
		bool textureValuesCacheable = true;
		// Textureの一時代替値は次のframeで再解決する
		bool built = overrides ?
			MaterialParameterBufferBuilder::BuildElementInto(cache.packedParameters, material.parameters, *overrides,
				layout, resolveTexture, &textureValuesCacheable) :
			MaterialParameterBufferBuilder::BuildInto(cache.packedParameters, material, layout,
				resolveTexture, &textureValuesCacheable);
		if (!built) {
			cache.packedParameters.clear();
			return 0;
		}
		cache.parametersValid = textureValuesCacheable;
		cache.packedFrame = frameIndex_;
	}
	if (cache.packedParameters.empty()) {
		return 0;
	}
	// 同じframeの転送先を再利用する
	if (cache.uploadedFrame == frameIndex_ && cache.gpuAddress != 0) {
		return cache.gpuAddress;
	}

	// 未転送の値を新しい領域へ書き込む
	const FrameConstantBufferAllocation allocation =
		allocator_.AllocateAndUploadBytes(retirement, device, cache.packedParameters);
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
	// Material用のTextureとRootBindingを対応付ける
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
		bool textureOverridden = false;
		// 明示した空Textureも上書きとして使う
		if (overrides) {
			if (const MaterialParameterValue* value =
				FindTextureParameter(*overrides, resource,
					&material.parameters)) {
				if (const AssetID* id = std::get_if<AssetID>(&value->value)) {
					textureID = *id;
					textureOverridden = true;
				}
			}
		}
		if (!textureOverridden) {
			if (const MaterialParameterValue* value =
				FindTextureParameter(material.parameters, resource)) {
				if (const AssetID* id = std::get_if<AssetID>(&value->value)) {
					textureID = *id;
				}
			}
		}
		cache.textures.emplace_back(TextureBinding{
			.rootBinding = rootBinding,
			.textureID = textureID,
			.semantic = resource.semantic,
			});
	}
	cache.texturesValid = true;
	return cache.textures;
}

void Engine::MaterialParameterBinder::SetTextureRevision(uint64_t revision, AssetID renderTextureTarget) {

	renderTextureTarget_ = renderTextureTarget;

	if (textureRevision_ == revision) return;
	textureRevision_ = revision;
	// 旧drawの転送先を保持し、次のdrawで新しい番号を詰める
	for (auto& [key, cache] : bindingCache_) {
		cache.parametersValid = false;
		cache.packedFrame = UINT64_MAX;
		cache.uploadedFrame = UINT64_MAX;
	}
}
