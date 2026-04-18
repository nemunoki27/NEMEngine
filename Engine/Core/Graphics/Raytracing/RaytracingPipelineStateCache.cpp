#include "RaytracingPipelineStateCache.h"

//============================================================================
//	RaytracingPipelineStateCache classMethods
//============================================================================

Engine::RaytracingPipelineState* Engine::RaytracingPipelineStateCache::GetOrCreate(GraphicsPlatform& graphicsPlatform,
	RenderAssetLibrary& assetLibrary, AssetID pipelineAssetID) {

	// 無効なIDの場合はnullptrを返す
	if (!pipelineAssetID) {
		return nullptr;
	}

	// すでにキャッシュに存在する場合はそれを返す
	auto found = cache_.find(pipelineAssetID);
	if (found != cache_.end()) {
		return found->second.get();
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
	// シェーダーアセットをロード
	const ShaderAsset* shaderAsset = assetLibrary.LoadShader(variant->shader);
	if (!shaderAsset) {
		return nullptr;
	}
	// パイプラインステートを作成してキャッシュする
	std::unique_ptr<RaytracingPipelineState> state = std::make_unique<RaytracingPipelineState>();
	state->Create(graphicsPlatform.GetDevice(), graphicsPlatform.GetDxShaderCompiler(), *variant, *shaderAsset);
	auto [it, inserted] = cache_.emplace(pipelineAssetID, std::move(state));
	return it->second.get();
}