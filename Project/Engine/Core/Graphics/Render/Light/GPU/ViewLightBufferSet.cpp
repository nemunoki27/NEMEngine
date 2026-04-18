#include "ViewLightBufferSet.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/GraphicsCore.h>

//============================================================================
//	ViewLightBufferSet classMethods
//============================================================================

namespace {

	template <typename SrcPtrT, typename DstT, typename ConvertFn>
	void FillLightScratch(const std::vector<const SrcPtrT*>& source,
		std::vector<DstT>& scratch, ConvertFn&& convert) {

		// 毎フレーム作り直さず再利用
		scratch.clear();

		// 必要数が現在容量を超える時だけ拡張
		if (scratch.capacity() < source.size()) {
			scratch.reserve(source.size());
		}
		for (const SrcPtrT* item : source) {

			scratch.emplace_back(convert(*item));
		}
	}
}

void Engine::ViewLightBufferSet::Init(GraphicsCore& graphicsCore) {

	// すでに初期化されている場合は何もしない
	if (initialized_) {
		return;
	}

	ID3D12Device* device = graphicsCore.GetDXObject().GetDevice();
	SRVDescriptor* srvDescriptor = &graphicsCore.GetSRVDescriptor();

	// バッファを初期化
	lightCounts_.Init(device);
	directionalLights_.Init(device, srvDescriptor);
	pointLights_.Init(device, srvDescriptor);
	spotLights_.Init(device, srvDescriptor);

	// 要素数を最低限確保
	directionalLights_.EnsureCapacity(1);
	pointLights_.EnsureCapacity(1);
	spotLights_.EnsureCapacity(1);
	directionalScratch_.reserve(1);
	pointScratch_.reserve(1);
	spotScratch_.reserve(1);

	// 初期化完了
	initialized_ = true;
}

void Engine::ViewLightBufferSet::Release() {

	directionalLights_.Release();
	pointLights_.Release();
	spotLights_.Release();
	directionalScratch_.clear();
	pointScratch_.clear();
	spotScratch_.clear();
	directionalScratch_.shrink_to_fit();
	pointScratch_.shrink_to_fit();
	spotScratch_.shrink_to_fit();

	initialized_ = false;
}

void Engine::ViewLightBufferSet::Upload(const PerViewLightSet& lightSet) {

	// ライト数を設定
	LightCountsGPU counts{};
	counts.directionalCount = lightSet.GetDirectionalCount();
	counts.pointCount = lightSet.GetPointCount();
	counts.spotCount = lightSet.GetSpotCount();
	counts.localCount = lightSet.GetLocalLightCount();

	// GPUへ転送
	lightCounts_.Upload(counts);

	// 平行光源
	FillLightScratch(lightSet.directionalLights, directionalScratch_,
		[](const DirectionalLightItem& item) {
			return ViewLightBufferSet::ToGPU(item);
		});
	// 点光源
	FillLightScratch(lightSet.pointLights, pointScratch_,
		[](const PointLightItem& item) {
			return ViewLightBufferSet::ToGPU(item);
		});
	// スポットライト
	FillLightScratch(lightSet.spotLights, spotScratch_,
		[](const SpotLightItem& item) {
			return ViewLightBufferSet::ToGPU(item);
		});

	// GPUへ転送
	directionalLights_.Upload(directionalScratch_);
	pointLights_.Upload(pointScratch_);
	spotLights_.Upload(spotScratch_);
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

	// スポットライト: SRV
	registry.Register({ .alias = "SpotLights",.resource = nullptr,.gpuAddress = spotLights_.GetGPUAddress(),
		.srvGPUHandle = spotLights_.GetGPUHandle(),.uavGPUHandle = {},.elementCount = 0,.stride = sizeof(SpotLightGPU) });
	registry.Register({ .alias = "gSpotLights",.resource = nullptr,.gpuAddress = spotLights_.GetGPUAddress(),
		.srvGPUHandle = spotLights_.GetGPUHandle(),.uavGPUHandle = {},.elementCount = 0,.stride = sizeof(SpotLightGPU) });
}

Engine::DirectionalLightGPU Engine::ViewLightBufferSet::ToGPU(const DirectionalLightItem& item) {

	DirectionalLightGPU light{};

	light.color = item.color;
	light.direction = item.direction;
	light.intensity = item.intensity;

	return light;
}

Engine::PointLightGPU Engine::ViewLightBufferSet::ToGPU(const PointLightItem& item) {

	PointLightGPU light{};
	
	light.color = item.color;
	light.pos = item.pos;
	light.intensity = item.intensity;
	light.radius = item.radius;
	light.decay = item.decay;

	return light;
}

Engine::SpotLightGPU Engine::ViewLightBufferSet::ToGPU(const SpotLightItem& item) {

	SpotLightGPU light{};

	light.color = item.color;
	light.direction = item.direction;
	light.pos = item.pos;
	light.intensity = item.intensity;
	light.distance = item.distance;
	light.decay = item.decay;
	light.cosAngle = item.cosAngle;
	light.cosFalloffStart = item.cosFalloffStart;

	return light;
}