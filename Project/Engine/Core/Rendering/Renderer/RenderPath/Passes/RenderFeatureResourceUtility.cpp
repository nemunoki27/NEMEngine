#include "RenderFeatureResourceUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Rendering/Profiling/GPUFrameProfiler.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessBindingNames.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessExecutor.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessTemporaryTargetPool.h>
#include <Engine/Core/Rendering/Raytracing/RayTracingExecutor.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureRuntimeOverrides.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingPipelineStateCache.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileRuntime.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileService.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPassExecutionHelper.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTargetCopyUtility.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetNames.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetRegistry.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphBindingNames.h>

// c++
#include <algorithm>
#include <string_view>

namespace Engine::RenderFeatureResourceUtility {

	constexpr std::string_view kDefaultOutputName = "Color";

	std::string MakeStateKey(Engine::RenderViewKind kind,
		Engine::UUID passID, std::string_view output) {

		return std::to_string(static_cast<uint32_t>(kind)) + "_" +
			std::to_string(passID.value) + "_" + std::string(output);
	}

	DXGI_FORMAT ToDXGIFormat(
		Engine::RenderFeatureTextureFormat format,
		DXGI_FORMAT inheritedFormat) {

		switch (format) {
		case Engine::RenderFeatureTextureFormat::Inherit:
			return inheritedFormat;
		case Engine::RenderFeatureTextureFormat::R8_UNORM:
			return DXGI_FORMAT_R8_UNORM;
		case Engine::RenderFeatureTextureFormat::R16_FLOAT:
			return DXGI_FORMAT_R16_FLOAT;
		case Engine::RenderFeatureTextureFormat::RG16_FLOAT:
			return DXGI_FORMAT_R16G16_FLOAT;
		case Engine::RenderFeatureTextureFormat::RGBA16_FLOAT:
			return DXGI_FORMAT_R16G16B16A16_FLOAT;
		case Engine::RenderFeatureTextureFormat::R32_FLOAT:
			return DXGI_FORMAT_R32_FLOAT;
		case Engine::RenderFeatureTextureFormat::RG32_FLOAT:
			return DXGI_FORMAT_R32G32_FLOAT;
		case Engine::RenderFeatureTextureFormat::RGBA32_FLOAT:
			return DXGI_FORMAT_R32G32B32A32_FLOAT;
		}
		return inheritedFormat;
	}

	std::string MakeOutputAlias(
		const Engine::RenderFeatureOutputReference& reference) {

		if (!reference.pass) {
			return Engine::RenderTargetNames::kSceneColorFinal;
		}
		return "RenderFeature_" + std::to_string(reference.pass.value) +
			"_" + (reference.output.empty() ?
				std::string(kDefaultOutputName) : reference.output);
	}

	Engine::RenderFeatureOutputSettings GetPrimaryOutput(
		const Engine::RenderFeaturePassSettings& pass) {

		if (!pass.outputs.empty()) {
			return pass.outputs.front();
		}
		return Engine::RenderFeatureOutputSettings{};
	}

	Engine::MultiRenderTarget* ResolveOutputTarget(
		Engine::RenderTargetRegistry& registry,
		const Engine::RenderFeatureOutputReference& reference) {

		return registry.Find(MakeOutputAlias(reference));
	}

	void ApplyDefaultSceneInputs(
		std::unordered_map<std::string, std::string>& inputs) {

		const auto setDefault = [&](std::string_view resource,
			std::string_view target) {

			inputs.try_emplace(std::string(resource), std::string(target));
		};
		setDefault(Engine::PostProcessBindingNames::kSourceDepth,
			Engine::RenderTargetNames::kSceneDepth);
		setDefault(Engine::PostProcessBindingNames::kSourceAlbedo,
			Engine::RenderTargetNames::kSceneColorMain);
		setDefault("gSourceNormal",
			Engine::RenderTargetNames::kSceneNormalMain);
		setDefault("gSourcePosition",
			Engine::RenderTargetNames::kScenePositionMain);
		setDefault("gSourceMaterial",
			Engine::RenderTargetNames::kSceneMaterialMain);
		setDefault(Engine::PostProcessBindingNames::kSourceFlags,
			Engine::RenderTargetNames::kSceneFlagsMain);
		setDefault(Engine::PostProcessBindingNames::kSourceMotion,
			Engine::RenderTargetNames::kSceneMotionMain);
		setDefault(Engine::ShaderGraphBindingNames::kSceneColor,
			Engine::RenderTargetNames::kSceneColorOpaque);
		setDefault(Engine::ShaderGraphBindingNames::kSceneDepth,
			Engine::RenderTargetNames::kSceneDepth);
		setDefault(Engine::ShaderGraphBindingNames::kSceneNormal,
			Engine::RenderTargetNames::kSceneNormalMain);
		setDefault(Engine::ShaderGraphBindingNames::kScenePosition,
			Engine::RenderTargetNames::kScenePositionMain);
		setDefault(Engine::ShaderGraphBindingNames::kSceneMaterial,
			Engine::RenderTargetNames::kSceneMaterialMain);
		setDefault(Engine::ShaderGraphBindingNames::kSceneEmissive,
			Engine::RenderTargetNames::kSceneEmissiveMain);
		setDefault(Engine::ShaderGraphBindingNames::kSceneFlags,
			Engine::RenderTargetNames::kSceneFlagsMain);
	}

	void ClearOutputTargets(Engine::GraphicsCore& graphicsCore,
		const std::unordered_map<std::string,
			Engine::MultiRenderTarget*>& outputTargets) {

		for (const auto& [name, target] : outputTargets) {
			if (!target || !target->GetColorTexture(0)) {
				continue;
			}
			target->TransitionForRender(
				*graphicsCore.GetDXObject().GetDxCommand());
			target->Clear(*graphicsCore.GetDXObject().GetDxCommand(),
				Engine::MultiRenderTargetClearDesc{
					.clearColor = true,
					.clearColorValue = Engine::Color4::Black(),
				});
		}
	}

	bool CanCopyColor(const Engine::MultiRenderTarget* source,
		const Engine::MultiRenderTarget* destination) {

		if (!source || !destination || !source->GetColorTexture(0) ||
			!destination->GetColorTexture(0)) {
			return false;
		}
		return source->GetWidth() == destination->GetWidth() &&
			source->GetHeight() == destination->GetHeight() &&
			source->GetColorTexture(0)->GetFormat() ==
			destination->GetColorTexture(0)->GetFormat();
	}
}
