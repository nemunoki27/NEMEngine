#include "RaytracingPipelineStateCache.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>

//============================================================================
//	RaytracingPipelineStateCache classMethods
//============================================================================

namespace {

	// Material側の同一ステージでパイプライン側のShaderを上書きする
	void OverlayShaderStages(Engine::ShaderAsset& target,
		const Engine::ShaderAsset& source) {

		for (const Engine::ShaderStageEntry& sourceStage : source.stages) {

			auto found = std::find_if(target.stages.begin(), target.stages.end(),
				[&](const Engine::ShaderStageEntry& stage) {
					return stage.stage == sourceStage.stage;
				});
			if (found != target.stages.end()) {
				*found = sourceStage;
			} else {
				target.stages.emplace_back(sourceStage);
			}
		}
	}
}

bool Engine::RaytracingPipelineCacheKey::operator==(
	const RaytracingPipelineCacheKey& rhs) const noexcept {

	return pipelineAsset == rhs.pipelineAsset &&
		pipelineShaderAsset == rhs.pipelineShaderAsset &&
		shaderOverrideAsset == rhs.shaderOverrideAsset;
}

Engine::RaytracingPipelineState* Engine::RaytracingPipelineStateCache::GetOrCreate(GraphicsPlatform& graphicsPlatform,
	RenderAssetLibrary& assetLibrary, AssetID pipelineAssetID,
	AssetID shaderOverrideAssetID) {

	// 無効なIDの場合はnullptrを返す
	if (!pipelineAssetID) {
		return nullptr;
	}

	// パイプラインアセットをロード
	const RenderPipelineAsset* pipelineAsset = assetLibrary.LoadPipeline(pipelineAssetID);
	if (!pipelineAsset) {
		return nullptr;
	}
	// ランタイムの機能から最適なパイプラインバリアントを解決
	const GraphicsRuntimeFeatures& runtimeFeatures = graphicsPlatform.GetFeatureController().GetRuntimeFeatures();
	const PipelineVariantDesc* variant = ResolveBestVariant(*pipelineAsset, PipelineVariantKind::Raytracing, runtimeFeatures);
	if (!variant || variant->kind != PipelineVariantKind::Raytracing) {
		return nullptr;
	}

	RaytracingPipelineCacheKey key{};
	key.pipelineAsset = pipelineAssetID;
	key.pipelineShaderAsset = variant->shader;
	key.shaderOverrideAsset = shaderOverrideAssetID;

	// 同じ構成のState Objectはフレーム間で再利用する
	auto found = cache_.find(key);
	if (found != cache_.end()) {
		return found->second.get();
	}

	// シェーダーアセットをロード
	const ShaderAsset* shaderAsset = assetLibrary.LoadShader(variant->shader);
	if (!shaderAsset) {
		return nullptr;
	}
	ShaderAsset composedShader = *shaderAsset;
	if (shaderOverrideAssetID) {
		const ShaderAsset* shaderOverride =
			assetLibrary.LoadShader(shaderOverrideAssetID);
		if (!shaderOverride) {
			return nullptr;
		}
		OverlayShaderStages(composedShader, *shaderOverride);
	}
	// パイプラインステートを作成してキャッシュする
	std::unique_ptr<RaytracingPipelineState> state = std::make_unique<RaytracingPipelineState>();
	if (!state->Create(graphicsPlatform.GetDevice(),
		graphicsPlatform.GetDxShaderCompiler(), *variant, composedShader)) {
		cache_.emplace(key, nullptr);
		return nullptr;
	}
	auto [it, inserted] = cache_.emplace(key, std::move(state));
	return it->second.get();
}

void Engine::RaytracingPipelineStateCache::Clear() {

	// DXR PSOを持つキャッシュはmap破棄任せにせず、終了時に明示resetする
	for (auto& entry : cache_) {
		entry.second.reset();
	}
	cache_.clear();
}

void Engine::RaytracingPipelineStateCache::InvalidateByPipelineAsset(
	AssetID pipelineAssetID) {

	for (auto it = cache_.begin(); it != cache_.end();) {
		if (it->first.pipelineAsset == pipelineAssetID) {
			it = cache_.erase(it);
		} else {
			++it;
		}
	}
}
