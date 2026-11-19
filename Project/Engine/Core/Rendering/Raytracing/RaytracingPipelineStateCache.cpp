#include "RaytracingPipelineStateCache.h"

//============================================================================
//	include
//============================================================================
#include "RaytracingPipelineBuilder.h"
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <chrono>
#include <cstring>
#include <algorithm>

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

	retirement_ = &graphicsPlatform.GetResourceRetirement();
	retiredStates_.Connect(*retirement_);
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
	state->SetRetirementQueue(*retirement_);
	auto [it, inserted] = cache_.emplace(key, std::move(state));
	failedRevisions_.erase(key);
	RetireFallbacks(pipelineAssetID, shaderOverrideAssetID,
		key.samplerHash);
	return it->second.get();
}

Engine::RaytracingPipelineState* Engine::RaytracingPipelineStateCache::GetMaterialReflection(
	GraphicsPlatform& platform, RenderAssetLibrary& assets, const PipelineStaticSamplerOverrideSet& samplers,
	std::span<const RaytracingHitMaterial> materials, bool prepareScene, std::span<const RaytracingHitMaterial>& activeMaterials) {

	retirement_ = &platform.GetResourceRetirement();
	retiredStates_.Connect(*retirement_);
	CollectRetiredStates();
	const uint64_t hash = HashPipelineStaticSamplerOverrides(&samplers);
	auto found = std::ranges::find(materialReflections_, hash, &MaterialReflectionState::samplerHash);
	if (found == materialReflections_.end()) {
		prepareScene = true;
		found = materialReflections_.emplace(materialReflections_.end());
		found->samplerHash = hash;
	}
	auto& entry = *found;
	for (const auto& material : materials) {
		const auto pos = std::lower_bound(entry.materials.begin(), entry.materials.end(), material);
		if (pos == entry.materials.end() || *pos != material) {
			entry.materials.insert(pos, material);
			entry.dirty = true;
		}
	}

	const uint64_t frame = GraphicsFrameState::GetFrameSerial();
	// 初回描画では命中Shaderとシェーダーテーブルの割り当てを同時に確定する
	for (;;) {
		if (entry.build.result.valid()) {
			if (!prepareScene && (entry.lastPollFrame == frame ||
				entry.build.result.wait_for(std::chrono::seconds(0)) != std::future_status::ready)) break;
			auto state = entry.build.result.get();
			if (entry.build.revision == entry.revision) {
				if (state) {
					RetireState(std::move(entry.state));
					state->SetRetirementQueue(*retirement_);
					entry.state = std::move(state);
					entry.activeMaterials.swap(entry.buildingMaterials);
				} else {
					Logger::Output(LogType::Engine, spdlog::level::err, "[レイトレーシングパイプライン] 反射用の命中Shaderの生成に失敗しました");
				}
			}
		}
		if (entry.materials.empty() || (!entry.dirty && entry.requestedRevision == entry.revision)) break;
		entry.dirty = false;
		entry.requestedRevision = entry.revision;
		if (!StartMaterialReflectionBuild(platform, assets, samplers, entry)) {
			Logger::Output(LogType::Engine, spdlog::level::err, "[レイトレーシングパイプライン] 反射用の命中Shaderを準備できません");
			break;
		}
		if (!prepareScene) break;
	}
	entry.lastPollFrame = frame;
	activeMaterials = entry.activeMaterials;
	return entry.state ? entry.state.get() :
		GetOrCreate(platform, assets, BuiltinAssets::Pipelines::RaytracingReflection, {}, &samplers);
}

bool Engine::RaytracingPipelineStateCache::StartMaterialReflectionBuild(GraphicsPlatform& platform, RenderAssetLibrary& assets,
	const PipelineStaticSamplerOverrideSet& samplers, MaterialReflectionState& entry) {

	const auto pipelineID = BuiltinAssets::Pipelines::RaytracingReflection;
	const auto& features = platform.GetFeatureController().GetRuntimeFeatures();
	const auto* pipeline = assets.LoadPipeline(pipelineID);
	const auto* base = pipeline ? ResolveBestVariant(*pipeline, PipelineVariantKind::Raytracing, features) : nullptr;
	if (!base || base->hitGroups.size() != 1) return false;
	PipelineVariantDesc variant = *base;
	const auto* shader = assets.LoadShader(variant.shader);
	if (!shader) return false;
	ShaderAsset combined = *shader;
	entry.shaderDependencies = { variant.shader };
	for (const auto& material : entry.materials) {

		const auto reportInvalidHit = [&](AssetID shader) {
			Logger::Output(LogType::Engine, spdlog::level::err,
				"[レイトレーシングパイプライン] 反射用の命中Shaderが無効です Pipeline={} Shader={}",
				ToString(material.pipeline), ToString(shader));
			return false;
		};
		const auto* materialPipeline = assets.LoadPipeline(material.pipeline);
		const auto* hitVariant = materialPipeline ? ResolveBestVariant(*materialPipeline, PipelineVariantKind::Raytracing, features) : nullptr;
		if (!hitVariant || hitVariant->hitGroups.empty()) return reportInvalidHit(material.shader);
		entry.shaderDependencies.emplace_back(hitVariant->shader);
		const auto* library = assets.LoadShader(hitVariant->shader);
		if (!library) return reportInvalidHit(hitVariant->shader);
		ShaderAsset hitShader = *library;
		if (material.shader && material.shader != hitVariant->shader) {
			const auto* overrideShader = assets.LoadShader(material.shader);
			if (!overrideShader) return reportInvalidHit(material.shader);
			OverlayShaderExports(hitShader, *overrideShader);
		}
		auto group = hitVariant->hitGroups.front();
		const std::string suffix = "_" + std::to_string(variant.hitGroups.size());
		group.exportName += suffix;
		auto append = [&](std::string& name) {
			if (name.empty()) return true;
			const auto* stage = FindShaderExport(hitShader, ShaderStage::Lib, name);
			if (!stage) return false;
			auto exported = *stage;
			name += suffix;
			exported.exportAlias = name;
			combined.stages.emplace_back(std::move(exported));
			return true;
		};
		if (!append(group.closestHitExport) || !append(group.anyHitExport) || !append(group.intersectionExport))
			return reportInvalidHit(material.shader ? material.shader : hitVariant->shader);
		variant.hitGroups.emplace_back(std::move(group));
		variant.maxPayloadSizeInBytes = (std::max)(variant.maxPayloadSizeInBytes, hitVariant->maxPayloadSizeInBytes);
		variant.maxAttributeSizeInBytes = (std::max)(variant.maxAttributeSizeInBytes, hitVariant->maxAttributeSizeInBytes);
		variant.maxRecursionDepth = (std::max)(variant.maxRecursionDepth, hitVariant->maxRecursionDepth);
		for (const auto& sampler : hitVariant->staticSamplers) {
			auto existing = std::ranges::find_if(variant.staticSamplers, [&](const auto& value) {
				return value.ShaderRegister == sampler.ShaderRegister && value.RegisterSpace == sampler.RegisterSpace;
			});
			if (existing == variant.staticSamplers.end()) variant.staticSamplers.emplace_back(sampler);
			else if (std::memcmp(&*existing, &sampler, sizeof(sampler)) != 0) {
				Logger::Output(LogType::Engine, spdlog::level::err, "[レイトレーシングパイプライン] 命中ShaderのSampler設定が競合しています");
				return false;
			}
		}
	}
	entry.buildingMaterials = entry.materials;
	entry.build.revision = entry.revision;
	ComPtr<ID3D12Device8> device = platform.GetDevice();
	entry.build.result = std::async(std::launch::async,
		[device, variant = std::move(variant), shader = std::move(combined), samplers]() mutable {
			DxShaderCompiler compiler{};
			compiler.Init();
			return RaytracingPipelineBuilder::Create(device.Get(), &compiler, variant, shader, &samplers);
		});
	return true;
}

void Engine::RaytracingPipelineStateCache::Clear() {

	materialReflections_.clear();

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

	for (auto& entry : materialReflections_) {
		if (pipelineAssetID == BuiltinAssets::Pipelines::RaytracingReflection ||
			std::ranges::find(entry.materials, pipelineAssetID, &RaytracingHitMaterial::pipeline) != entry.materials.end()) {
			++entry.revision;
		}
	}

	// 初回生成に失敗した構成も更新後に再試行する
	std::erase_if(failedRevisions_, [pipelineAssetID](const auto& entry) {
		return entry.first.pipelineAsset == pipelineAssetID;
	});
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

	for (auto& entry : materialReflections_) {
		if (std::ranges::find(entry.shaderDependencies, shaderAssetID) != entry.shaderDependencies.end() ||
			std::ranges::find(entry.materials, shaderAssetID, &RaytracingHitMaterial::shader) != entry.materials.end()) {
			++entry.revision;
		}
	}

	// 有効な旧StateがなくてもShaderの修正を反映する
	std::erase_if(failedRevisions_, [shaderAssetID](const auto& entry) {
		return entry.first.pipelineShaderAsset == shaderAssetID || entry.first.shaderOverrideAsset == shaderAssetID;
	});
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
			state->SetRetirementQueue(*retirement_);
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
