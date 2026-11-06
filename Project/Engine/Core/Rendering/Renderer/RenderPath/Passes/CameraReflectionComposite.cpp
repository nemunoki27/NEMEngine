#include "CameraReflectionComposite.h"

//============================================================================
//	include
//============================================================================
#include "RenderFeatureResourceUtility.h"
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessExecutor.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureRuntimeOverrides.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetRegistry.h>

// c++
#include <string_view>

namespace {

	bool HasCompositeParameters(const Engine::MaterialParameterSet& parameters) {

		for (const auto& [name, value] : parameters) {
			if ((name != "reflectionMaxRoughness" && name != "reflectionRoughnessFade") ||
				!std::holds_alternative<float>(value.value)) return false;
		}
		return true;
	}

	bool HasCompositePipeline(Engine::RenderAssetLibrary& library, const Engine::MaterialAsset& material,
		Engine::AssetID pipelineID, Engine::AssetID shaderID, const char* entry, std::string_view& sourceFile) {

		if (material.domain != Engine::MaterialDomain::Compute || material.shaderGraph || material.passes.size() != 1) return false;
		const auto& binding = material.passes.front();
		if (binding.passKind != Engine::MaterialPassKind::PostProcess || binding.pipeline != pipelineID ||
			binding.preferredVariant != Engine::PipelineVariantKind::Compute || binding.shaderOverride) return false;
		const auto* pipeline = library.LoadPipeline(pipelineID);
		if (!pipeline || pipeline->variants.size() != 1) return false;
		const auto& variant = pipeline->variants.front();
		if (variant.kind != Engine::PipelineVariantKind::Compute || variant.pipelineType != Engine::PipelineType::Compute ||
			variant.shader != shaderID || !variant.staticSamplers.empty() || variant.requiresInlineRayTracing ||
			variant.requiresDispatchRays || variant.requiresMeshShader) return false;
		const auto* shader = library.LoadShader(shaderID);
		if (!shader || shader->stages.size() != 1 || !shader->parameters.empty() || !shader->colorParameters.empty()) return false;
		const auto& stage = shader->stages.front();
		if (stage.stage != Engine::ShaderStage::CS || stage.entry != entry || stage.profile != "cs_6_0") return false;
		sourceFile = stage.file;
		return !sourceFile.empty();
	}
}

bool Engine::TryCompositeCameraReflectionInPlace(GraphicsCore& graphicsCore, SceneExecutionContext& context,
	const RenderPipelineDeps& deps, const RenderFeaturePassSettings& pass,
	const RenderFeaturePassRuntimeOverride* runtimeOverride, MultiRenderTarget& sceneFinal,
	const std::string& sceneColorAlias) {

	if (pass.id != UUID{0x5254434f4d504f53ull} || pass.type != RenderFeaturePassType::Compute ||
		pass.material != BuiltinAssets::Materials::ReflectionComposite || pass.materialPass != MaterialPassKind::PostProcess ||
		pass.sourceKind != RenderFeatureSourceKind::SceneColor || pass.adaptiveResolution ||
		!pass.textureOverrides.empty() || !pass.samplerOverrides.empty() || pass.outputs.size() != 1 ||
		!HasCompositeParameters(pass.parameterOverrides)) return false;
	if (runtimeOverride && (runtimeOverride->enabled == false || !runtimeOverride->textureOverrides.empty() ||
		!HasCompositeParameters(runtimeOverride->parameters))) return false;
	const auto& output = pass.outputs.front();
	if (output.name != "Color" || output.shaderResource != "gDestColor" ||
		output.format != RenderFeatureTextureFormat::Inherit || output.widthScale != 1.0f || output.heightScale != 1.0f ||
		output.history || output.clearColor.has_value()) return false;
	if (context.useViewportRect && (context.viewportX != 0 || context.viewportY != 0 ||
		context.viewportWidth != sceneFinal.GetWidth() || context.viewportHeight != sceneFinal.GetHeight())) return false;
	const auto* target = sceneFinal.GetColorTexture(0);
	if (!target || target->GetFormat() != DXGI_FORMAT_R32G32B32A32_FLOAT || !target->GetUAVGPUHandle().ptr ||
		context.targetRegistry->FindColorByName(sceneColorAlias) != target) return false;

	const auto* material = deps.assetLibrary->LoadMaterial(pass.material);
	const auto* inPlaceMaterial = deps.assetLibrary->LoadMaterial(BuiltinAssets::Materials::ReflectionCompositeInPlace);
	if (!material || !inPlaceMaterial || !HasCompositeParameters(material->parameters) ||
		!material->parameters.contains("reflectionMaxRoughness") ||
		!material->parameters.contains("reflectionRoughnessFade")) return false;
	std::string_view sourceFile, inPlaceFile;
	if (!HasCompositePipeline(*deps.assetLibrary, *material, BuiltinAssets::Pipelines::ReflectionComposite,
		BuiltinAssets::Shaders::ReflectionComposite, "main", sourceFile) ||
		!HasCompositePipeline(*deps.assetLibrary, *inPlaceMaterial, BuiltinAssets::Pipelines::ReflectionCompositeInPlace,
			BuiltinAssets::Shaders::ReflectionCompositeInPlace, "mainInPlace", inPlaceFile) || sourceFile != inPlaceFile) return false;

	PostProcessExecutionDesc desc{};
	desc.material = BuiltinAssets::Materials::ReflectionCompositeInPlace;
	desc.source.colors = {sceneColorAlias};
	desc.dest.colors = {sceneColorAlias};
	desc.extraSources = pass.sceneInputs;
	for (const auto& [name, reference] : pass.passInputs) {
		desc.extraSources[name] = RenderFeatureResourceUtility::MakeOutputAlias(reference);
	}
	// 元Materialの設定を先に引き継いで実行時の上書きを重ねる
	desc.parameterOverrides = material->parameters;
	desc.parameterOverrides.MergeFrom(pass.parameterOverrides);
	if (runtimeOverride) desc.parameterOverrides.MergeFrom(runtimeOverride->parameters);
	desc.allowInPlace = true;
	if (!deps.postProcessExecutor->Execute(graphicsCore, {}, context, *deps.assetLibrary, *deps.pipelineCache, desc)) return false;
	const auto alias = RenderFeatureResourceUtility::MakeOutputAlias({pass.id, output.name});
	context.targetRegistry->Register(alias, &sceneFinal, {alias}, std::nullopt);
	return true;
}
