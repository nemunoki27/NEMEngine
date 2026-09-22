#include "RaytracingSceneResult.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>

void Engine::RaytracingSceneResult::Init(GraphicsCore& graphicsCore) {

	sceneInstances_.Init(graphicsCore.GetDXObject().GetDevice(), &graphicsCore.GetSRVDescriptor());
	sceneGeometries_.Init(graphicsCore.GetDXObject().GetDevice(), &graphicsCore.GetSRVDescriptor());
	sceneSubMeshes_.Init(graphicsCore.GetDXObject().GetDevice(), &graphicsCore.GetSRVDescriptor());
	// 事前にある程度の容量を確保しておく
	sceneInstances_.EnsureCapacity(256);
	sceneGeometries_.EnsureCapacity(256);
	sceneSubMeshes_.EnsureCapacity(256);
	sceneInstanceScratch_.reserve(256);
	sceneGeometryScratch_.reserve(256);
	sceneSubMeshScratch_.reserve(256);

}

void Engine::RaytracingSceneResult::Release() {

	sceneInstances_.Release();
	sceneGeometries_.Release();
	sceneSubMeshes_.Release();
	sceneInstanceScratch_.clear();
	sceneGeometryScratch_.clear();
	sceneSubMeshScratch_.clear();
	scenePickRecords_.clear();
	scenePickRecordOffsets_.clear();
	sceneUploadFrameSerials_ = { 0, 0, 0 };
}

void Engine::RaytracingSceneResult::Upload() {

	sceneInstances_.Upload(sceneInstanceScratch_);
	sceneGeometries_.Upload(sceneGeometryScratch_);
	sceneSubMeshes_.Upload(sceneSubMeshScratch_);
	sceneUploadFrameSerials_[GraphicsFrameState::GetCurrentIndex()] =
		GraphicsFrameState::GetFrameSerial();
}

void Engine::RaytracingSceneResult::UploadCached() {

	const uint32_t frameIndex = GraphicsFrameState::GetCurrentIndex();
	const uint64_t frameSerial = GraphicsFrameState::GetFrameSerial();
	if (sceneUploadFrameSerials_[frameIndex] == frameSerial) {
		return;
	}

	sceneInstances_.Upload(sceneInstanceScratch_);
	sceneGeometries_.Upload(sceneGeometryScratch_);
	sceneSubMeshes_.Upload(sceneSubMeshScratch_);
	sceneUploadFrameSerials_[frameIndex] = frameSerial;
}

void Engine::RaytracingSceneResult::Publish(SceneExecutionContext& context,
	ID3D12Resource* tlas, uint64_t materialGeneration, bool texturesReady) const {

	// RaytracingSceneInstances
	if (sceneInstances_.GetResource()) {
		context.bufferRegistry.Register({
			.alias = "RaytracingSceneInstances",
			.resource = sceneInstances_.GetResource(),
			.gpuAddress = sceneInstances_.GetGPUAddress(),
			.srvGPUHandle = sceneInstances_.GetGPUHandle(),
			.elementCount = static_cast<uint32_t>(sceneInstanceScratch_.size()),
			.stride = sizeof(RaytracingInstanceShaderData),
			});
		context.bufferRegistry.Register({
			.alias = "gRaytracingSceneInstances",
			.resource = sceneInstances_.GetResource(),
			.gpuAddress = sceneInstances_.GetGPUAddress(),
			.srvGPUHandle = sceneInstances_.GetGPUHandle(),
			.elementCount = static_cast<uint32_t>(sceneInstanceScratch_.size()),
			.stride = sizeof(RaytracingInstanceShaderData),
		});
	}

	// RaytracingGeometries
	if (sceneGeometries_.GetResource()) {
		context.bufferRegistry.Register({
			.alias = "RaytracingGeometries",
			.resource = sceneGeometries_.GetResource(),
			.gpuAddress = sceneGeometries_.GetGPUAddress(),
			.srvGPUHandle = sceneGeometries_.GetGPUHandle(),
			.elementCount = static_cast<uint32_t>(sceneGeometryScratch_.size()),
			.stride = sizeof(RaytracingGeometryShaderData),
			});
		context.bufferRegistry.Register({
			.alias = "gRaytracingGeometries",
			.resource = sceneGeometries_.GetResource(),
			.gpuAddress = sceneGeometries_.GetGPUAddress(),
			.srvGPUHandle = sceneGeometries_.GetGPUHandle(),
			.elementCount = static_cast<uint32_t>(sceneGeometryScratch_.size()),
			.stride = sizeof(RaytracingGeometryShaderData),
			});
	}

	// RaytracingSubMeshes
	if (sceneSubMeshes_.GetResource()) {
		context.bufferRegistry.Register({
			.alias = "RaytracingSubMeshes",
			.resource = sceneSubMeshes_.GetResource(),
			.gpuAddress = sceneSubMeshes_.GetGPUAddress(),
			.srvGPUHandle = sceneSubMeshes_.GetGPUHandle(),
			.elementCount = static_cast<uint32_t>(sceneSubMeshScratch_.size()),
			.stride = sizeof(MeshSubMeshShaderData),
			});
		context.bufferRegistry.Register({
			.alias = "gRaytracingSubMeshes",
			.resource = sceneSubMeshes_.GetResource(),
			.gpuAddress = sceneSubMeshes_.GetGPUAddress(),
			.srvGPUHandle = sceneSubMeshes_.GetGPUHandle(),
			.elementCount = static_cast<uint32_t>(sceneSubMeshScratch_.size()),
			.stride = sizeof(MeshSubMeshShaderData),
			});
	}
	context.raytracing.tlasResource = tlas;
	context.raytracing.instanceCount = static_cast<uint32_t>(sceneInstanceScratch_.size());
	context.raytracing.materialGeneration = materialGeneration;
	context.raytracing.materialTexturesReady =
		texturesReady;
}
