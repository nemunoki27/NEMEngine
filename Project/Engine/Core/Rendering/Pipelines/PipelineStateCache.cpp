#include "PipelineStateCache.h"

//============================================================================
//	include
//============================================================================
#include "PipelineDescriptionBuilder.h"
#include "PipelineStateBuilder.h"
#include <algorithm>
#include <string>
#include <vector>

//============================================================================
//	PipelineStateCache classMethods
//============================================================================

using namespace Engine::PipelineDescriptionBuilder;

bool Engine::PipelineCacheKey::operator==(const PipelineCacheKey& rhs) const noexcept {
	return pipelineAsset == rhs.pipelineAsset &&
		geometryPipelineAsset == rhs.geometryPipelineAsset &&
		pipelineShaderAsset == rhs.pipelineShaderAsset &&
		geometryShaderAsset == rhs.geometryShaderAsset &&
		shaderOverrideAsset == rhs.shaderOverrideAsset &&
		resolvedKind == rhs.resolvedKind &&
		formatHash == rhs.formatHash &&
		meshEnabled == rhs.meshEnabled &&
		inlineRayTracingEnabled == rhs.inlineRayTracingEnabled &&
		dispatchRaysEnabled == rhs.dispatchRaysEnabled &&
		depthForcedTestWrite == rhs.depthForcedTestWrite &&
		samplerHash == rhs.samplerHash;
}

const Engine::PipelineState* Engine::PipelineStateCache::GetORCreateComposed(GraphicsPlatform& graphicsPlatform,
	RenderAssetLibrary& assetLibrary, AssetID pipelineAssetID, AssetID geometryPipelineAssetID,
	AssetID shaderOverrideAssetID, PipelineVariantKind desiredKind,
	std::span<const DXGI_FORMAT> runtimeRTVFormats, DXGI_FORMAT runtimeDSVFormat,
	const GraphicsRuntimeFeatures& runtimeFeatures, const PipelineVariantDesc** outVariant) {

	const RenderPipelineAsset* statePipeline = assetLibrary.LoadPipeline(pipelineAssetID);
	const RenderPipelineAsset* geometryPipeline = assetLibrary.LoadPipeline(geometryPipelineAssetID);
	if (!statePipeline || !geometryPipeline) {
		return nullptr;
	}

	const PipelineVariantDesc* stateVariant = ResolveBestVariant(*statePipeline, desiredKind, runtimeFeatures);
	const PipelineVariantDesc* geometryVariant = ResolveBestVariant(*geometryPipeline, desiredKind, runtimeFeatures);
	if (!stateVariant || !geometryVariant) {
		return nullptr;
	}
	if (outVariant) {
		*outVariant = geometryVariant;
	}

	PipelineCacheKey key{};
	key.pipelineAsset = pipelineAssetID;
	key.geometryPipelineAsset = geometryPipelineAssetID;
	key.pipelineShaderAsset = stateVariant->shader;
	key.geometryShaderAsset = geometryVariant->shader;
	key.shaderOverrideAsset = shaderOverrideAssetID;
	key.resolvedKind = geometryVariant->kind;
	key.meshEnabled = runtimeFeatures.useMeshShader;
	key.inlineRayTracingEnabled = runtimeFeatures.useInlineRayTracing;
	key.dispatchRaysEnabled = runtimeFeatures.useDispatchRays;
	key.formatHash = HashFormats(runtimeRTVFormats,
		(stateVariant->dsvFormat != DXGI_FORMAT_UNKNOWN) ? stateVariant->dsvFormat : runtimeDSVFormat);

	if (auto found = cache_.find(key); found != cache_.end()) {
		return found->second.get();
	}
	auto restoreFallback = [&]() -> const PipelineState* {

		auto fallback = fallbackCache_.find(key);
		if (fallback == fallbackCache_.end()) {
			return nullptr;
		}
		auto [restored, inserted] = cache_.emplace(key, std::move(fallback->second));
		fallbackCache_.erase(fallback);
		return restored->second.get();
	};

	const ShaderAsset* stateShader = assetLibrary.LoadShader(stateVariant->shader);
	const ShaderAsset* geometryShader = assetLibrary.LoadShader(geometryVariant->shader);
	if (!stateShader || !geometryShader) {
		return restoreFallback();
	}
	ShaderAsset composedShader = *geometryShader;
	for (const ShaderStageEntry& stage : stateShader->stages) {
		if (stage.stage == ShaderStage::PS) {
			ShaderAsset pixelShader{};
			pixelShader.stages.emplace_back(stage);
			pixelShader.colorParameters = stateShader->colorParameters;
			pixelShader.parameters =
				stateShader->parameters;
			OverlayShaderExports(composedShader, pixelShader);
		}
	}
	if (shaderOverrideAssetID) {
		const ShaderAsset* shaderOverride = assetLibrary.LoadShader(shaderOverrideAssetID);
		if (!shaderOverride) {
			return restoreFallback();
		}
		OverlayShaderExports(composedShader, *shaderOverride);
	}

	PipelineVariantDesc composedVariant = *stateVariant;
	composedVariant.kind = geometryVariant->kind;
	composedVariant.pipelineType = geometryVariant->pipelineType;
	composedVariant.topologyType = geometryVariant->topologyType;
	composedVariant.requiresMeshShader = geometryVariant->requiresMeshShader;

	GraphicsPipelineDesc desc{};
	if (!BuildGraphicsPipelineDesc(composedVariant, composedShader,
		runtimeRTVFormats, runtimeDSVFormat, nullptr, desc)) {
		return restoreFallback();
	}
	std::unique_ptr<PipelineState> pipelineState = PipelineStateBuilder::CreateGraphics(graphicsPlatform.GetResourceRetirement(),
		graphicsPlatform.GetDevice(), graphicsPlatform.GetDxShaderCompiler(), desc, &composedShader);
	if (!pipelineState) {
		return restoreFallback();
	}

	auto [it, inserted] = cache_.emplace(key, std::move(pipelineState));
	fallbackCache_.erase(key);
	graphicsReflectionByPipeline_[pipelineAssetID] =
		it->second->GetGraphicsReflection();
	return it->second.get();
}

const Engine::PipelineState* Engine::PipelineStateCache::GetORCreate(GraphicsPlatform& graphicsPlatform,
	RenderAssetLibrary& assetLibrary, AssetID pipelineAssetID, PipelineVariantKind desiredKind,
	std::span<const DXGI_FORMAT> runtimeRTVFormats, DXGI_FORMAT runtimeDSVFormat) {

	return GetORCreate(graphicsPlatform, assetLibrary, pipelineAssetID, desiredKind,
		runtimeRTVFormats, runtimeDSVFormat,
		graphicsPlatform.GetFeatureController().GetRuntimeFeatures());
}

const Engine::PipelineState* Engine::PipelineStateCache::GetORCreate(GraphicsPlatform& graphicsPlatform,
	RenderAssetLibrary& assetLibrary, AssetID pipelineAssetID, PipelineVariantKind desiredKind,
	std::span<const DXGI_FORMAT> runtimeRTVFormats, DXGI_FORMAT runtimeDSVFormat,
	const GraphicsRuntimeFeatures& runtimeFeatures,
	const PipelineVariantDesc** outVariant, bool forceDepthTestWrite,
	const PipelineStaticSamplerOverrideSet* samplerOverrides,
	AssetID shaderOverrideAssetID) {

	// アセットライブラリからパイプラインアセットをロード
	const RenderPipelineAsset* pipelineAsset = assetLibrary.LoadPipeline(pipelineAssetID);
	// 存在しない場合はnullptrを返す
	if (!pipelineAsset) {
		return nullptr;
	}

	// パイプラインアセットから、要求された種類とGPUの機能に最も適したバリアントを解決する
	const PipelineVariantDesc* variant = ResolveBestVariant(*pipelineAsset, desiredKind, runtimeFeatures);
	if (!variant) {
		return nullptr;
	}
	// 呼び出し側が同じロードとバリアント解決を繰り返さずに済むよう、解決済みバリアントを返す
	if (outVariant) {
		*outVariant = variant;
	}

	// キャッシュキーを構築して、キャッシュに存在するか確認する
	PipelineCacheKey key{};
	key.pipelineAsset = pipelineAssetID;
	key.pipelineShaderAsset = variant->shader;
	key.shaderOverrideAsset = shaderOverrideAssetID;
	key.resolvedKind = variant->kind;
	key.meshEnabled = runtimeFeatures.useMeshShader;
	key.inlineRayTracingEnabled = runtimeFeatures.useInlineRayTracing;
	key.dispatchRaysEnabled = runtimeFeatures.useDispatchRays;
	key.depthForcedTestWrite = forceDepthTestWrite;
	key.formatHash = HashFormats(runtimeRTVFormats,
		variant->dsvFormat != DXGI_FORMAT_UNKNOWN ?
		variant->dsvFormat : runtimeDSVFormat);
	key.samplerHash = HashStaticSamplerOverrides(samplerOverrides);

	// キャッシュに存在する場合はそれを返す
	auto found = cache_.find(key);
	if (found != cache_.end()) {
		return found->second.get();
	}
	auto restoreFallback = [&]() -> const PipelineState* {

		auto fallback = fallbackCache_.find(key);
		if (fallback == fallbackCache_.end()) {
			return nullptr;
		}
		auto [restored, inserted] = cache_.emplace(key, std::move(fallback->second));
		fallbackCache_.erase(fallback);
		return restored->second.get();
	};

	// キャッシュに存在しない場合は、新たにパイプラインステートを生成する
	const ShaderAsset* shaderAsset = assetLibrary.LoadShader(variant->shader);
	if (!shaderAsset) {
		return restoreFallback();
	}
	ShaderAsset composedShader{};
	if (shaderOverrideAssetID) {
		const ShaderAsset* shaderOverride =
			assetLibrary.LoadShader(shaderOverrideAssetID);
		if (!shaderOverride) {
			return restoreFallback();
		}
		composedShader = *shaderAsset;
		OverlayShaderExports(composedShader, *shaderOverride);
		shaderAsset = &composedShader;
	}

	std::unique_ptr<PipelineState> pipelineState;
	switch (variant->kind) {
	case PipelineVariantKind::GraphicsVertex:
	case PipelineVariantKind::GraphicsGeometry:
	case PipelineVariantKind::GraphicsMesh:
	{
		// グラフィックスパイプラインの記述を構築
		GraphicsPipelineDesc desc{};
		if (!BuildGraphicsPipelineDesc(*variant, *shaderAsset, runtimeRTVFormats,
			runtimeDSVFormat, samplerOverrides, desc)) {
			return restoreFallback();
		}
		// 次元で深度挙動を変える描画用に、深度テスト+書き込みを強制する
		// 3DテキストをMeshと同じく前後遮蔽させたいが、2Dと同じパイプラインを使うためここで上書きする
		if (forceDepthTestWrite) {

			desc.depthStencil.DepthEnable = TRUE;
			desc.depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
			desc.depthStencil.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		}
		// パイプラインステートオブジェクトを生成
		pipelineState = PipelineStateBuilder::CreateGraphics(graphicsPlatform.GetResourceRetirement(), graphicsPlatform.GetDevice(),
			graphicsPlatform.GetDxShaderCompiler(), desc, shaderAsset);
		break;
	}
	case PipelineVariantKind::Compute:
	{
		// コンピュートパイプラインの記述を構築
		ComputePipelineDesc desc{};
		if (!BuildComputePipelineDesc(*variant, *shaderAsset, samplerOverrides, desc)) {
			return restoreFallback();
		}
		// パイプラインステートオブジェクトを生成
		pipelineState = PipelineStateBuilder::CreateCompute(graphicsPlatform.GetResourceRetirement(), graphicsPlatform.GetDevice(),
			graphicsPlatform.GetDxShaderCompiler(), desc, shaderAsset);
		break;
	}
	default:
		return nullptr;
	}
	// 生成に失敗した場合は退避した旧PSOへ戻す
	if (!pipelineState) {
		return restoreFallback();
	}
	// キャッシュに保存
	auto [it, inserted] = cache_.emplace(key, std::move(pipelineState));
	fallbackCache_.erase(key);
	// マテリアルインスペクタ等がエディタ側でPSOを再生成せず、reflectionを引けるようpipelineAsset別に保存する
	if (variant->kind != PipelineVariantKind::Compute) {
		graphicsReflectionByPipeline_[pipelineAssetID] =
			it->second->GetGraphicsReflection();
	}
	return it->second.get();
}

const Engine::ShaderReflectionInfo* Engine::PipelineStateCache::FindGraphicsReflection(AssetID pipelineAssetID) const {

	auto found = graphicsReflectionByPipeline_.find(pipelineAssetID);
	if (found == graphicsReflectionByPipeline_.end()) {
		return nullptr;
	}
	return &found->second;
}

void Engine::PipelineStateCache::Clear() {

	// PipelineStateはRootSignature/PSOを持つため、cache破棄前に明示resetする
	for (auto& entry : cache_) {
		entry.second.reset();
	}
	cache_.clear();
	for (auto& entry : fallbackCache_) {
		entry.second.reset();
	}
	fallbackCache_.clear();
	graphicsReflectionByPipeline_.clear();
}

void Engine::PipelineStateCache::InvalidateByPipelineAsset(AssetID pipelineAssetID) {

	for (auto it = cache_.begin(); it != cache_.end(); ) {
		if (it->first.pipelineAsset == pipelineAssetID || it->first.geometryPipelineAsset == pipelineAssetID) {
			graphicsReflectionByPipeline_.erase(it->first.pipelineAsset);
			if (it->second) {
				it->second.reset();
			}
			it = cache_.erase(it);
		} else {
			++it;
		}
	}
	for (auto it = fallbackCache_.begin(); it != fallbackCache_.end(); ) {
		if (it->first.pipelineAsset == pipelineAssetID || it->first.geometryPipelineAsset == pipelineAssetID) {
			graphicsReflectionByPipeline_.erase(it->first.pipelineAsset);
			if (it->second) {
				it->second.reset();
			}
			it = fallbackCache_.erase(it);
		} else {
			++it;
		}
	}
	graphicsReflectionByPipeline_.erase(pipelineAssetID);
}

uint64_t Engine::PipelineStateCache::HashFormats(std::span<const DXGI_FORMAT> rtvFormats, DXGI_FORMAT dsvFormat) {

	uint64_t hash = 1469598103934665603ull;
	auto mix = [&](uint64_t value) {
		hash ^= value;
		hash *= 1099511628211ull;
		};

	mix(static_cast<uint64_t>(dsvFormat));
	for (DXGI_FORMAT format : rtvFormats) {
		mix(static_cast<uint64_t>(format));
	}
	return hash;
}

void Engine::PipelineStateCache::InvalidateByShaderAsset(AssetID shaderAssetID) {

	for (auto it = cache_.begin(); it != cache_.end(); ) {
		if (it->first.pipelineShaderAsset == shaderAssetID ||
			it->first.geometryShaderAsset == shaderAssetID ||
			it->first.shaderOverrideAsset == shaderAssetID) {

			graphicsReflectionByPipeline_.erase(it->first.pipelineAsset);
			fallbackCache_[it->first] = std::move(it->second);
			it = cache_.erase(it);
		} else {
			++it;
		}
	}
}

const Engine::ShaderReflectionInfo* Engine::PipelineStateCache::FindGraphicsReflection(
	AssetID pipelineAssetID, AssetID shaderOverrideAssetID) const {

	for (const auto& [key, pipeline] : cache_) {
		if (key.pipelineAsset == pipelineAssetID && key.shaderOverrideAsset == shaderOverrideAssetID && pipeline) {
			return &pipeline->GetGraphicsReflection();
		}
	}
	for (const auto& [key, pipeline] : fallbackCache_) {
		if (key.pipelineAsset == pipelineAssetID && key.shaderOverrideAsset == shaderOverrideAssetID && pipeline) {
			return &pipeline->GetGraphicsReflection();
		}
	}
	return FindGraphicsReflection(pipelineAssetID);
}

uint64_t Engine::PipelineStateCache::HashStaticSamplerOverrides(
	const PipelineStaticSamplerOverrideSet* samplerOverrides) {

	return HashPipelineStaticSamplerOverrides(samplerOverrides);
}

//============================================================================
//	PipelineStateCache classMethods
//============================================================================

namespace Engine {

	size_t PipelineStateCache::PipelineCacheKeyHash::operator()(const PipelineCacheKey& key) const noexcept {

		size_t h = std::hash<AssetID>{}(key.pipelineAsset);
		h ^= (std::hash<AssetID>{}(key.geometryPipelineAsset) << 1);
		h ^= (std::hash<AssetID>{}(key.pipelineShaderAsset) << 2);
		h ^= (std::hash<AssetID>{}(key.geometryShaderAsset) << 3);
		h ^= (std::hash<AssetID>{}(key.shaderOverrideAsset) << 4);
		h ^= (std::hash<uint32_t>{}(static_cast<uint32_t>(key.resolvedKind)) << 5);
		h ^= (std::hash<uint64_t>{}(key.formatHash) << 6);
		h ^= (std::hash<bool>{}(key.meshEnabled) << 7);
		h ^= (std::hash<bool>{}(key.inlineRayTracingEnabled) << 8);
		h ^= (std::hash<bool>{}(key.dispatchRaysEnabled) << 9);
		h ^= (std::hash<bool>{}(key.depthForcedTestWrite) << 10);
		h ^= (std::hash<uint64_t>{}(key.samplerHash) << 11);
		return h;
	}
}
