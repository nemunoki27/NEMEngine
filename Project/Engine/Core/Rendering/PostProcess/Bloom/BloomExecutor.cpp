#include "BloomExecutor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/DeferredRenderPath.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessExecutor.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessTemporaryTargetPool.h>
#include <Engine/Core/Rendering/PostProcess/Color/ColorPipelineProcessor.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetRegistry.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>

// c++
#include <array>
#include <cmath>

bool Engine::BloomExecutor::Execute(GraphicsCore& graphicsCore, SceneExecutionContext& context, const RenderPipelineDeps& deps,
	UUID passID, const std::string& sourceAlias, const std::string& outputAlias, const BloomSettings& settings) {

	auto& registry = *context.targetRegistry;
	RenderTargetSetReference sourceReference;
	sourceReference.colors = {sourceAlias};
	auto* source = registry.Resolve(sourceReference);
	if (!source) return false;
	// Bloomで増えた輝度を自動露出の測定へ混ぜない
	if (!deps.colorPipelineProcessor->PrepareExposure(graphicsCore, context, source, *deps.assetLibrary,
		*deps.pipelineCache, context.postProcessCamera.colorPipeline)) return false;

	const auto sizes = BuildBloomLevelSizes(source->GetWidth(), source->GetHeight());
	const std::string prefix = "Bloom." + ToString(passID) + ".";
	std::array<std::string, 5> down, up;
	MaterialParameterSet parameters;
	WriteBloomSettings(parameters, settings);
	// 実際の描画は共通のCompute実行経路へ渡す
	const auto dispatch = [&](AssetID material, const std::string& input, const std::string& output,
		const std::string& extra = {}) {
		PostProcessExecutionDesc desc;
		desc.material = material;
		desc.source.colors = {input};
		desc.dest.colors = {output};
		desc.parameterOverrides = parameters;
		desc.samplerOverrides["gBloomSampler"] = PipelineStaticSamplerSettings{};
		if (!extra.empty()) desc.extraSources["gBloomColor"] = extra;
		return deps.postProcessExecutor->Execute(graphicsCore, RenderFrameRequest{}, context, *deps.assetLibrary, *deps.pipelineCache, desc);
	};
	// 同じPassの中間画像を毎フレーム再利用する
	const auto acquire = [&](const std::string& name, uint32_t level) {
		PostProcessTemporaryTargetDesc desc;
		desc.name = name;
		desc.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		desc.widthScale = desc.heightScale = std::ldexp(1.0f, -static_cast<int>(level + 1));
		return deps.postProcessTargetPool->Acquire(graphicsCore, registry, desc, *source);
	};
	for (size_t level = 0; level < sizes.size(); ++level) {
		down[level] = prefix + "Down" + std::to_string(level);
		if (!acquire(down[level], static_cast<uint32_t>(level))) return false;
		if (!dispatch(level == 0 ? BuiltinAssets::Materials::BloomPrefilter : BuiltinAssets::Materials::BloomDownsample,
			level == 0 ? sourceAlias : down[level - 1], down[level])) return false;
	}

	// 小さい画像から戻し、強いにじみと広い光を重ねる
	std::string blurred = down[sizes.size() - 1];
	for (size_t level = sizes.size() - 1; 0 < level; --level) {
		up[level - 1] = prefix + "Up" + std::to_string(level - 1);
		if (!acquire(up[level - 1], static_cast<uint32_t>(level - 1))) return false;
		if (!dispatch(BuiltinAssets::Materials::BloomUpsample, down[level - 1], up[level - 1], blurred)) return false;
		blurred = up[level - 1];
	}
	// 元のHDR画像はここで一度だけ加算する
	return dispatch(BuiltinAssets::Materials::BloomComposite, sourceAlias, outputAlias, blurred);
}
