#include "RaytracingSceneResult.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>

void Engine::RaytracingSceneResult::Init(GraphicsCore& graphicsCore) {

	auto* device = graphicsCore.GetDXObject().GetDevice();
	auto* descriptors = &graphicsCore.GetSRVDescriptor();
	auto* uploader = &graphicsCore.GetBufferUploadService();
	sceneInstances_.Init(device, descriptors, uploader);
	sceneGeometries_.Init(device, descriptors, uploader);
	sceneSubMeshes_.Init(device, descriptors, uploader);
	materialParameters_.Init(device, descriptors, uploader);
	sceneInstanceScratch_.reserve(256);
	sceneGeometryScratch_.reserve(256);
	sceneSubMeshScratch_.reserve(256);

}

void Engine::RaytracingSceneResult::Release() {

	sceneInstances_.Release();
	sceneGeometries_.Release();
	sceneSubMeshes_.Release();
	materialParameters_.Release();
	materialParameterWords_.clear();
	hitMaterials_.clear();
	sceneInstanceScratch_.clear();
	sceneGeometryScratch_.clear();
	sceneGeometryBounds_.clear();
	instanceWorldMatrices_.clear();
	sceneSubMeshScratch_.clear();
	scenePickRecords_.clear();
	scenePickRecordOffsets_.clear();
	sceneUploadFrameSerials_ = {};
}

void Engine::RaytracingSceneResult::Upload() {

	instanceMatricesDirty_ = true;
	if (materialParameterWords_.empty()) materialParameterWords_.resize(4);
	sceneInstances_.MarkFullUpdate(static_cast<uint32_t>(sceneInstanceScratch_.size()));
	sceneGeometries_.MarkFullUpdate(static_cast<uint32_t>(sceneGeometryScratch_.size()));
	sceneSubMeshes_.MarkFullUpdate(static_cast<uint32_t>(sceneSubMeshScratch_.size()));
	materialParameters_.MarkFullUpdate(static_cast<uint32_t>(materialParameterWords_.size()));
	commonLayers_ = 0x00ffffffu;
	for (const auto& instance : sceneInstanceScratch_) commonLayers_ &= instance.renderFlags >> 8u;
	UploadCached();
}

void Engine::RaytracingSceneResult::UploadCached() {

	const uint32_t frame = GraphicsFrameState::GetCurrentIndex();
	sceneUploadFrameSerials_[frame] = GraphicsFrameState::GetFrameSerial();
	sceneInstances_.UploadCurrentFrame(sceneInstanceScratch_);
	sceneGeometries_.UploadCurrentFrame(sceneGeometryScratch_);
	sceneSubMeshes_.UploadCurrentFrame(sceneSubMeshScratch_);
	materialParameters_.UploadCurrentFrame(materialParameterWords_);
}

void Engine::RaytracingSceneResult::Publish(SceneExecutionContext& context,
	ID3D12Resource* tlas, uint64_t materialGeneration, bool texturesReady, std::span<const RaytracingTLASInstance> instances) {

	// 再利用中のTLASも現在の配置で投影候補を選別する
	if (instanceMatricesDirty_ || instanceWorldMatrices_.size() != instances.size()) {
		instanceWorldMatrices_.resize(instances.size());
		for (size_t index = 0; index < instances.size(); ++index) instanceWorldMatrices_[index] = instances[index].worldMatrix;
		instanceMatricesDirty_ = false;
	}
	context.raytracing.instanceWorldMatrices = instanceWorldMatrices_;
	context.raytracing.geometryBounds = sceneGeometryBounds_;
	context.raytracing.commonLayers = commonLayers_;
	context.raytracing.probeReceiversCovered = probeReceiversCovered_;
	context.raytracing.instanceData = sceneInstanceScratch_;
	context.raytracing.hitMaterials = hitMaterials_;
	if (!materialParameterWords_.empty()) {
		context.bufferRegistry.Register({.alias = "gRaytracingMaterialParameters",
			.resource = materialParameters_.GetResource(), .gpuAddress = materialParameters_.GetGPUAddress()});
	}

	// インスタンスの転送先を公開
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

	// 形状の転送先を公開
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

	// Material値の転送先を公開
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
