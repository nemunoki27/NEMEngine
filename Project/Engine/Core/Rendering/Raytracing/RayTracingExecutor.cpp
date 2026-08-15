#include "RayTracingExecutor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterBufferBuilder.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingPipelineStateCache.h>
#include <Engine/Core/Rendering/Raytracing/RayTracingRuntimeOverrides.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Backends/Core/IRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTargetCopyUtility.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>

// c++
#include <algorithm>

namespace {

	constexpr const char* kParameterBufferName = "RayTracingParameters";
	constexpr const char* kShaderGraphTimeBufferName =
		"ShaderGraphTimeConstants";

	Engine::RayTracingTextureSource ResolveInputSource(
		const Engine::RayTracingEffectSettings& effect,
		std::string_view shaderResource,
		bool& found) {

		for (const Engine::RayTracingInputBinding& input : effect.inputs) {
			if (input.shaderResource == shaderResource) {
				found = true;
				return input.source;
			}
		}
		found = false;
		return Engine::RayTracingTextureSource::SceneColor;
	}

	Engine::RenderTexture2D* ResolveColorSource(
		const Engine::SceneExecutionContext& context,
		Engine::RayTracingTextureSource source) {

		if (!context.resources) {
			return nullptr;
		}
		switch (source) {
		case Engine::RayTracingTextureSource::SceneColor:
			return context.resources->GetSceneColorOpaque()->GetColorTexture(0);
		case Engine::RayTracingTextureSource::GBufferAlbedo:
			return context.resources->GetGBufferAlbedo();
		case Engine::RayTracingTextureSource::GBufferNormal:
			return context.resources->GetGBufferNormal();
		case Engine::RayTracingTextureSource::GBufferPosition:
			return context.resources->GetGBufferPosition();
		case Engine::RayTracingTextureSource::GBufferMaterial:
			return context.resources->GetGBufferMaterial();
		case Engine::RayTracingTextureSource::GBufferEmissive:
			return context.resources->GetGBufferEmissive();
		case Engine::RayTracingTextureSource::GBufferFlags:
			return context.resources->GetGBufferFlags();
		case Engine::RayTracingTextureSource::SceneDepth:
			return nullptr;
		}
		return nullptr;
	}

	Engine::DepthTexture2D* ResolveDepthSource(
		const Engine::SceneExecutionContext& context,
		Engine::RayTracingTextureSource source) {

		if (!context.resources ||
			source != Engine::RayTracingTextureSource::SceneDepth) {
			return nullptr;
		}
		return context.resources->GetSceneMain()->GetDepthTexture();
	}

	Engine::AssetID ResolveTextureAsset(
		const Engine::RayTracingEffectSettings& effect,
		std::string_view shaderResource) {

		for (const Engine::RayTracingTextureBinding& texture : effect.textures) {
			if (texture.shaderResource == shaderResource) {
				return texture.texture;
			}
		}
		return {};
	}
}

//============================================================================
//	RayTracingExecutor classMethods
//============================================================================
void Engine::RayTracingExecutor::BeginFrame() {

	const uint64_t frameSerial = GraphicsFrameState::GetFrameSerial();
	if (allocatorFrameSerial_ == frameSerial) {
		return;
	}
	allocatorFrameSerial_ = frameSerial;
	constantBufferAllocator_.BeginFrame();
}

void Engine::RayTracingExecutor::Release() {

	constantBufferAllocator_.Release();
	parameterLayoutCache_.clear();
	diagnostics_.clear();
	allocatorFrameSerial_ = 0;
}

void Engine::RayTracingExecutor::ReportFailure(
	const RayTracingEffectSettings& effect, std::string_view reason) {

	std::string key = effect.name;
	key.push_back('|');
	key.append(reason);
	if (!diagnostics_.emplace(std::move(key)).second) {
		return;
	}
	Logger::Output(LogType::Engine, spdlog::level::err,
		"[RayTracing] {}: {}", effect.name, reason);
}

bool Engine::RayTracingExecutor::Execute(
	GraphicsCore& graphicsCore,
	const SceneExecutionContext& context,
	RenderAssetLibrary& assetLibrary,
	RaytracingPipelineStateCache& pipelineCache,
	const RayTracingEffectSettings& effect,
	const RayTracingEffectRuntimeOverride* runtimeOverride) {

	if (!context.resources || !context.assetDatabase || !effect.material ||
		!context.raytracing.tlasResource) {
		ReportFailure(effect, "required scene resources are unavailable");
		return false;
	}
	const MaterialAsset* material = assetLibrary.LoadMaterial(effect.material);
	if (!material) {
		ReportFailure(effect, "material asset could not be loaded");
		return false;
	}
	const MaterialPassBinding* pass = FindPass(
		*material, MaterialPassKind::RayTracing);
	if (!pass || pass->preferredVariant != PipelineVariantKind::Raytracing) {
		ReportFailure(effect, "material has no RayTracing pass");
		return false;
	}
	RaytracingPipelineState* pipeline = pipelineCache.GetOrCreate(
		graphicsCore.GetDXObject(), assetLibrary,
		pass->pipeline, pass->shaderOverride);
	if (!pipeline) {
		ReportFailure(effect, "raytracing pipeline creation failed");
		return false;
	}

	MultiRenderTarget* sceneFinal = context.resources->GetSceneFinal();
	RenderTexture2D* destColor = sceneFinal ?
		sceneFinal->GetColorTexture(0) : nullptr;
	if (!destColor || destColor->GetUAVGPUHandle().ptr == 0) {
		ReportFailure(effect, "scene output UAV is unavailable");
		return false;
	}

	bool needsSceneColorCopy = false;
	for (const RayTracingInputBinding& input : effect.inputs) {
		needsSceneColorCopy |=
			input.source == RayTracingTextureSource::SceneColor;
	}
	if (needsSceneColorCopy) {
		MultiRenderTargetCopy::CopyColor0Resource(graphicsCore,
			sceneFinal, context.resources->GetSceneColorOpaque());
		context.resources->GetSceneColorOpaque()->TransitionForShaderRead(
			*graphicsCore.GetDXObject().GetDxCommand());
	}

	BeginFrame();
	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	auto* commandList = dxCommand->GetCommandList();
	dxCommand->SetDescriptorHeaps({
		graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });
	commandList->SetComputeRootSignature(pipeline->GetRootSignature());
	commandList->SetPipelineState1(pipeline->GetStateObject());

	const ShaderReflectionInfo& reflection = pipeline->GetReflection();
	for (const ShaderResourceBinding& binding : reflection.resources) {

		const RootBindingLocation* location = pipeline->FindBindingByName(
			binding.name, binding.kind);
		if (!location || binding.kind == ShaderBindingKind::Sampler) {
			continue;
		}
		if (binding.kind == ShaderBindingKind::CBV &&
			(binding.name == kParameterBufferName ||
			 binding.name == kShaderGraphTimeBufferName)) {

			continue;
		}
		if (binding.kind == ShaderBindingKind::AccelStruct ||
			binding.kind == ShaderBindingKind::CBV ||
			(binding.kind == ShaderBindingKind::SRV &&
				(binding.rawType == D3D_SIT_STRUCTURED ||
					binding.rawType == D3D_SIT_BYTEADDRESS))) {

			const RegisteredRenderBuffer* registered =
				context.bufferRegistry.Find(binding.name);
			if (registered) {
				if (binding.kind == ShaderBindingKind::CBV) {
					RootBindingCommand::SetComputeCBV(commandList,
						location, registered->gpuAddress);
				} else {
					RootBindingCommand::SetComputeSRV(commandList,
						location, registered->gpuAddress,
						registered->srvGPUHandle);
				}
				continue;
			}
			ReportFailure(effect,
				"unresolved buffer: " + binding.name);
			return false;
		}

		if (binding.kind == ShaderBindingKind::UAV) {
			destColor->Transition(*dxCommand,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			RootBindingCommand::SetComputeUAV(commandList, location,
				0, destColor->GetUAVGPUHandle());
			continue;
		}
		if (binding.kind != ShaderBindingKind::SRV) {
			continue;
		}

		bool hasSource = false;
		const RayTracingTextureSource source = ResolveInputSource(
			effect, binding.name, hasSource);
		if (hasSource) {
			if (RenderTexture2D* color = ResolveColorSource(context, source)) {
				color->Transition(*dxCommand,
					D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				RootBindingCommand::SetComputeSRV(commandList, location,
					0, color->GetSRVGPUHandle());
				continue;
			}
			if (DepthTexture2D* depth = ResolveDepthSource(context, source)) {
				depth->Transition(*dxCommand,
					D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				RootBindingCommand::SetComputeSRV(commandList, location,
					0, depth->GetSRVGPUHandle());
				continue;
			}
		}

		const AssetID textureAsset = ResolveTextureAsset(effect, binding.name);
		const GPUTextureResource* texture = RuntimeTextureResolver::Resolve(
			graphicsCore, context.assetDatabase, textureAsset);
		if (!texture || !texture->valid) {
			ReportFailure(effect,
				"unresolved SRV: " + binding.name);
			return false;
		}
		RootBindingCommand::SetComputeSRV(commandList, location,
			0, texture->gpuHandle);
	}

	auto layout = parameterLayoutCache_.find(pipeline->GetUniqueID());
	if (layout == parameterLayoutCache_.end()) {
		MaterialParameterLayout created{};
		created.Build(reflection, kParameterBufferName);
		layout = parameterLayoutCache_.emplace(
			pipeline->GetUniqueID(), std::move(created)).first;
	}
	if (layout->second.IsValid()) {
		MaterialAsset merged = *material;
		for (const auto& [name, value] : effect.parameterOverrides) {
			merged.parameters[name] = value;
		}
		if (runtimeOverride) {
			for (const MaterialParameterRecord& parameter :
				runtimeOverride->parameters.GetRecords()) {

				merged.parameters.Set(parameter.id,
					parameter.namedValue.first, parameter.semantic,
					parameter.namedValue.second);
			}
		}
		const auto resolveTexture = [&](MaterialParameterSemantic semantic,
			const AssetID& textureAssetID) {

			const RuntimeTextureResolver::BindlessResolveResult resolved =
				RuntimeTextureResolver::ResolveBindless(graphicsCore,
					context.assetDatabase, textureAssetID,
					IsSRGBMaterialTexture(semantic));
			return MaterialParameterBufferBuilder::TextureResolveResult{
				.index = resolved.srvIndex,
				.cacheable = !resolved.retry,
			};
		};
		const std::vector<uint8_t> bytes = MaterialParameterBufferBuilder::Build(
			merged, layout->second, resolveTexture);
		const PostProcessConstantBufferAllocation allocation =
			constantBufferAllocator_.AllocateAndUploadBytes(
				graphicsCore.GetDXObject().GetDevice(), bytes);
		const RootBindingLocation* parameterBinding =
			pipeline->FindBindingByName(kParameterBufferName,
				ShaderBindingKind::CBV);
		if (parameterBinding && allocation.gpuAddress) {
			RootBindingCommand::SetComputeCBV(commandList,
				parameterBinding, allocation.gpuAddress);
		}
	}

	const RootBindingLocation* timeBinding =
		pipeline->FindBindingByName(kShaderGraphTimeBufferName,
			ShaderBindingKind::CBV);
	if (timeBinding && context.systemContext) {
		const ShaderGraphTimeConstantsGPU constants{
			.time = context.systemContext->time,
			.deltaTime = context.systemContext->deltaTime,
			.smoothDeltaTime = context.systemContext->smoothDeltaTime,
			.unscaledTime = context.systemContext->unscaledTime,
		};
		const PostProcessConstantBufferAllocation allocation =
			constantBufferAllocator_.AllocateAndUpload(
				graphicsCore.GetDXObject().GetDevice(), constants);
		if (allocation.gpuAddress) {
			RootBindingCommand::SetComputeCBV(commandList,
				timeBinding, allocation.gpuAddress);
		}
	}

	D3D12_DISPATCH_RAYS_DESC dispatch = pipeline->BuildDispatchDesc(
		sceneFinal->GetWidth(), sceneFinal->GetHeight(), 1,
		effect.rayGenerationIndex);
	if (dispatch.Width == 0 || dispatch.Height == 0) {
		ReportFailure(effect, "ray generation index is out of range");
		return false;
	}
	commandList->DispatchRays(&dispatch);
	dxCommand->UAVBarrier(destColor->GetResource());
	destColor->Transition(*dxCommand,
		static_cast<D3D12_RESOURCE_STATES>(
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
	return true;
}
