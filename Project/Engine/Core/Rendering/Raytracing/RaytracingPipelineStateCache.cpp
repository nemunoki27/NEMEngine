#include "RaytracingPipelineStateCache.h"

//============================================================================
//	include
//============================================================================
#include "RaytracingPipelineBuilder.h"
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <chrono>

//============================================================================
//	RaytracingPipelineStateCache classMethods
//============================================================================

bool Engine::RaytracingPipelineCacheKey::operator==(
	const RaytracingPipelineCacheKey& rhs) const noexcept {

	return pipelineAsset == rhs.pipelineAsset &&
		pipelineShaderAsset == rhs.pipelineShaderAsset &&
		shaderOverrideAsset == rhs.shaderOverrideAsset &&
		samplerHash == rhs.samplerHash;
}

Engine::RaytracingPipelineState* Engine::RaytracingPipelineStateCache::GetOrCreate(GraphicsPlatform& graphicsPlatform,
	RenderAssetLibrary& assetLibrary, AssetID pipelineAssetID,
	AssetID shaderOverrideAssetID,
	const PipelineStaticSamplerOverrideSet* samplerOverrides) {

	CollectRetiredStates();

	// 無効なIDの場合はnullptrを返す
	if (!pipelineAssetID) {
		return nullptr;
	}

	// パイプラインアセットをロード
	const RenderPipelineAsset* pipelineAsset = assetLibrary.LoadPipeline(pipelineAssetID);
	if (!pipelineAsset) {
		return FindFallback(pipelineAssetID, shaderOverrideAssetID,
			HashPipelineStaticSamplerOverrides(samplerOverrides));
	}
	// ランタイムの機能から最適なパイプラインバリアントを解決
	const GraphicsRuntimeFeatures& runtimeFeatures = graphicsPlatform.GetFeatureController().GetRuntimeFeatures();
	const PipelineVariantDesc* variant = ResolveBestVariant(*pipelineAsset, PipelineVariantKind::Raytracing, runtimeFeatures);
	if (!variant || variant->kind != PipelineVariantKind::Raytracing) {
		return FindFallback(pipelineAssetID, shaderOverrideAssetID,
			HashPipelineStaticSamplerOverrides(samplerOverrides));
	}

	RaytracingPipelineCacheKey key{};
	key.pipelineAsset = pipelineAssetID;
	key.pipelineShaderAsset = variant->shader;
	key.shaderOverrideAsset = shaderOverrideAssetID;
	key.samplerHash = HashPipelineStaticSamplerOverrides(samplerOverrides);

	// 同じ構成のState Objectはフレーム間で再利用する
	auto found = cache_.find(key);
	if (found != cache_.end()) {
		return found->second.get();
	}
	if (const auto failed = failedRevisions_.find(key);
		failed != failedRevisions_.end() &&
		failed->second == revisions_[key]) {

		return FindFallback(pipelineAssetID, shaderOverrideAssetID,
			key.samplerHash);
	}

	// シェーダーアセットをロード
	const ShaderAsset* shaderAsset = assetLibrary.LoadShader(variant->shader);
	if (!shaderAsset) {
		return FindFallback(pipelineAssetID, shaderOverrideAssetID,
			key.samplerHash);
	}
	ShaderAsset composedShader = *shaderAsset;
	if (shaderOverrideAssetID) {
		const ShaderAsset* shaderOverride =
			assetLibrary.LoadShader(shaderOverrideAssetID);
		if (!shaderOverride) {
			return FindFallback(pipelineAssetID, shaderOverrideAssetID,
				key.samplerHash);
		}
		OverlayShaderExports(composedShader, *shaderOverride);
	}
	if (FindFallback(pipelineAssetID, shaderOverrideAssetID,
		key.samplerHash)) {
		return UpdateAsyncBuild(graphicsPlatform.GetDevice(), key,
			*variant, composedShader, samplerOverrides);
	}
	// パイプラインステートを作成してキャッシュする
	auto state = RaytracingPipelineBuilder::Create(graphicsPlatform.GetDevice(),
		graphicsPlatform.GetDxShaderCompiler(), *variant, composedShader, samplerOverrides);
	if (!state) {

		failedRevisions_[key] = revisions_[key];
		return FindFallback(pipelineAssetID, shaderOverrideAssetID,
			key.samplerHash);
	}
	auto [it, inserted] = cache_.emplace(key, std::move(state));
	failedRevisions_.erase(key);
	RetireFallbacks(pipelineAssetID, shaderOverrideAssetID,
		key.samplerHash);
	return it->second.get();
}

void Engine::RaytracingPipelineStateCache::Clear() {

	// DXR PSOを持つキャッシュはmap破棄任せにせず、終了時に明示resetする
	for (auto& entry : cache_) {
		entry.second.reset();
	}
	cache_.clear();
	for (auto& entry : fallbackCache_) {
		entry.second.reset();
	}
	fallbackCache_.clear();
	for (auto& [key, build] : pendingBuilds_) {
		if (build.result.valid()) {
			build.result.wait();
		}
	}
	pendingBuilds_.clear();
	revisions_.clear();
	failedRevisions_.clear();
	for (auto& states : retiredStates_) {
		states.clear();
	}
	retiredFrameSerials_ = {};
}

void Engine::RaytracingPipelineStateCache::InvalidateByPipelineAsset(
	AssetID pipelineAssetID) {

	for (auto it = cache_.begin(); it != cache_.end();) {
		if (it->first.pipelineAsset == pipelineAssetID) {
			++revisions_[it->first];
			failedRevisions_.erase(it->first);
			PreserveFallback(it->first, std::move(it->second));
			it = cache_.erase(it);
		} else {
			++it;
		}
	}
	for (auto& [key, build] : pendingBuilds_) {
		if (key.pipelineAsset == pipelineAssetID) {
			++revisions_[key];
			failedRevisions_.erase(key);
		}
	}
}

void Engine::RaytracingPipelineStateCache::InvalidateByShaderAsset(
	AssetID shaderAssetID) {

	for (auto it = cache_.begin(); it != cache_.end();) {
		if (it->first.pipelineShaderAsset == shaderAssetID ||
			it->first.shaderOverrideAsset == shaderAssetID) {

			++revisions_[it->first];
			failedRevisions_.erase(it->first);
			PreserveFallback(it->first, std::move(it->second));
			it = cache_.erase(it);
		} else {
			++it;
		}
	}
	for (auto& [key, build] : pendingBuilds_) {
		if (key.pipelineShaderAsset == shaderAssetID ||
			key.shaderOverrideAsset == shaderAssetID) {

			++revisions_[key];
			failedRevisions_.erase(key);
		}
	}
}

void Engine::RaytracingPipelineStateCache::CollectRetiredStates() {

	const uint32_t frameIndex = GraphicsFrameState::GetCurrentIndex();
	const uint64_t frameSerial = GraphicsFrameState::GetFrameSerial();
	if (retiredFrameSerials_[frameIndex] == frameSerial) {
		return;
	}
	retiredStates_[frameIndex].clear();
	retiredFrameSerials_[frameIndex] = frameSerial;
}

void Engine::RaytracingPipelineStateCache::RetireState(
	std::unique_ptr<RaytracingPipelineState> state) {

	if (!state) {
		return;
	}
	CollectRetiredStates();
	retiredStates_[GraphicsFrameState::GetCurrentIndex()].emplace_back(
		std::move(state));
}

Engine::RaytracingPipelineState*
Engine::RaytracingPipelineStateCache::FindFallback(
	AssetID pipelineAssetID, AssetID shaderOverrideAssetID,
	uint64_t samplerHash) const {

	for (const auto& [key, state] : fallbackCache_) {
		if (key.pipelineAsset == pipelineAssetID &&
			key.shaderOverrideAsset == shaderOverrideAssetID &&
			key.samplerHash == samplerHash && state) {

			return state.get();
		}
	}
	return nullptr;
}

void Engine::RaytracingPipelineStateCache::PreserveFallback(
	const RaytracingPipelineCacheKey& key,
	std::unique_ptr<RaytracingPipelineState> state) {

	if (!state) {
		return;
	}
	auto found = fallbackCache_.find(key);
	if (found != fallbackCache_.end()) {
		RetireState(std::move(found->second));
		found->second = std::move(state);
		return;
	}
	fallbackCache_.emplace(key, std::move(state));
}

void Engine::RaytracingPipelineStateCache::RetireFallbacks(
	AssetID pipelineAssetID, AssetID shaderOverrideAssetID,
	uint64_t samplerHash) {

	for (auto it = fallbackCache_.begin();
		it != fallbackCache_.end();) {

		if (it->first.pipelineAsset == pipelineAssetID &&
			it->first.shaderOverrideAsset == shaderOverrideAssetID &&
			it->first.samplerHash == samplerHash) {

			RetireState(std::move(it->second));
			it = fallbackCache_.erase(it);
		} else {
			++it;
		}
	}
}

Engine::RaytracingPipelineState*
Engine::RaytracingPipelineStateCache::UpdateAsyncBuild(
	ID3D12Device8* device,
	const RaytracingPipelineCacheKey& key,
	const PipelineVariantDesc& variant,
	const ShaderAsset& shaderAsset,
	const PipelineStaticSamplerOverrideSet* samplerOverrides) {

	RaytracingPipelineState* fallback = FindFallback(
		key.pipelineAsset, key.shaderOverrideAsset, key.samplerHash);
	const uint64_t revision = revisions_[key];
	auto pending = pendingBuilds_.find(key);
	if (pending != pendingBuilds_.end()) {
		if (pending->second.result.wait_for(std::chrono::seconds(0)) !=
			std::future_status::ready) {

			return fallback;
		}

		std::unique_ptr<RaytracingPipelineState> state =
			pending->second.result.get();
		const uint64_t completedRevision = pending->second.revision;
		pendingBuilds_.erase(pending);
		if (completedRevision != revisions_[key]) {
			RetireState(std::move(state));
		} else if (state) {
			auto [created, inserted] = cache_.emplace(key, std::move(state));
			RetireFallbacks(key.pipelineAsset, key.shaderOverrideAsset,
				key.samplerHash);
			failedRevisions_.erase(key);
			Logger::Output(LogType::Engine,
				"[レイトレーシングパイプライン] ホットリロード結果の差し替えが完了しました");
			return created->second.get();
		} else {
			failedRevisions_[key] = completedRevision;
			Logger::Output(LogType::Engine, spdlog::level::err,
				"[レイトレーシングパイプライン] ホットリロードに失敗したため直前の有効なStateを維持します");
			return fallback;
		}
	}

	if (auto failed = failedRevisions_.find(key);
		failed != failedRevisions_.end() && failed->second == revision) {

		return fallback;
	}

	ComPtr<ID3D12Device8> retainedDevice = device;
	PipelineVariantDesc variantCopy = variant;
	ShaderAsset shaderCopy = shaderAsset;
	const PipelineStaticSamplerOverrideSet samplerCopy = samplerOverrides ?
		*samplerOverrides : PipelineStaticSamplerOverrideSet{};
	const bool hasSamplerOverrides = samplerOverrides != nullptr;
	PendingBuild build{};
	build.revision = revision;
	build.result = std::async(std::launch::async,
		[retainedDevice, variant = std::move(variantCopy),
			shader = std::move(shaderCopy), samplerCopy,
			hasSamplerOverrides]() mutable {

			DxShaderCompiler compiler{};
			compiler.Init();
			return RaytracingPipelineBuilder::Create(retainedDevice.Get(), &compiler,
				variant, shader, hasSamplerOverrides ? &samplerCopy : nullptr);
		});
	pendingBuilds_.emplace(key, std::move(build));
	Logger::Output(LogType::Engine,
		"[レイトレーシングパイプライン] ホットリロード用ビルドを開始しました");
	return fallback;
}

//============================================================================
//	RaytracingPipelineStateCache classMethods
//============================================================================

namespace Engine {

	size_t RaytracingPipelineStateCache::RaytracingPipelineCacheKeyHash::operator()(const RaytracingPipelineCacheKey& key) const noexcept {

		size_t hash = std::hash<AssetID>{}(key.pipelineAsset);
		hash ^= std::hash<AssetID>{}(key.pipelineShaderAsset) << 1;
		hash ^= std::hash<AssetID>{}(key.shaderOverrideAsset) << 2;
		hash ^= std::hash<uint64_t>{}(key.samplerHash) << 3;
		return hash;
	}
}
