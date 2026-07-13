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
		std::span<const DXGI_FORMAT> runtimeRTVFormats, DXGI_FORMAT runtimeDSVFormat, Engine::GraphicsPipelineDesc& outDesc) {

		// 基本的な情報をセット
		outDesc = Engine::GraphicsPipelineDesc{};
		outDesc.type = variant.pipelineType;
		outDesc.staticSamplers = variant.staticSamplers;
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
			outDesc.preRaster.file = vs->file;
			outDesc.preRaster.entry = ResolveEntryOrDefault(vs);
			outDesc.preRaster.profile = ResolveProfileOrDefault(Engine::ShaderStage::VS, vs);
			outDesc.pixel.file = ps->file;
			outDesc.pixel.entry = ResolveEntryOrDefault(ps);
			outDesc.pixel.profile = ResolveProfileOrDefault(Engine::ShaderStage::PS, ps);
			break;
		case Engine::PipelineType::Geometry:

			// GSパイプラインはVS→GS→PSの3段、VSで頂点を通しGSで太線へ展開する
			if (!vs || !gs || !ps) {
				return false;
			}
			outDesc.preRaster.file = vs->file;
			outDesc.preRaster.entry = ResolveEntryOrDefault(vs);
			outDesc.preRaster.profile = ResolveProfileOrDefault(Engine::ShaderStage::VS, vs);
			outDesc.geometry.file = gs->file;
			outDesc.geometry.entry = ResolveEntryOrDefault(gs);
			outDesc.geometry.profile = ResolveProfileOrDefault(Engine::ShaderStage::GS, gs);
			outDesc.pixel.file = ps->file;
			outDesc.pixel.entry = ResolveEntryOrDefault(ps);
			outDesc.pixel.profile = ResolveProfileOrDefault(Engine::ShaderStage::PS, ps);
			break;
		case Engine::PipelineType::Mesh:

			if (!ms || !ps) {
				return false;
			}
			outDesc.preRaster.file = ms->file;
			outDesc.preRaster.entry = ResolveEntryOrDefault(ms);
			outDesc.preRaster.profile = ResolveProfileOrDefault(Engine::ShaderStage::MS, ms);
			outDesc.pixel.file = ps->file;
			outDesc.pixel.entry = ResolveEntryOrDefault(ps);
			outDesc.pixel.profile = ResolveProfileOrDefault(Engine::ShaderStage::PS, ps);
			if (as) {
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
		outDesc.compute.file = cs->file;
		outDesc.compute.entry = ResolveEntryOrDefault(cs);
		outDesc.compute.profile = ResolveProfileOrDefault(Engine::ShaderStage::CS, cs);
		return true;
	}
	// 同じステージを部分シェーダーで上書きする
	void OverlayShaderStages(Engine::ShaderAsset& target, const Engine::ShaderAsset& source) {

		for (const Engine::ShaderStageEntry& sourceStage : source.stages) {

			auto found = std::find_if(target.stages.begin(), target.stages.end(),
				[&](const Engine::ShaderStageEntry& stage) { return stage.stage == sourceStage.stage; });
			if (found != target.stages.end()) {
				*found = sourceStage;
			} else {
				target.stages.emplace_back(sourceStage);
			}
		}
		for (const std::string& name : source.colorParameters) {
			if (std::find(target.colorParameters.begin(), target.colorParameters.end(), name) == target.colorParameters.end()) {
				target.colorParameters.emplace_back(name);
			}
		}
	}
}

bool Engine::PipelineCacheKey::operator==(const PipelineCacheKey& rhs) const noexcept {
	return pipelineAsset == rhs.pipelineAsset &&
		geometryPipelineAsset == rhs.geometryPipelineAsset &&
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
			OverlayShaderStages(composedShader, pixelShader);
		}
	}
	if (shaderOverrideAssetID) {
		const ShaderAsset* shaderOverride = assetLibrary.LoadShader(shaderOverrideAssetID);
		if (!shaderOverride) {
			return restoreFallback();
		}
		OverlayShaderStages(composedShader, *shaderOverride);
	}

	PipelineVariantDesc composedVariant = *stateVariant;
	composedVariant.kind = geometryVariant->kind;
	composedVariant.pipelineType = geometryVariant->pipelineType;
	composedVariant.topologyType = geometryVariant->topologyType;
	composedVariant.requiresMeshShader = geometryVariant->requiresMeshShader;

	GraphicsPipelineDesc desc{};
	if (!BuildGraphicsPipelineDesc(composedVariant, composedShader, runtimeRTVFormats, runtimeDSVFormat, desc)) {
		return restoreFallback();
	}
	std::unique_ptr<PipelineState> pipelineState = std::make_unique<PipelineState>();
	if (!pipelineState->CreateGraphics(graphicsPlatform.GetDevice(), graphicsPlatform.GetDxShaderCompiler(), desc)) {
		return restoreFallback();
	}

	auto [it, inserted] = cache_.emplace(key, std::move(pipelineState));
	fallbackCache_.erase(key);
	ShaderReflectionInfo reflection = it->second->GetGraphicsReflection();
	for (ShaderConstantBufferInfo& cb : reflection.constantBuffers) {
		for (ShaderConstantBufferVariable& var : cb.variables) {
			var.isColor = std::find(composedShader.colorParameters.begin(),
				composedShader.colorParameters.end(), var.name) != composedShader.colorParameters.end();
		}
	}
	for (ShaderStructuredBufferInfo& buffer : reflection.structuredBuffers) {
		for (ShaderConstantBufferVariable& var : buffer.variables) {
			var.isColor = std::find(composedShader.colorParameters.begin(),
				composedShader.colorParameters.end(), var.name) != composedShader.colorParameters.end();
		}
	}
	graphicsReflectionByPipeline_[pipelineAssetID] = std::move(reflection);
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
	key.resolvedKind = variant->kind;
	key.meshEnabled = runtimeFeatures.useMeshShader;
	key.inlineRayTracingEnabled = runtimeFeatures.useInlineRayTracing;
	key.dispatchRaysEnabled = runtimeFeatures.useDispatchRays;
	key.depthForcedTestWrite = forceDepthTestWrite;
	key.formatHash = HashFormats(runtimeRTVFormats, (variant->dsvFormat != DXGI_FORMAT_UNKNOWN) ? variant->dsvFormat : runtimeDSVFormat);
	key.samplerHash = HashStaticSamplerOverrides(samplerOverrides);

	// キャッシュに存在する場合はそれを返す
	auto found = cache_.find(key);
	if (found != cache_.end()) {
		return found->second.get();
	}

	// キャッシュに存在しない場合は、新たにパイプラインステートを生成する
	const ShaderAsset* shaderAsset = assetLibrary.LoadShader(variant->shader);
	if (!shaderAsset) {
		return nullptr;
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
		if (!BuildGraphicsPipelineDesc(*variant, *shaderAsset, runtimeRTVFormats, runtimeDSVFormat, desc)) {
			return nullptr;
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
			return nullptr;
		}
		// パイプラインステートオブジェクトを生成
		created = pipelineState->CreateCompute(graphicsPlatform.GetDevice(),
			graphicsPlatform.GetDxShaderCompiler(), desc);
		break;
	}
	default:
		return nullptr;
	}
	// 生成に失敗した場合はnullptrを返す
	if (!created) {
		return nullptr;
	}
	// キャッシュに保存
	auto [it, inserted] = cache_.emplace(key, std::move(pipelineState));
	// マテリアルインスペクタ等がエディタ側でPSOを再生成せず、reflectionを引けるようpipelineAsset別に保存する
	if (variant->kind != PipelineVariantKind::Compute) {

		ShaderReflectionInfo reflection = it->second->GetGraphicsReflection();
		// シェーダー側メタデータで宣言された色paramにisColorを立てる
		for (ShaderConstantBufferInfo& cb : reflection.constantBuffers) {
			for (ShaderConstantBufferVariable& var : cb.variables) {
				var.isColor = std::find(shaderAsset->colorParameters.begin(),
					shaderAsset->colorParameters.end(), var.name) != shaderAsset->colorParameters.end();
			}
		}
		for (ShaderStructuredBufferInfo& buffer : reflection.structuredBuffers) {
			for (ShaderConstantBufferVariable& var : buffer.variables) {
				var.isColor = std::find(shaderAsset->colorParameters.begin(),
					shaderAsset->colorParameters.end(), var.name) != shaderAsset->colorParameters.end();
			}
		}
		graphicsReflectionByPipeline_[pipelineAssetID] = std::move(reflection);
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
			if (it->second) {
				it->second.reset();
			}
			it = cache_.erase(it);
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

void Engine::PipelineStateCache::InvalidateByShaderOverride(AssetID shaderOverrideAssetID) {

	for (auto it = cache_.begin(); it != cache_.end(); ) {
		if (it->first.shaderOverrideAsset == shaderOverrideAssetID) {
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

	if (!samplerOverrides) {
		return 0;
	}

	uint64_t hash = 1469598103934665603ull;
	auto mix = [&](uint64_t value) {
		hash ^= value;
		hash *= 1099511628211ull;
		};
	auto mixFloat = [&](float value) {
		mix(std::hash<float>{}(value));
		};

	mix(samplerOverrides->fillMissingSamplers ? 1ull : 0ull);

	std::vector<std::string> names;
	names.reserve(samplerOverrides->byName.size());
	for (const auto& [name, settings] : samplerOverrides->byName) {
		names.emplace_back(name);
	}
	std::sort(names.begin(), names.end());

	for (const std::string& name : names) {

		mix(std::hash<std::string>{}(name));
		const PipelineStaticSamplerSettings& settings = samplerOverrides->byName.at(name);
		mix(static_cast<uint64_t>(settings.filter));
		mix(static_cast<uint64_t>(settings.addressU));
		mix(static_cast<uint64_t>(settings.addressV));
		mix(static_cast<uint64_t>(settings.addressW));
		mix(static_cast<uint64_t>(settings.borderColor));
		mix(static_cast<uint64_t>(settings.comparisonFunc));
		mix(static_cast<uint64_t>(settings.maxAnisotropy));
		mixFloat(settings.mipLODBias);
		mixFloat(settings.minLOD);
		mixFloat(settings.maxLOD);
	}
	return hash;
}
