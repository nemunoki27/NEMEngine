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
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureRuntimeOverrides.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Backends/Core/IRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetRegistry.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>

// c++
#include <algorithm>

namespace {

	constexpr const char* kParameterBufferName = "RayTracingParameters";
	constexpr const char* kShaderGraphTimeBufferName =
		"ShaderGraphTimeConstants";

	Engine::AssetID ResolveTextureAsset(
		const Engine::RenderFeaturePassSettings& pass,
		const Engine::RenderFeaturePassRuntimeOverride* runtimeOverride,
		std::string_view shaderResource) {

		if (runtimeOverride) {
			const auto runtime = runtimeOverride->textureOverrides.find(
				std::string(shaderResource));
			if (runtime != runtimeOverride->textureOverrides.end()) {
				return runtime->second;
			}
		}

		const auto found = pass.textureOverrides.find(
			std::string(shaderResource));
		return found == pass.textureOverrides.end() ?
			Engine::AssetID{} : found->second;
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
	lastReflection_ = nullptr;
}

void Engine::RayTracingExecutor::ReportFailure(
	const RenderFeaturePassSettings& pass, std::string_view reason) {

	std::string key = pass.name;
	key.push_back('|');
	key.append(reason);
	if (!diagnostics_.emplace(std::move(key)).second) {
		return;
	}
	Logger::Output(LogType::Engine, spdlog::level::err,
		"[レンダー機能/レイトレーシング] {}: {}", pass.name, reason);
}

bool Engine::RayTracingExecutor::Execute(
	GraphicsCore& graphicsCore,
	const SceneExecutionContext& context,
	RenderAssetLibrary& assetLibrary,
	RaytracingPipelineStateCache& pipelineCache,
	const RenderFeaturePassSettings& pass,
	const RayTracingExecutionResources& resources,
	const RenderFeaturePassRuntimeOverride* runtimeOverride) {

	if (!context.resources || !context.assetDatabase || !pass.material ||
		!context.raytracing.tlasResource) {
		ReportFailure(pass, "required scene resources are unavailable");
		return false;
	}
	const MaterialAsset* material = assetLibrary.LoadMaterial(pass.material);
	if (!material) {
		ReportFailure(pass, "material asset could not be loaded");
		return false;
	}
	const MaterialPassBinding* materialPass = FindPass(
		*material, pass.materialPass);
	if (!materialPass ||
		materialPass->preferredVariant != PipelineVariantKind::Raytracing) {

		ReportFailure(pass, "material has no RayTracing pass");
		return false;
	}
	PipelineStaticSamplerOverrideSet samplerOverrides{};
	samplerOverrides.fillMissingSamplers = true;
	samplerOverrides.byName = pass.samplerOverrides;
	RaytracingPipelineState* pipeline = pipelineCache.GetOrCreate(
		graphicsCore.GetDXObject(), assetLibrary,
		materialPass->pipeline, materialPass->shaderOverride,
		&samplerOverrides);
	if (!pipeline) {
		ReportFailure(pass, "raytracing pipeline creation failed");
		return false;
	}
	if (resources.outputs.empty() || !resources.dispatchTarget) {
		ReportFailure(pass, "output UAV is unavailable");
		return false;
	}

	BeginFrame();
	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	auto* commandList = dxCommand->GetCommandList();
	dxCommand->SetDescriptorHeaps({
		graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });
	commandList->SetComputeRootSignature(pipeline->GetRootSignature());
	commandList->SetPipelineState1(pipeline->GetStateObject());

	const ShaderReflectionInfo& reflection = pipeline->GetReflection();
	lastReflection_ = &reflection;
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
			ReportFailure(pass,
				"unresolved buffer: " + binding.name);
			return false;
		}

		if (binding.kind == ShaderBindingKind::UAV) {
			const auto output = resources.outputs.find(binding.name);
			if (output == resources.outputs.end() || !output->second ||
				output->second->GetUAVGPUHandle().ptr == 0) {

				ReportFailure(pass, "unresolved UAV: " + binding.name);
				return false;
			}
			output->second->Transition(*dxCommand,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			RootBindingCommand::SetComputeUAV(commandList, location,
				0, output->second->GetUAVGPUHandle());
			continue;
		}
		if (binding.kind != ShaderBindingKind::SRV) {
			continue;
		}

		const auto input = resources.inputs.find(binding.name);
		if (input != resources.inputs.end() && context.targetRegistry) {
			if (RenderTexture2D* color =
				context.targetRegistry->FindColorByName(input->second)) {

				color->Transition(*dxCommand,
					D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				RootBindingCommand::SetComputeSRV(commandList, location,
					0, color->GetSRVGPUHandle());
				continue;
			}
			if (DepthTexture2D* depth =
				context.targetRegistry->FindDepthByName(input->second)) {

				depth->Transition(*dxCommand,
					D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				RootBindingCommand::SetComputeSRV(commandList, location,
					0, depth->GetSRVGPUHandle());
				continue;
			}
		}

		const AssetID textureAsset = ResolveTextureAsset(
			pass, runtimeOverride, binding.name);
		const GPUTextureResource* texture = RuntimeTextureResolver::Resolve(
			graphicsCore, context.assetDatabase, textureAsset);
		if (!texture || !texture->valid) {
			ReportFailure(pass,
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
		merged.parameters.MergeFrom(pass.parameterOverrides);
		if (runtimeOverride) {
			merged.parameters.MergeFrom(runtimeOverride->parameters);
		}
		const auto resolveTexture = [&](MaterialParameterSemantic semantic,
			const AssetID& textureAssetID) {

			const RuntimeTextureResolver::BindlessResolveResult resolved =
				RuntimeTextureResolver::ResolveBindless(graphicsCore,
					context.assetDatabase, textureAssetID,
					IsSRGBMaterialTexture(semantic) ?
					TextureColorSpace::SRGB : TextureColorSpace::Linear);
			return MaterialParameterBufferBuilder::TextureResolveResult{
				.index = resolved.srvIndex,
				.cacheable = !resolved.retry,
			};
		};
		const std::vector<uint8_t> bytes = MaterialParameterBufferBuilder::Build(
			merged, layout->second, resolveTexture);
		const FrameConstantBufferAllocation allocation =
			constantBufferAllocator_.AllocateAndUploadBytes(graphicsCore.GetDXObject().GetResourceRetirement(),
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
		const FrameConstantBufferAllocation allocation =
			constantBufferAllocator_.AllocateAndUpload(graphicsCore.GetDXObject().GetResourceRetirement(),
			graphicsCore.GetDXObject().GetDevice(), constants);
		if (allocation.gpuAddress) {
			RootBindingCommand::SetComputeCBV(commandList,
				timeBinding, allocation.gpuAddress);
		}
	}

	D3D12_DISPATCH_RAYS_DESC dispatch = pipeline->BuildDispatchDesc(
		resources.dispatchTarget->GetRenderTarget().width,
		resources.dispatchTarget->GetRenderTarget().height, 1,
		pass.rayGenerationIndex);
	if (dispatch.Width == 0 || dispatch.Height == 0) {
		ReportFailure(pass, "ray generation index is out of range");
		return false;
	}
	commandList->DispatchRays(&dispatch);
	for (const auto& [name, output] : resources.outputs) {

		if (!output) {
			continue;
		}
		dxCommand->UAVBarrier(output->GetResource());
		output->Transition(*dxCommand,
			static_cast<D3D12_RESOURCE_STATES>(
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
	}
	return true;
}

//============================================================================
//	RayTracingExecutor classMethods
//============================================================================

namespace Engine {

	void RayTracingExecutor::ClearParameterLayoutCache() {

		parameterLayoutCache_.clear();
		diagnostics_.clear();
	}
}
