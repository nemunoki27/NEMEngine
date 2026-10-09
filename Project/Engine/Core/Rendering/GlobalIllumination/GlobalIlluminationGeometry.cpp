#include "GlobalIlluminationGeometry.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphArtifactCache.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateBuilder.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>

//============================================================================
//	GlobalIlluminationGeometry classMethods
//============================================================================
void Engine::GlobalIlluminationGeometry::BeginFrame() {

	allocator_.BeginFrame();
	const uint64_t frame = GraphicsFrameState::GetFrameSerial();
	const auto removeUnused = [frame](auto& entries) {

		std::erase_if(entries, [frame](const auto& entry) { return frame > entry.second->lastFrame + 120u; });
	};
	removeUnused(meshEntries_);
	removeUnused(primitiveEntries_);
}

void Engine::GlobalIlluminationGeometry::Clear() {

	// BLASと頂点は既存の回収窓口へ渡す
	meshEntries_.clear();
	primitiveEntries_.clear();
	pipelines_.clear();
	copyPipeline_.reset();
	allocator_.Release();
}

Engine::GlobalIlluminationGeometry::PipelineEntry* Engine::GlobalIlluminationGeometry::FindPipeline(
	GraphicsCore& graphicsCore, RenderAssetLibrary& library, const MaterialAsset& material) {

	if (!material.shaderGraph) return nullptr;
	const auto found = pipelines_.find(material.shaderGraph);
	if (found != pipelines_.end()) return &found->second;
	const auto id = ShaderGraphArtifactCache::MakeDerivedID(material.shaderGraph, 0x4749564552544558ull);
	const auto* shader = library.LoadShader(id);
	if (!shader || shader->stages.empty()) return nullptr;
	const auto& stage = shader->stages[0];
	ComputePipelineDesc description;
	description.compute = { .file = stage.file, .entry = stage.entry, .profile = stage.profile, .shader = id };
	// GraphのSampler設定を通常描画から引き継ぐ
	const auto* pass = FindPass(material, MaterialPassKind::RayTracing);
	const auto* graphPipeline = pass ? library.LoadPipeline(pass->pipeline) : nullptr;
	if (graphPipeline && !graphPipeline->variants.empty()) description.staticSamplers = graphPipeline->variants[0].staticSamplers;
	PipelineEntry entry;
	auto& platform = graphicsCore.GetDXObject();
	entry.pipeline = PipelineStateBuilder::CreateCompute(platform.GetResourceRetirement(), platform.GetDevice(),
		platform.GetDxShaderCompiler(), description, shader);
	if (!entry.pipeline) return nullptr;
	const auto& reflection = entry.pipeline->GetComputeReflection();
	entry.layout.Build(reflection, FindStructuredBuffer(reflection, "gMeshMaterialParameters") ?
		"gMeshMaterialParameters" : "MaterialParameters");
	return &pipelines_.emplace(material.shaderGraph, std::move(entry)).first->second;
}

D3D12_GPU_VIRTUAL_ADDRESS Engine::GlobalIlluminationGeometry::Upload(GraphicsCore& graphicsCore,
	const void* bytes, size_t size) {

	return allocator_.AllocateAndUploadBytes(graphicsCore.GetDXObject().GetResourceRetirement(),
		graphicsCore.GetDXObject().GetDevice(), { static_cast<const uint8_t*>(bytes), size }).gpuAddress;
}

bool Engine::GlobalIlluminationGeometry::Dispatch(GraphicsCore& graphicsCore, SceneExecutionContext& context,
	PipelineState& pipeline, GeometryEntry& entry, uint32_t descriptor, uint32_t offset, uint32_t count, uint32_t subMesh) {

	// 各dispatchへ別の定数領域を割り当てる
	const std::array<uint32_t, 4> constants{ descriptor, offset, count, subMesh };
	const auto address = Upload(graphicsCore, constants.data(), sizeof(constants));
	auto& command = *graphicsCore.GetDXObject().GetDxCommand();
	auto* list = command.GetCommandList();
	command.SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });
	entry.vertices.Transition(command, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	list->SetComputeRootSignature(pipeline.GetRootSignature());
	list->SetPipelineState(pipeline.GetComputePipeline());
	for (const auto& binding : pipeline.GetComputeReflection().resources) {

		if (binding.kind == ShaderBindingKind::Sampler) continue;
		const auto* location = pipeline.FindBindingByName(binding.name, binding.kind);
		if (!location) continue;
		if (binding.space == 7u) {

			if (binding.kind == ShaderBindingKind::CBV) RootBindingCommand::SetComputeCBV(list, location, address);
			else RootBindingCommand::SetComputeUAV(list, location, entry.vertices.GetGPUAddress(), entry.vertices.GetUAVGPUHandle());
			continue;
		}
		if (binding.name == "ShaderGraphTimeConstants" && context.systemContext) {

			const auto& time = *context.systemContext;
			const ShaderGraphTimeConstantsGPU values{ time.time, time.deltaTime, time.smoothDeltaTime, time.unscaledTime };
			RootBindingCommand::SetComputeCBV(list, location, Upload(graphicsCore, &values, sizeof(values)));
			continue;
		}
		const auto* buffer = context.bufferRegistry.Find(binding.name);
		if (!buffer) {

			Logger::Output(LogType::Engine, spdlog::level::err, "GI頂点のBufferが見つかりません: {}", binding.name);
			return false;
		}
		if (binding.kind == ShaderBindingKind::CBV) RootBindingCommand::SetComputeCBV(list, location, buffer->gpuAddress);
		else RootBindingCommand::SetComputeSRV(list, location, buffer->gpuAddress, buffer->srvGPUHandle);
	}
	list->Dispatch((count + 63u) / 64u, 1, 1);
	command.UAVBarrier(entry.vertices.GetResource());
	return true;
}

bool Engine::GlobalIlluminationGeometry::CopyVertices(GraphicsCore& graphicsCore, SceneExecutionContext& context,
	GeometryEntry& entry, uint32_t descriptor, uint32_t offset, uint32_t count) {

	if (!copyPipeline_) {

		ComputePipelineDesc description;
		description.compute = { .file = "Builtin/GlobalIllumination/giVertexCopy.CS.hlsl", .entry = "main",
			.profile = "cs_6_6", .shader = BuiltinAssets::Shaders::GIVertexCopy };
		auto& platform = graphicsCore.GetDXObject();
		copyPipeline_ = PipelineStateBuilder::CreateCompute(platform.GetResourceRetirement(), platform.GetDevice(),
			platform.GetDxShaderCompiler(), description);
		if (!copyPipeline_) return false;
	}
	entry.vertices.Init(graphicsCore.GetDXObject().GetDevice(), &graphicsCore.GetSRVDescriptor());
	entry.vertices.EnsureCapacity(count);
	entry.lastFrame = GraphicsFrameState::GetFrameSerial();
	return Dispatch(graphicsCore, context, *copyPipeline_, entry, descriptor, offset, count, 0);
}
