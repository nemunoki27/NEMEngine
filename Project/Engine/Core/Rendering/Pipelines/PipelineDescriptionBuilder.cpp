#include "PipelineDescriptionBuilder.h"

//============================================================================
//	include
//============================================================================
#include <algorithm>

namespace Engine::PipelineDescriptionBuilder {

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
