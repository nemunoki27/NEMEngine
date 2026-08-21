#include "PipelineStateCache.h"

//============================================================================
//	include
//============================================================================
#include <algorithm>
#include <string>
#include <vector>

//============================================================================
//	PipelineStateCache classMethods
//============================================================================

namespace {

	// シェーダーステージエントリからエントリポイントを解決する、存在しない場合は "main" を返す
	std::string ResolveEntryOrDefault(const Engine::ShaderStageEntry* stage) {

		if (!stage || stage->entry.empty()) {
			return "main";
		}
		return stage->entry;
	}
	// シェーダーステージエントリからプロファイルを解決する、存在しない場合はステージに応じたデフォルトを返す
	std::string ResolveProfileOrDefault(Engine::ShaderStage shaderStage, const Engine::ShaderStageEntry* stage) {

		if (stage && !stage->profile.empty()) {
			return stage->profile;
		}
		switch (shaderStage) {
		case Engine::ShaderStage::VS: return "vs_6_0";
		case Engine::ShaderStage::GS: return "gs_6_0";
		case Engine::ShaderStage::PS: return "ps_6_0";
		case Engine::ShaderStage::CS: return "cs_6_0";
		case Engine::ShaderStage::MS: return "ms_6_6";
		case Engine::ShaderStage::AS: return "as_6_6";
		default:                      return "";
		}
	}
	// パイプラインバリアントの情報とシェーダーアセットからグラフィックスパイプラインの記述を構築する
	bool BuildGraphicsPipelineDesc(const Engine::PipelineVariantDesc& variant, const Engine::ShaderAsset& shaderAsset,
		std::span<const DXGI_FORMAT> runtimeRTVFormats, DXGI_FORMAT runtimeDSVFormat,
		const Engine::PipelineStaticSamplerOverrideSet* samplerOverrides,
		Engine::GraphicsPipelineDesc& outDesc) {

		// 基本的な情報をセット
		outDesc = Engine::GraphicsPipelineDesc{};
		outDesc.type = variant.pipelineType;
		outDesc.staticSamplers = variant.staticSamplers;
		if (samplerOverrides) {
			outDesc.staticSamplerOverrides = *samplerOverrides;
		}
		outDesc.rasterizer = variant.rasterizer;
		outDesc.depthStencil = variant.depthStencil;
		outDesc.sampleDesc = variant.sampleDesc;
		outDesc.topologyType = variant.topologyType;

		// シェーダーステージのエントリを取得
		const Engine::ShaderStageEntry* vs = Engine::FindShaderStage(shaderAsset, Engine::ShaderStage::VS);
		const Engine::ShaderStageEntry* gs = Engine::FindShaderStage(shaderAsset, Engine::ShaderStage::GS);
		const Engine::ShaderStageEntry* ms = Engine::FindShaderStage(shaderAsset, Engine::ShaderStage::MS);
		const Engine::ShaderStageEntry* ps = Engine::FindShaderStage(shaderAsset, Engine::ShaderStage::PS);
		const Engine::ShaderStageEntry* as = Engine::FindShaderStage(shaderAsset, Engine::ShaderStage::AS);

		// 種類に応じてシェーダーファイル名をセット
		switch (variant.pipelineType) {
		case Engine::PipelineType::Vertex:

			if (!vs || !ps) {
				return false;
			}
			outDesc.preRaster.shader = vs->ownerShader;
			outDesc.preRaster.file = vs->file;
			outDesc.preRaster.entry = ResolveEntryOrDefault(vs);
			outDesc.preRaster.profile = ResolveProfileOrDefault(Engine::ShaderStage::VS, vs);
			outDesc.pixel.shader = ps->ownerShader;
			outDesc.pixel.file = ps->file;
			outDesc.pixel.entry = ResolveEntryOrDefault(ps);
			outDesc.pixel.profile = ResolveProfileOrDefault(Engine::ShaderStage::PS, ps);
			break;
		case Engine::PipelineType::Geometry:

			// GSパイプラインはVS→GS→PSの3段、VSで頂点を通しGSで太線へ展開する
			if (!vs || !gs || !ps) {
				return false;
			}
			outDesc.preRaster.shader = vs->ownerShader;
			outDesc.preRaster.file = vs->file;
			outDesc.preRaster.entry = ResolveEntryOrDefault(vs);
			outDesc.preRaster.profile = ResolveProfileOrDefault(Engine::ShaderStage::VS, vs);
			outDesc.geometry.shader = gs->ownerShader;
			outDesc.geometry.file = gs->file;
			outDesc.geometry.entry = ResolveEntryOrDefault(gs);
			outDesc.geometry.profile = ResolveProfileOrDefault(Engine::ShaderStage::GS, gs);
			outDesc.pixel.shader = ps->ownerShader;
			outDesc.pixel.file = ps->file;
			outDesc.pixel.entry = ResolveEntryOrDefault(ps);
			outDesc.pixel.profile = ResolveProfileOrDefault(Engine::ShaderStage::PS, ps);
			break;
		case Engine::PipelineType::Mesh:

			if (!ms || !ps) {
				return false;
			}
			outDesc.preRaster.shader = ms->ownerShader;
			outDesc.preRaster.file = ms->file;
			outDesc.preRaster.entry = ResolveEntryOrDefault(ms);
			outDesc.preRaster.profile = ResolveProfileOrDefault(Engine::ShaderStage::MS, ms);
			outDesc.pixel.shader = ps->ownerShader;
			outDesc.pixel.file = ps->file;
			outDesc.pixel.entry = ResolveEntryOrDefault(ps);
			outDesc.pixel.profile = ResolveProfileOrDefault(Engine::ShaderStage::PS, ps);
			if (as) {
				outDesc.amplification.shader = as->ownerShader;
				outDesc.amplification.file = as->file;
				outDesc.amplification.entry = ResolveEntryOrDefault(as);
				outDesc.amplification.profile = ResolveProfileOrDefault(Engine::ShaderStage::AS, as);
			}
			break;
		default:
			return false;
		}

		std::vector<DXGI_FORMAT> finalRTVFormats = variant.rtvFormats;

		if (variant.numRenderTargets == 0) {
			outDesc.numRenderTargets = 0;
			outDesc.dsvFormat = (variant.dsvFormat != DXGI_FORMAT_UNKNOWN) ? variant.dsvFormat : runtimeDSVFormat;
			return true;
		}

		if (variant.dynamicRenderTargetFormats || finalRTVFormats.empty()) {
			finalRTVFormats.assign(runtimeRTVFormats.begin(), runtimeRTVFormats.end());
		}

		// 動的指定でも空だった場合だけ1枚フォールバック
		if (finalRTVFormats.empty()) {

			finalRTVFormats.emplace_back(DXGI_FORMAT_R32G32B32A32_FLOAT);
		}
		outDesc.numRenderTargets = static_cast<UINT>((std::min)(size_t(8), finalRTVFormats.size()));
		for (UINT i = 0; i < outDesc.numRenderTargets; ++i) {
			outDesc.rtvFormats[i] = finalRTVFormats[i];
		}
		outDesc.dsvFormat = (variant.dsvFormat != DXGI_FORMAT_UNKNOWN) ? variant.dsvFormat : runtimeDSVFormat;
		return true;
	}
	// パイプラインバリアントの情報とシェーダーアセットからコンピュートパイプラインの記述を構築する
	bool BuildComputePipelineDesc(const Engine::PipelineVariantDesc& variant,
		const Engine::ShaderAsset& shaderAsset,
		const Engine::PipelineStaticSamplerOverrideSet* samplerOverrides, Engine::ComputePipelineDesc& outDesc) {

		// 基本的な情報をセット
		outDesc = Engine::ComputePipelineDesc{};
		outDesc.staticSamplers = variant.staticSamplers;
		if (samplerOverrides) {
			outDesc.staticSamplerOverrides = *samplerOverrides;
		}

		// シェーダーステージのエントリを取得
		const Engine::ShaderStageEntry* cs = Engine::FindShaderStage(shaderAsset, Engine::ShaderStage::CS);
		if (!cs) {
			return false;
		}
		outDesc.compute.shader = cs->ownerShader;
		outDesc.compute.file = cs->file;
		outDesc.compute.entry = ResolveEntryOrDefault(cs);
		outDesc.compute.profile = ResolveProfileOrDefault(Engine::ShaderStage::CS, cs);
		return true;
	}
}

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
	std::unique_ptr<PipelineState> pipelineState = std::make_unique<PipelineState>();
	if (!pipelineState->CreateGraphics(graphicsPlatform.GetDevice(), graphicsPlatform.GetDxShaderCompiler(), desc)) {
		return restoreFallback();
	}
	pipelineState->ApplyShaderMetadata(
		composedShader);

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
	const PipelineStaticSamplerOverrideSet* samplerOverrides) {

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

	std::unique_ptr<PipelineState> pipelineState = std::make_unique<PipelineState>();
	bool created = false;
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
		created = pipelineState->CreateGraphics(graphicsPlatform.GetDevice(),
			graphicsPlatform.GetDxShaderCompiler(), desc);
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
		created = pipelineState->CreateCompute(graphicsPlatform.GetDevice(),
			graphicsPlatform.GetDxShaderCompiler(), desc);
		break;
	}
	default:
		return nullptr;
	}
	// 生成に失敗した場合は退避した旧PSOへ戻す
	if (!created) {
		return restoreFallback();
	}
	pipelineState->ApplyShaderMetadata(
		*shaderAsset);
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
