#include "PipelineStateCache.h"

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

			outDesc.preRaster.file = vs->file;
			outDesc.preRaster.entry = ResolveEntryOrDefault(vs);
			outDesc.preRaster.profile = ResolveProfileOrDefault(Engine::ShaderStage::VS, vs);
			outDesc.pixel.file = ps->file;
			outDesc.pixel.entry = ResolveEntryOrDefault(ps);
			outDesc.pixel.profile = ResolveProfileOrDefault(Engine::ShaderStage::PS, ps);
			break;
		case Engine::PipelineType::Geometry:

			outDesc.preRaster.file = gs->file;
			outDesc.preRaster.entry = ResolveEntryOrDefault(gs);
			outDesc.preRaster.profile = ResolveProfileOrDefault(Engine::ShaderStage::GS, gs);
			outDesc.pixel.file = ps->file;
			outDesc.pixel.entry = ResolveEntryOrDefault(ps);
			outDesc.pixel.profile = ResolveProfileOrDefault(Engine::ShaderStage::PS, ps);
			break;
		case Engine::PipelineType::Mesh:

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
		const Engine::ShaderAsset& shaderAsset, Engine::ComputePipelineDesc& outDesc) {

		// 基本的な情報をセット
		outDesc = Engine::ComputePipelineDesc{};
		outDesc.staticSamplers = variant.staticSamplers;

		// シェーダーステージのエントリを取得
		const Engine::ShaderStageEntry* cs = Engine::FindShaderStage(shaderAsset, Engine::ShaderStage::CS);
		outDesc.compute.file = cs->file;
		outDesc.compute.entry = ResolveEntryOrDefault(cs);
		outDesc.compute.profile = ResolveProfileOrDefault(Engine::ShaderStage::CS, cs);
		return true;
	}
}

bool Engine::PipelineCacheKey::operator==(const PipelineCacheKey& rhs) const noexcept {
	return pipelineAsset == rhs.pipelineAsset &&
		resolvedKind == rhs.resolvedKind &&
		formatHash == rhs.formatHash &&
		meshEnabled == rhs.meshEnabled &&
		inlineRayTracingEnabled == rhs.inlineRayTracingEnabled &&
		dispatchRaysEnabled == rhs.dispatchRaysEnabled;
}

const Engine::PipelineState* Engine::PipelineStateCache::GetORCreate(GraphicsPlatform& graphicsPlatform,
	RenderAssetLibrary& assetLibrary, AssetID pipelineAssetID, PipelineVariantKind desiredKind,
	std::span<const DXGI_FORMAT> runtimeRTVFormats, DXGI_FORMAT runtimeDSVFormat) {

	// アセットライブラリからパイプラインアセットをロード
	const RenderPipelineAsset* pipelineAsset = assetLibrary.LoadPipeline(pipelineAssetID);
	// 存在しない場合はnullptrを返す
	if (!pipelineAsset) {
		return nullptr;
	}

	// GPUのランタイム機能を取得する
	const GraphicsRuntimeFeatures& runtimeFeatures = graphicsPlatform.GetFeatureController().GetRuntimeFeatures();
	// パイプラインアセットから、要求された種類とGPUの機能に最も適したバリアントを解決する
	const PipelineVariantDesc* variant = ResolveBestVariant(*pipelineAsset, desiredKind, runtimeFeatures);
	if (!variant) {
		return nullptr;
	}

	// キャッシュキーを構築して、キャッシュに存在するか確認する
	PipelineCacheKey key{};
	key.pipelineAsset = pipelineAssetID;
	key.resolvedKind = variant->kind;
	key.meshEnabled = runtimeFeatures.useMeshShader;
	key.inlineRayTracingEnabled = runtimeFeatures.useInlineRayTracing;
	key.dispatchRaysEnabled = runtimeFeatures.useDispatchRays;
	key.formatHash = HashFormats(runtimeRTVFormats, (variant->dsvFormat != DXGI_FORMAT_UNKNOWN) ? variant->dsvFormat : runtimeDSVFormat);

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
		// パイプラインステートオブジェクトを生成
		created = pipelineState->CreateGraphics(graphicsPlatform.GetDevice(),
			graphicsPlatform.GetDxShaderCompiler(), desc);
		break;
	}
	case PipelineVariantKind::Compute:
	{
		// コンピュートパイプラインの記述を構築
		ComputePipelineDesc desc{};
		if (!BuildComputePipelineDesc(*variant, *shaderAsset, desc)) {
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
	return it->second.get();
}

void Engine::PipelineStateCache::Clear() {

	cache_.clear();
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