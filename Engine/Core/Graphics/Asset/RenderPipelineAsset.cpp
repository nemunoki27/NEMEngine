#include "RenderPipelineAsset.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Utility/Enum/EnumAdapter.h>

//============================================================================
//	RenderPipelineAsset classMethods
//============================================================================

namespace {

	// JSONからD3D12_RASTERIZER_DESCをパースする関数
	D3D12_RASTERIZER_DESC ParseRasterizer(const nlohmann::json& data) {

		D3D12_RASTERIZER_DESC desc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

		if (!data.is_object()) {
			return desc;
		}

		desc.FillMode = EnumAdapter<D3D12_FILL_MODE>::FromString(data.value("fillMode",
			std::string(EnumAdapter<D3D12_FILL_MODE>::ToString(desc.FillMode)))).value_or(desc.FillMode);
		desc.CullMode = EnumAdapter<D3D12_CULL_MODE>::FromString(data.value("cullMode",
			std::string(EnumAdapter<D3D12_CULL_MODE>::ToString(desc.CullMode)))).value_or(desc.CullMode);
		desc.FrontCounterClockwise = data.value("frontCounterClockwise", desc.FrontCounterClockwise != 0) ? TRUE : FALSE;
		desc.DepthClipEnable = data.value("depthClipEnable", desc.DepthClipEnable != 0) ? TRUE : FALSE;
		desc.MultisampleEnable = data.value("multisampleEnable", desc.MultisampleEnable != 0) ? TRUE : FALSE;
		desc.AntialiasedLineEnable = data.value("antialiasedLineEnable", desc.AntialiasedLineEnable != 0) ? TRUE : FALSE;
		return desc;
	}
	// JSONからD3D12_DEPTH_STENCIL_DESCをパースする関数
	D3D12_DEPTH_STENCIL_DESC ParseDepthStencil(const nlohmann::json& data) {

		D3D12_DEPTH_STENCIL_DESC desc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);

		if (!data.is_object()) {
			return desc;
		}

		desc.DepthEnable = data.value("depthEnable", desc.DepthEnable != 0) ? TRUE : FALSE;
		desc.DepthWriteMask = EnumAdapter<D3D12_DEPTH_WRITE_MASK>::FromString(data.value("depthWriteMask",
			std::string(EnumAdapter<D3D12_DEPTH_WRITE_MASK>::ToString(desc.DepthWriteMask)))).value_or(desc.DepthWriteMask);
		desc.DepthFunc = EnumAdapter<D3D12_COMPARISON_FUNC>::FromString(data.value("depthFunc",
			std::string(EnumAdapter<D3D12_COMPARISON_FUNC>::ToString(desc.DepthFunc)))).value_or(desc.DepthFunc);
		desc.StencilEnable = data.value("stencilEnable", desc.StencilEnable != 0) ? TRUE : FALSE;
		return desc;
	}
	// JSONからD3D12_STATIC_SAMPLER_DESCをパースする関数
	D3D12_STATIC_SAMPLER_DESC ParseStaticSampler(const nlohmann::json& data) {

		D3D12_STATIC_SAMPLER_DESC sampler{};
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler.MipLODBias = 0.0f;
		sampler.MaxAnisotropy = 1;
		sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
		sampler.MinLOD = 0.0f;
		sampler.MaxLOD = D3D12_FLOAT32_MAX;
		sampler.ShaderRegister = 0;
		sampler.RegisterSpace = 0;
		sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		if (!data.is_object()) {
			return sampler;
		}

		sampler.Filter = EnumAdapter<D3D12_FILTER>::FromString(data.value("filter",
			std::string(EnumAdapter<D3D12_FILTER>::ToString(sampler.Filter)))).value_or(sampler.Filter);
		sampler.AddressU = EnumAdapter<D3D12_TEXTURE_ADDRESS_MODE>::FromString(data.value("addressU",
			std::string(EnumAdapter<D3D12_TEXTURE_ADDRESS_MODE>::ToString(sampler.AddressU)))).value_or(sampler.AddressU);
		sampler.AddressV = EnumAdapter<D3D12_TEXTURE_ADDRESS_MODE>::FromString(data.value("addressV",
			std::string(EnumAdapter<D3D12_TEXTURE_ADDRESS_MODE>::ToString(sampler.AddressV)))).value_or(sampler.AddressV);
		sampler.AddressW = EnumAdapter<D3D12_TEXTURE_ADDRESS_MODE>::FromString(data.value("addressW",
			std::string(EnumAdapter<D3D12_TEXTURE_ADDRESS_MODE>::ToString(sampler.AddressW)))).value_or(sampler.AddressW);
		sampler.ComparisonFunc = EnumAdapter<D3D12_COMPARISON_FUNC>::FromString(data.value("comparisonFunc",
			std::string(EnumAdapter<D3D12_COMPARISON_FUNC>::ToString(sampler.ComparisonFunc)))).value_or(sampler.ComparisonFunc);
		sampler.ShaderVisibility = EnumAdapter<D3D12_SHADER_VISIBILITY>::FromString(data.value("shaderVisibility",
			std::string(EnumAdapter<D3D12_SHADER_VISIBILITY>::ToString(sampler.ShaderVisibility)))).value_or(sampler.ShaderVisibility);
		sampler.ShaderRegister = data.value("shaderRegister", 0u);
		sampler.RegisterSpace = data.value("registerSpace", 0u);
		sampler.MaxAnisotropy = data.value("maxAnisotropy", sampler.MaxAnisotropy);
		sampler.MipLODBias = data.value("mipLODBias", sampler.MipLODBias);
		sampler.MinLOD = data.value("minLOD", sampler.MinLOD);
		sampler.MaxLOD = data.value("maxLOD", sampler.MaxLOD);
		return sampler;
	}
	// パイプラインバリアントがランタイムの機能と互換性があるかどうかをチェックする関数
	bool IsCompatible(const PipelineVariantDesc& variant, const GraphicsRuntimeFeatures& runtimeFeatures) {

		if (variant.requiresMeshShader && !runtimeFeatures.useMeshShader) {
			return false;
		}
		if (variant.requiresInlineRayTracing && !runtimeFeatures.useInlineRayTracing) {
			return false;
		}
		if (variant.requiresDispatchRays && !runtimeFeatures.useDispatchRays) {
			return false;
		}
		switch (variant.kind) {
		case PipelineVariantKind::GraphicsMesh:

			return !variant.requiresMeshShader || runtimeFeatures.useMeshShader;
		case PipelineVariantKind::Raytracing:

			return runtimeFeatures.useDispatchRays;
		}
		return true;
	}
	// 指定された順序で互換性のある最初のパイプラインバリアントを検索する関数
	const PipelineVariantDesc* FindFirstCompatible(const RenderPipelineAsset& asset,
		const std::vector<PipelineVariantKind>& order, const GraphicsRuntimeFeatures& runtimeFeatures) {

		for (PipelineVariantKind kind : order) {
			for (const auto& variant : asset.variants) {
				if (variant.kind != kind) {
					continue;
				}
				if (!IsCompatible(variant, runtimeFeatures)) {
					continue;
				}
				return &variant;
			}
		}
		return nullptr;
	}
}

bool Engine::FromJson(const nlohmann::json& data, RenderPipelineAsset& outAsset) {

	if (!data.is_object()) {
		return false;
	}

	outAsset = RenderPipelineAsset{};
	outAsset.guid = ParseAssetID(data, "guid");
	outAsset.name = data.value("name", "UnnamedPipeline");
	if (data.contains("variants") && data["variants"].is_array()) {
		for (const auto& item : data["variants"]) {

			if (!item.is_object()) {
				continue;
			}

			PipelineVariantDesc variant{};
			variant.kind = EnumAdapter<PipelineVariantKind>::FromString(item.value("kind",
				"GraphicsVertex")).value_or(PipelineVariantKind::GraphicsVertex);
			variant.pipelineType = EnumAdapter<PipelineType>::FromString(item.value("pipelineType",
				"Vertex")).value_or(PipelineType::Vertex);
			variant.topologyType = EnumAdapter<D3D12_PRIMITIVE_TOPOLOGY_TYPE>::FromString(item.value("topologyType",
				std::string(EnumAdapter<D3D12_PRIMITIVE_TOPOLOGY_TYPE>::ToString(variant.topologyType)))).value_or(variant.topologyType);
			variant.dsvFormat = EnumAdapter<DXGI_FORMAT>::FromString(item.value("dsvFormat",
				std::string(EnumAdapter<DXGI_FORMAT>::ToString(variant.dsvFormat)))).value_or(variant.dsvFormat);
			variant.shader = ParseAssetID(item, "shader");
			variant.rasterizer = ParseRasterizer(item.value("rasterizer", nlohmann::json::object()));
			variant.depthStencil = ParseDepthStencil(item.value("depthStencil", nlohmann::json::object()));
			variant.numRenderTargets = item.value("numRenderTargets", 1u);
			variant.dynamicRenderTargetFormats = item.value("dynamicRenderTargetFormats", false);
			variant.requiresMeshShader = item.value("requiresMeshShader", false);
			variant.requiresInlineRayTracing = item.value("requiresInlineRayTracing", true);
			variant.requiresDispatchRays = item.value("requiresDispatchRays", true);
			variant.rayGenerationExport = item.value("rayGenerationExport", "");
			variant.missExport = item.value("missExport", "");
			variant.closestHitExport = item.value("closestHitExport", "");
			variant.anyHitExport = item.value("anyHitExport", "");
			variant.hitGroupExport = item.value("hitGroupExport", "DefaultHitGroup");
			variant.maxPayloadSizeInBytes = item.value("maxPayloadSizeInBytes", 16u);
			variant.maxAttributeSizeInBytes = item.value("maxAttributeSizeInBytes",
				static_cast<uint32_t>(D3D12_RAYTRACING_MAX_ATTRIBUTE_SIZE_IN_BYTES));
			variant.maxRecursionDepth = item.value("maxRecursionDepth", 1u);
			if (item.contains("rtvFormats") && item["rtvFormats"].is_array()) {
				for (const auto& formatJson : item["rtvFormats"]) {

					if (!formatJson.is_string()) {
						continue;
					}
					auto format = EnumAdapter<DXGI_FORMAT>::FromString(formatJson.get<std::string>());
					if (format.has_value()) {
						variant.rtvFormats.emplace_back(*format);
					}
				}
			}
			if (item.contains("staticSamplers") && item["staticSamplers"].is_array()) {
				for (const auto& samplerJson : item["staticSamplers"]) {
					variant.staticSamplers.emplace_back(ParseStaticSampler(samplerJson));
				}
			}
			if (!variant.shader) {
				continue;
			}
			outAsset.variants.emplace_back(std::move(variant));
		}
	}
	return true;
}

nlohmann::json Engine::ToJson(const RenderPipelineAsset& asset) {

	nlohmann::json data = nlohmann::json::object();

	data["guid"] = ToString(asset.guid);
	data["name"] = asset.name;
	data["variants"] = nlohmann::json::array();
	for (const auto& variant : asset.variants) {

		nlohmann::json item = nlohmann::json::object();

		item["kind"] = EnumAdapter<PipelineVariantKind>::ToString(variant.kind);
		item["pipelineType"] = EnumAdapter<PipelineType>::ToString(variant.pipelineType);
		item["shader"] = ToString(variant.shader);
		item["topologyType"] = EnumAdapter<D3D12_PRIMITIVE_TOPOLOGY_TYPE>::ToString(variant.topologyType);
		item["numRenderTargets"] = variant.numRenderTargets;
		item["dsvFormat"] = EnumAdapter<DXGI_FORMAT>::ToString(variant.dsvFormat);
		item["dynamicRenderTargetFormats"] = variant.dynamicRenderTargetFormats;
		item["requiresMeshShader"] = variant.requiresMeshShader;
		item["requiresInlineRayTracing"] = variant.requiresInlineRayTracing;
		item["requiresDispatchRays"] = variant.requiresDispatchRays;
		item["rayGenerationExport"] = variant.rayGenerationExport;
		item["missExport"] = variant.missExport;
		item["closestHitExport"] = variant.closestHitExport;
		item["anyHitExport"] = variant.anyHitExport;
		item["hitGroupExport"] = variant.hitGroupExport;
		item["maxPayloadSizeInBytes"] = variant.maxPayloadSizeInBytes;
		item["maxAttributeSizeInBytes"] = variant.maxAttributeSizeInBytes;
		item["maxRecursionDepth"] = variant.maxRecursionDepth;
		item["rtvFormats"] = nlohmann::json::array();
		for (DXGI_FORMAT format : variant.rtvFormats) {
			item["rtvFormats"].push_back(EnumAdapter<DXGI_FORMAT>::ToString(format));
		}
		data["variants"].push_back(item);
	}
	return data;
}

const Engine::PipelineVariantDesc* Engine::ResolveBestVariant(const RenderPipelineAsset& asset,
	PipelineVariantKind desiredKind, const GraphicsRuntimeFeatures& runtimeFeatures) {

	if (asset.variants.empty()) {
		return nullptr;
	}

	// メッシュシェーダーが使用できない場合、GraphicsMeshをGraphicsVertexとして扱う
	PipelineVariantKind effectiveDesired = desiredKind;
	if (desiredKind == PipelineVariantKind::GraphicsMesh && !runtimeFeatures.useMeshShader) {
		effectiveDesired = PipelineVariantKind::GraphicsVertex;
	}
	{
		const auto* exact = FindFirstCompatible(asset, { effectiveDesired }, runtimeFeatures);
		if (exact) {
			return exact;
		}
	}
	// 要求された種類に基づいて、互換性のあるバリアントを優先的に検索する
	switch (effectiveDesired) {
	case PipelineVariantKind::GraphicsVertex:
	case PipelineVariantKind::GraphicsGeometry:
	case PipelineVariantKind::GraphicsMesh:
		if (runtimeFeatures.useMeshShader) {
			if (const auto* mesh = FindFirstCompatible(asset, { PipelineVariantKind::GraphicsMesh }, runtimeFeatures)) {
				return mesh;
			}
		}
		if (const auto* vertex = FindFirstCompatible(asset, { PipelineVariantKind::GraphicsVertex, PipelineVariantKind::GraphicsGeometry }, runtimeFeatures)) {
			return vertex;
		}
		break;

	case PipelineVariantKind::Compute:
		if (const auto* compute = FindFirstCompatible(asset, { PipelineVariantKind::Compute }, runtimeFeatures)) {
			return compute;
		}
		break;

	case PipelineVariantKind::Raytracing:
		if (const auto* ray = FindFirstCompatible(asset, { PipelineVariantKind::Raytracing }, runtimeFeatures)) {
			return ray;
		}
		break;
	}
	for (const auto& variant : asset.variants) {
		if (IsCompatible(variant, runtimeFeatures)) {
			return &variant;
		}
	}
	return &asset.variants.front();
}