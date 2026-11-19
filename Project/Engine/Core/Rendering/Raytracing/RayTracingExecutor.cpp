#include "RayTracingExecutor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterBufferBuilder.h>
#include <Engine/Core/Rendering/Materials/MaterialResolver.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingPipelineStateCache.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureRuntimeOverrides.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Backends/Core/IRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshDrawPathCommon.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetRegistry.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>

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
	shaderGraphFrameConstants_.BeginFrame();
}

void Engine::RayTracingExecutor::Release() {

	constantBufferAllocator_.Release();
	shaderGraphFrameConstants_.Release();
	parameterLayoutCache_.clear();
	diagnostics_.clear();
	preparedScene_ = {};
	geometryMaterials_.clear();
	hitMaterials_.clear();
	hitRecordPipeline_ = 0;
	allocatorFrameSerial_ = 0;
	lastReflection_ = nullptr;
	lastExecutedPipelineID_ = 0;
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
	const bool materialHits = materialPass->pipeline == BuiltinAssets::Pipelines::RaytracingReflection &&
		!materialPass->shaderOverride;
	if (materialHits && !std::ranges::equal(geometryMaterials_, context.raytracing.hitMaterials)) {
		geometryMaterials_.assign(context.raytracing.hitMaterials.begin(), context.raytracing.hitMaterials.end());
		hitMaterials_.clear();
		for (const auto& materialHit : geometryMaterials_) {
			if (materialHit.pipeline && (materialHit.shader || materialHit.pipeline != BuiltinAssets::Pipelines::RaytracingReflection)) {
				hitMaterials_.emplace_back(materialHit);
			}
		}
		std::sort(hitMaterials_.begin(), hitMaterials_.end());
		hitMaterials_.erase(std::unique(hitMaterials_.begin(), hitMaterials_.end()), hitMaterials_.end());
		hitRecordPipeline_ = 0;
	}
	// 初回描画で非表示のRendererが参照する材質も登録する
	const bool prepareScene = materialHits && context.sceneInstance && context.world &&
		preparedScene_ != context.sceneInstance->instanceID;
	if (prepareScene) {
		preparedScene_ = context.sceneInstance->instanceID;
		std::unordered_set<AssetID> materialIDs;
		const MaterialResolver resolver{};
		context.world->ForEach<MeshRendererComponent>([&](Entity entity, const MeshRendererComponent& renderer) {
			materialIDs.insert(resolver.ResolveORDefault(*context.assetDatabase, renderer.material, DefaultMaterialSlot::Mesh));
			for (const auto& subMesh : GetMeshSubMeshes(std::as_const(*context.world), entity)) {
				const auto state = MeshDrawPathCommon::ResolveSubMeshRenderState(renderer, subMesh);
				materialIDs.insert(resolver.ResolveORDefault(*context.assetDatabase, state.material, DefaultMaterialSlot::Mesh));
			}
		});
		context.world->ForEach<PrimitiveRendererComponent>([&](Entity, const PrimitiveRendererComponent& renderer) {
			materialIDs.insert(resolver.ResolveORDefault(*context.assetDatabase, renderer.material, DefaultMaterialSlot::Primitive));
		});
		for (AssetID id : materialIDs) {
			if (!id) continue;
			const auto* asset = assetLibrary.LoadMaterial(id);
			const auto* hit = asset ? FindPass(*asset, MaterialPassKind::RayTracing) : nullptr;
			if (hit && hit->pipeline && (hit->shaderOverride || hit->pipeline != BuiltinAssets::Pipelines::RaytracingReflection)) {
				hitMaterials_.push_back({ hit->shaderOverride, hit->pipeline });
			}
		}
	}
	std::span<const RaytracingHitMaterial> activeMaterials;
	RaytracingPipelineState* pipeline = materialHits ?
		pipelineCache.GetMaterialReflection(graphicsCore.GetDXObject(), assetLibrary, samplerOverrides, hitMaterials_, prepareScene, activeMaterials) :
		pipelineCache.GetOrCreate(graphicsCore.GetDXObject(), assetLibrary,
			materialPass->pipeline, materialPass->shaderOverride, &samplerOverrides);
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
	lastExecutedPipelineID_ = pipeline->GetUniqueID();
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
		if (binding.kind == ShaderBindingKind::CBV && context.raytracingViewConstants &&
			(binding.name == "RaytracingViewConstants" || binding.name == "gRaytracingViewConstants")) {
			auto constants = *context.raytracingViewConstants;
			// 解像度の復元後も実行中のViewに合わせる
			if (context.view) {
				constants.renderSize = Vector2(static_cast<float>((std::max)(context.view->width, 1u)),
					static_cast<float>((std::max)(context.view->height, 1u)));
				constants.invRenderSize = Vector2(1.0f / constants.renderSize.x, 1.0f / constants.renderSize.y);
				if (const auto* camera = context.view->FindCamera(RenderCameraDomain::Perspective)) {
					constants.view = camera->matrices.viewMatrix;
					constants.projection = camera->matrices.projectionMatrix;
					constants.inverseView = camera->matrices.inverseViewMatrix;
					constants.inverseProjection = camera->matrices.inverseProjectionMatrix;
					constants.inverseViewProjection = Matrix4x4::Inverse(camera->matrices.viewProjectionMatrix);
					constants.cameraPos = camera->cameraPos;
					constants.nearClip = camera->nearClip;
					constants.farClip = camera->farClip;
				}
			}
			const auto allocation = constantBufferAllocator_.AllocateAndUpload(graphicsCore.GetDXObject().GetResourceRetirement(),
				graphicsCore.GetDXObject().GetDevice(), constants);
			RootBindingCommand::SetComputeCBV(commandList, location, allocation.gpuAddress);
			continue;
		}
		if (binding.kind == ShaderBindingKind::AccelStruct ||
			binding.kind == ShaderBindingKind::CBV ||
			(binding.kind == ShaderBindingKind::SRV &&
				(binding.rawType == D3D_SIT_STRUCTURED ||
					binding.rawType == D3D_SIT_BYTEADDRESS))) {

			if (binding.name == "gSceneTLAS" && !materialHits && pipeline->GetHitGroupCount() > 1) {
				auto* legacy = context.raytracing.resolveLegacyTLAS ? context.raytracing.resolveLegacyTLAS(graphicsCore) : nullptr;
				if (!legacy) {
					ReportFailure(pass, "legacy acceleration structure is unavailable");
					return false;
				}
				RootBindingCommand::SetComputeSRV(commandList, location, legacy->GetGPUVirtualAddress(), {});
				continue;
			}
			const RegisteredRenderBuffer* registered =
				context.bufferRegistry.Find(binding.name);
			if (registered) {
				// 未転送の定数をGPUへ渡さない
				if (binding.kind == ShaderBindingKind::CBV && !registered->gpuAddress) {
					ReportFailure(pass, "constant buffer is not uploaded: " + binding.name);
					return false;
				}
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
	if (timeBinding && context.systemContext && context.view) {
		const auto& output = resources.dispatchTarget->GetRenderTarget();
		const Vector4 viewport = materialHits ? Vector4{} :
			Vector4(0.0f, 0.0f, static_cast<float>(output.width), static_cast<float>(output.height));
		const auto address = shaderGraphFrameConstants_.GetAddress(graphicsCore, *context.systemContext, *context.view,
			RenderCameraDomain::Perspective, viewport);
		if (address) {
			RootBindingCommand::SetComputeCBV(commandList,
				timeBinding, address);
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
	if (materialHits && !geometryMaterials_.empty()) {
		if (hitRecordPipeline_ != pipeline->GetUniqueID()) {
			hitRecords_.resize(geometryMaterials_.size() * RaytracingPipelineState::kRecordStride);
			for (size_t index = 0; index < geometryMaterials_.size(); ++index) {
				const auto& hit = geometryMaterials_[index];
				const auto group = std::lower_bound(activeMaterials.begin(), activeMaterials.end(), hit);
				const uint32_t slot = group != activeMaterials.end() && *group == hit ?
					static_cast<uint32_t>(1 + (group - activeMaterials.begin())) : 0u;
				const auto identifier = pipeline->GetHitGroupIdentifier(slot);
				std::ranges::copy(identifier, hitRecords_.begin() + index * RaytracingPipelineState::kRecordStride);
			}
			hitRecordPipeline_ = pipeline->GetUniqueID();
		}
		const auto table = constantBufferAllocator_.AllocateAndUploadBytes(graphicsCore.GetDXObject().GetResourceRetirement(),
			graphicsCore.GetDXObject().GetDevice(), hitRecords_);
		dispatch.HitGroupTable.StartAddress = table.gpuAddress;
		dispatch.HitGroupTable.SizeInBytes = hitRecords_.size();
		dispatch.HitGroupTable.StrideInBytes = RaytracingPipelineState::kRecordStride;
	}
	commandList->DispatchRays(&dispatch);
	outputTextureScratch_.clear();
	outputTextureScratch_.reserve(resources.outputs.size());
	for (const auto& [name, output] : resources.outputs) {

		if (!output) {
			continue;
		}
		outputTextureScratch_.push_back(output);
	}
	// 全出力の用途遷移で後続の読み取りを同期する
	RenderTexture2D::TransitionAll(*dxCommand, outputTextureScratch_, static_cast<D3D12_RESOURCE_STATES>(
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
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
