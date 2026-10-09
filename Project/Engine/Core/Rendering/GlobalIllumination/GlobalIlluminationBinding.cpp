#include "GlobalIlluminationView.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>

//============================================================================
//	GlobalIlluminationView classMethods
//============================================================================
bool Engine::GlobalIlluminationView::BindCompute(GraphicsCore& graphicsCore, const SceneExecutionContext& context,
	const ShaderReflectionInfo& reflection,
	const std::function<const RootBindingLocation*(std::string_view, ShaderBindingKind)>& findBinding, uint32_t destination) {

	auto* commandList = graphicsCore.GetDXObject().GetDxCommand()->GetCommandList();
	const auto& field = fields_[publishedField_];
	const std::array<const RenderTexture2D*, 4> readTextures = { &field.irradiance, &field.distance, &field.positions, &field.offsets };
	auto& output = fields_[destination];
	const std::array<RenderTexture2D*, 4> writeTextures = { &output.irradiance, &output.distance, &output.positions, &output.offsets };
	for (const auto& binding : reflection.resources) {

		const auto* location = findBinding(binding.name, binding.kind);
		if (!location || binding.kind == ShaderBindingKind::Sampler) continue;
		if (binding.space == 6u) {

			// Probe専用の定数と入出力を接続
			if (binding.kind == ShaderBindingKind::CBV) {
				RootBindingCommand::SetComputeCBV(commandList, location, constantsAddress_);
			} else if (binding.kind == ShaderBindingKind::SRV) {
				const auto handle = binding.bindPoint < 4u ? readTextures[binding.bindPoint]->GetSRVGPUHandle() : rayResults_.GetSRVGPUHandle();
				RootBindingCommand::SetComputeSRV(commandList, location, 0, handle);
			} else if (binding.kind == ShaderBindingKind::UAV) {
				const auto handle = binding.name == "gGIRayResults" ? rayResults_.GetUAVGPUHandle() : writeTextures[binding.bindPoint]->GetUAVGPUHandle();
				RootBindingCommand::SetComputeUAV(commandList, location, 0, handle);
			}
			continue;
		}
		// Bindless Textureは同じDescriptor Heapを参照
		if (binding.name.starts_with("gNEMGlobal")) {
			RootBindingCommand::SetComputeSRV(commandList, location, 0, graphicsCore.GetSRVDescriptor().GetGPUHandle(0));
			continue;
		}
		if (binding.name == "gSceneTLAS") {
			RootBindingCommand::SetComputeSRV(commandList, location, context.raytracing.tlasResource->GetGPUVirtualAddress(), {});
			continue;
		}
		if (binding.name == "ShaderGraphTimeConstants" && context.systemContext) {

			const auto& time = *context.systemContext;
			const ShaderGraphTimeConstantsGPU values{ time.time, time.deltaTime, time.smoothDeltaTime, time.unscaledTime };
			const auto allocation = constantAllocator_.AllocateAndUpload(graphicsCore.GetDXObject().GetResourceRetirement(),
				graphicsCore.GetDXObject().GetDevice(), values);
			RootBindingCommand::SetComputeCBV(commandList, location, allocation.gpuAddress);
			continue;
		}
		const auto* buffer = context.bufferRegistry.Find(binding.name);
		if (!buffer) {
			Logger::Output(LogType::Engine, spdlog::level::err, "GIのBufferが見つかりません: {}", binding.name);
			return false;
		}
		if (binding.kind == ShaderBindingKind::CBV) {
			RootBindingCommand::SetComputeCBV(commandList, location, buffer->gpuAddress);
		} else {
			RootBindingCommand::SetComputeSRV(commandList, location, buffer->gpuAddress, buffer->srvGPUHandle);
		}
	}
	return true;
}

void Engine::GlobalIlluminationView::BindLighting(GraphicsCore& graphicsCore, const PipelineState& pipeline) const {

	// このCameraで確定したProbeだけを公開
	auto* commandList = graphicsCore.GetDXObject().GetDxCommand()->GetCommandList();
	const auto* constants = pipeline.FindBinding(ShaderBindingKind::CBV, 0, 6);
	if (constants) RootBindingCommand::SetGraphicsCBV(commandList, constants, constantsAddress_);
	const auto& field = fields_[publishedField_];
	const std::array<const RenderTexture2D*, 4> textures = { &field.irradiance, &field.distance, &field.positions, &field.offsets };
	for (uint32_t index = 0; index < textures.size(); ++index) {

		const auto* binding = pipeline.FindBinding(ShaderBindingKind::SRV, index, 6);
		if (binding) RootBindingCommand::SetGraphicsSRV(commandList, binding, 0, textures[index]->GetSRVGPUHandle());
	}
}

void Engine::GlobalIlluminationView::BindReflection(GraphicsCore& graphicsCore, const RaytracingPipelineState& pipeline) const {

	// Cameraで確定したProbeをDXRへ接続
	auto* commandList = graphicsCore.GetDXObject().GetDxCommand()->GetCommandList();
	if (const auto* binding = pipeline.FindBindingByName("GlobalIlluminationConstants", ShaderBindingKind::CBV)) {

		RootBindingCommand::SetComputeCBV(commandList, binding, constantsAddress_);
	}
	const auto& field = fields_[publishedField_];
	const std::array<const RenderTexture2D*, 4> textures = { &field.irradiance, &field.distance, &field.positions, &field.offsets };
	const std::array<std::string_view, 4> names = { "gGIIrradiance", "gGIDistance", "gGIPositions", "gGIOffsets" };
	for (size_t index = 0; index < textures.size(); ++index) {

		if (const auto* binding = pipeline.FindBindingByName(names[index], ShaderBindingKind::SRV)) {

			RootBindingCommand::SetComputeSRV(commandList, binding, 0, textures[index]->GetSRVGPUHandle());
		}
	}
}
