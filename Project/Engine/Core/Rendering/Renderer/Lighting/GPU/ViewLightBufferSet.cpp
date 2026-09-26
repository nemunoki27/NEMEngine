#include "ViewLightBufferSet.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>

// c++
#include <algorithm>
#include <cmath>
#include <execution>
#include <thread>

//============================================================================
//	ViewLightBufferSet classMethods
//============================================================================

void Engine::ViewLightBufferSet::Init(GraphicsCore& graphicsCore) {

	// すでに初期化されている場合は何もしない
	if (initialized_) {
		return;
	}

	ID3D12Device* device = graphicsCore.GetDXObject().GetDevice();
	SRVDescriptor* srvDescriptor = &graphicsCore.GetSRVDescriptor();

	// バッファを初期化
	lightCounts_.Init(graphicsCore.GetDXObject().GetResourceRetirement(), device);
	directionalLights_.Init(device, srvDescriptor);
	pointLights_.Init(device, srvDescriptor);
	rectLights_.Init(device, srvDescriptor);
	spotLights_.Init(device, srvDescriptor);
	clusterConstants_.Init(graphicsCore.GetDXObject().GetResourceRetirement(), device);
	clusterHeaders_.Init(device, srvDescriptor);
	clusterLightIndices_.Init(device, srvDescriptor);

	// 要素数を最低限確保
	directionalLights_.EnsureCapacity(1);
	pointLights_.EnsureCapacity(1);
	rectLights_.EnsureCapacity(1);
	spotLights_.EnsureCapacity(1);
	clusterHeaders_.EnsureCapacity(1);
	clusterLightIndices_.EnsureCapacity(1);
	data_.directionalScratch_.reserve(1);
	data_.pointScratch_.reserve(1);
	data_.rectScratch_.reserve(1);
	data_.spotScratch_.reserve(1);

	// 初期化完了
	initialized_ = true;
}

void Engine::ViewLightBufferSet::Release() {

	// 定数と構造化Bufferをまとめて返す
	lightCounts_.Release();
	clusterConstants_.Release();
	directionalLights_.Release();
	pointLights_.Release();
	rectLights_.Release();
	spotLights_.Release();
	clusterHeaders_.Release();
	clusterLightIndices_.Release();
	data_.Reset();
	uploadedSerials_.fill(0);

	initialized_ = false;
}

void Engine::ViewLightBufferSet::Upload(const PerViewLightSet& lightSet) {

	data_.Update(lightSet);
	UploadCachedBuffers();
}

void Engine::ViewLightBufferSet::UploadCachedBuffers() {

	const uint32_t frameIndex =
		GraphicsFrameState::GetCurrentIndex();
	if (uploadedSerials_[frameIndex] == data_.dataSerial_) {
		return;
	}

	lightCounts_.Upload(data_.lightCountsScratch_);
	directionalLights_.Upload(data_.directionalScratch_);
	pointLights_.Upload(data_.pointScratch_);
	rectLights_.Upload(data_.rectScratch_);
	spotLights_.Upload(data_.spotScratch_);
	clusterConstants_.Upload(data_.clusterConstantsScratch_);
	clusterHeaders_.Upload(data_.clusterHeaderScratch_);
	clusterLightIndices_.Upload(data_.clusterLightIndexScratch_);
	uploadedSerials_[frameIndex] = data_.dataSerial_;
}

void Engine::ViewLightBufferSet::RegisterTo(RenderBufferRegistry& registry) const {

	// ライト数: CBV
	registry.Register({ .alias = "LightCounts",.resource = nullptr,.gpuAddress = lightCounts_.GetGPUAddress(),
		.srvGPUHandle = {},.uavGPUHandle = {},.elementCount = 1,.stride = sizeof(LightCountsGPU) });
	registry.Register({ .alias = "gLightCounts",.resource = nullptr,.gpuAddress = lightCounts_.GetGPUAddress(),
		.srvGPUHandle = {},.uavGPUHandle = {},.elementCount = 1,.stride = sizeof(LightCountsGPU) });

	// 平行光源: SRV
	registry.Register({ .alias = "DirectionalLights",.resource = nullptr,.gpuAddress = directionalLights_.GetGPUAddress(),
		.srvGPUHandle = directionalLights_.GetGPUHandle(),.uavGPUHandle = {},.elementCount = 0,.stride = sizeof(DirectionalLightGPU) });
	registry.Register({ .alias = "gDirectionalLights",.resource = nullptr,.gpuAddress = directionalLights_.GetGPUAddress(),
		.srvGPUHandle = directionalLights_.GetGPUHandle(),.uavGPUHandle = {},.elementCount = 0,.stride = sizeof(DirectionalLightGPU) });

	// 点光源: SRV
	registry.Register({ .alias = "PointLights",.resource = nullptr,.gpuAddress = pointLights_.GetGPUAddress(),
		.srvGPUHandle = pointLights_.GetGPUHandle(),.uavGPUHandle = {},.elementCount = 0,.stride = sizeof(PointLightGPU) });
	registry.Register({ .alias = "gPointLights",.resource = nullptr,.gpuAddress = pointLights_.GetGPUAddress(),
		.srvGPUHandle = pointLights_.GetGPUHandle(),.uavGPUHandle = {},.elementCount = 0,.stride = sizeof(PointLightGPU) });

	// 矩形面光源: SRV
	registry.Register({ .alias = "RectLights",.resource = nullptr,.gpuAddress = rectLights_.GetGPUAddress(),
		.srvGPUHandle = rectLights_.GetGPUHandle(),.uavGPUHandle = {},.elementCount = 0,.stride = sizeof(RectLightGPU) });
	registry.Register({ .alias = "gRectLights",.resource = nullptr,.gpuAddress = rectLights_.GetGPUAddress(),
		.srvGPUHandle = rectLights_.GetGPUHandle(),.uavGPUHandle = {},.elementCount = 0,.stride = sizeof(RectLightGPU) });

	// スポットライト: SRV
	registry.Register({ .alias = "SpotLights",.resource = nullptr,.gpuAddress = spotLights_.GetGPUAddress(),
		.srvGPUHandle = spotLights_.GetGPUHandle(),.uavGPUHandle = {},.elementCount = 0,.stride = sizeof(SpotLightGPU) });
	registry.Register({ .alias = "gSpotLights",.resource = nullptr,.gpuAddress = spotLights_.GetGPUAddress(),
		.srvGPUHandle = spotLights_.GetGPUHandle(),.uavGPUHandle = {},.elementCount = 0,.stride = sizeof(SpotLightGPU) });

	// クラスター分割とライト索引
	registry.Register({ .alias = "LightClusterConstants",.resource = nullptr,.gpuAddress = clusterConstants_.GetGPUAddress(),
		.srvGPUHandle = {},.uavGPUHandle = {},.elementCount = 1,.stride = sizeof(LightClusterConstantsGPU) });
	registry.Register({ .alias = "gLightClusterHeaders",.resource = nullptr,.gpuAddress = clusterHeaders_.GetGPUAddress(),
		.srvGPUHandle = clusterHeaders_.GetGPUHandle(),.uavGPUHandle = {},.elementCount = 0,.stride = sizeof(LightClusterHeaderGPU) });
	registry.Register({ .alias = "gLightClusterIndices",.resource = nullptr,.gpuAddress = clusterLightIndices_.GetGPUAddress(),
		.srvGPUHandle = clusterLightIndices_.GetGPUHandle(),.uavGPUHandle = {},.elementCount = 0,.stride = sizeof(uint32_t) });
}
