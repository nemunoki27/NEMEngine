#include "ViewLightBufferSet.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>

// c++
#include <algorithm>
#include <cmath>

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
	clusterConstants_.Init(device);
	clusterHeaders_.Init(device, srvDescriptor);
	clusterLightIndices_.Init(device, srvDescriptor);

	// 要素数を最低限確保
	directionalLights_.EnsureCapacity(1);
	pointLights_.EnsureCapacity(1);
	spotLights_.EnsureCapacity(1);
	clusterHeaders_.EnsureCapacity(1);
	clusterLightIndices_.EnsureCapacity(1);
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
	clusterHeaders_.Release();
	clusterLightIndices_.Release();
	directionalScratch_.clear();
	pointScratch_.clear();
	spotScratch_.clear();
	directionalScratch_.shrink_to_fit();
	pointScratch_.shrink_to_fit();
	spotScratch_.shrink_to_fit();
	clusterHeaderScratch_.clear();
	clusterLightIndexScratch_.clear();
	clusterCountScratch_.clear();
	clusterCursorScratch_.clear();

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
	BuildClusters(lightSet);
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

	// クラスター分割とライト索引
	registry.Register({ .alias = "LightClusterConstants",.resource = nullptr,.gpuAddress = clusterConstants_.GetGPUAddress(),
		.srvGPUHandle = {},.uavGPUHandle = {},.elementCount = 1,.stride = sizeof(LightClusterConstantsGPU) });
	registry.Register({ .alias = "gLightClusterHeaders",.resource = nullptr,.gpuAddress = clusterHeaders_.GetGPUAddress(),
		.srvGPUHandle = clusterHeaders_.GetGPUHandle(),.uavGPUHandle = {},.elementCount = 0,.stride = sizeof(LightClusterHeaderGPU) });
	registry.Register({ .alias = "gLightClusterIndices",.resource = nullptr,.gpuAddress = clusterLightIndices_.GetGPUAddress(),
		.srvGPUHandle = clusterLightIndices_.GetGPUHandle(),.uavGPUHandle = {},.elementCount = 0,.stride = sizeof(uint32_t) });
}

void Engine::ViewLightBufferSet::BuildClusters(
	const PerViewLightSet& lightSet) {

	constexpr uint32_t kTileSize = 32;
	constexpr uint32_t kZSliceCount = 16;
	constexpr uint32_t kMaxLightsPerCluster = 256;

	LightClusterConstantsGPU constants{};
	constants.tileSize = kTileSize;
	constants.zSliceCount = kZSliceCount;
	constants.maxLightsPerCluster = kMaxLightsPerCluster;
	clusterHeaderScratch_.clear();
	clusterLightIndexScratch_.clear();

	if (!lightSet.view || !lightSet.camera ||
		!lightSet.view->valid || !lightSet.camera->valid ||
		lightSet.view->width == 0 || lightSet.view->height == 0) {
		clusterConstants_.Upload(constants);
		FrameProfiler::GetInstance().SetClusterStatistics(
			0, lightSet.GetLocalLightCount(), 0, 0);
		return;
	}

	const ResolvedRenderView& view = *lightSet.view;
	const ResolvedCameraView& camera = *lightSet.camera;
	constants.tileCountX = (view.width + kTileSize - 1) / kTileSize;
	constants.tileCountY = (view.height + kTileSize - 1) / kTileSize;
	constants.nearClip = (std::max)(camera.nearClip, 0.001f);
	constants.farClip = (std::max)(
		camera.farClip, constants.nearClip + 0.001f);
	const float logDepthRange = std::log2(
		constants.farClip / constants.nearClip);
	constants.sliceScale = static_cast<float>(kZSliceCount) /
		(std::max)(logDepthRange, 0.001f);
	constants.sliceBias = -std::log2(constants.nearClip) *
		constants.sliceScale;
	constants.clusterCount =
		constants.tileCountX * constants.tileCountY * kZSliceCount;

	clusterCountScratch_.assign(constants.clusterCount, 0);

	struct ClusterBounds {

		uint32_t minX = 0;
		uint32_t maxX = 0;
		uint32_t minY = 0;
		uint32_t maxY = 0;
		uint32_t minZ = 0;
		uint32_t maxZ = 0;
		uint32_t lightIndex = 0;
	};
	std::vector<ClusterBounds> boundsScratch{};
	boundsScratch.reserve(lightSet.GetLocalLightCount());

	auto depthToSlice = [&](float depth) {
		const float slice = std::log2(
			(std::max)(depth, constants.nearClip)) *
			constants.sliceScale + constants.sliceBias;
		return (std::min)(static_cast<uint32_t>(
			(std::max)(slice, 0.0f)), kZSliceCount - 1);
		};
	auto appendBounds = [&](const Vector3& worldCenter,
		float radius, uint32_t lightIndex) {

		radius = (std::max)(radius, 0.001f);
		const Vector3 viewCenter = Vector3::Transform(
			worldCenter, camera.matrices.viewMatrix);
		const float minDepth = viewCenter.z - radius;
		const float maxDepth = viewCenter.z + radius;
		if (maxDepth < constants.nearClip ||
			constants.farClip < minDepth) {
			return;
		}

		ClusterBounds bounds{};
		bounds.lightIndex = lightIndex;
		bounds.minZ = depthToSlice(
			(std::max)(minDepth, constants.nearClip));
		bounds.maxZ = depthToSlice(
			(std::min)(maxDepth, constants.farClip));

		if (viewCenter.z <= radius + constants.nearClip) {
			bounds.maxX = constants.tileCountX - 1;
			bounds.maxY = constants.tileCountY - 1;
		} else {
			const Vector3 ndc = Vector3::Transform(
				worldCenter, camera.matrices.viewProjectionMatrix);
			const float projectionX =
				std::abs(camera.matrices.projectionMatrix.m[0][0]);
			const float projectionY =
				std::abs(camera.matrices.projectionMatrix.m[1][1]);
			const float radiusNdcX = radius * projectionX / viewCenter.z;
			const float radiusNdcY = radius * projectionY / viewCenter.z;
			const float minPixelX = (ndc.x - radiusNdcX) * 0.5f *
				static_cast<float>(view.width) +
				0.5f * static_cast<float>(view.width);
			const float maxPixelX = (ndc.x + radiusNdcX) * 0.5f *
				static_cast<float>(view.width) +
				0.5f * static_cast<float>(view.width);
			const float minPixelY = (0.5f - (ndc.y + radiusNdcY) * 0.5f) *
				static_cast<float>(view.height);
			const float maxPixelY = (0.5f - (ndc.y - radiusNdcY) * 0.5f) *
				static_cast<float>(view.height);
			const int32_t minTileX = static_cast<int32_t>(
				std::floor(minPixelX / static_cast<float>(kTileSize)));
			const int32_t maxTileX = static_cast<int32_t>(
				std::floor(maxPixelX / static_cast<float>(kTileSize)));
			const int32_t minTileY = static_cast<int32_t>(
				std::floor(minPixelY / static_cast<float>(kTileSize)));
			const int32_t maxTileY = static_cast<int32_t>(
				std::floor(maxPixelY / static_cast<float>(kTileSize)));
			if (maxTileX < 0 || maxTileY < 0 ||
				static_cast<int32_t>(constants.tileCountX) <= minTileX ||
				static_cast<int32_t>(constants.tileCountY) <= minTileY) {
				return;
			}
			bounds.minX = static_cast<uint32_t>(std::clamp(
				minTileX, 0, static_cast<int32_t>(constants.tileCountX - 1)));
			bounds.maxX = static_cast<uint32_t>(std::clamp(
				maxTileX, 0, static_cast<int32_t>(constants.tileCountX - 1)));
			bounds.minY = static_cast<uint32_t>(std::clamp(
				minTileY, 0, static_cast<int32_t>(constants.tileCountY - 1)));
			bounds.maxY = static_cast<uint32_t>(std::clamp(
				maxTileY, 0, static_cast<int32_t>(constants.tileCountY - 1)));
		}
		boundsScratch.emplace_back(bounds);
		};

	for (uint32_t index = 0;
		index < static_cast<uint32_t>(lightSet.pointLights.size()); ++index) {
		const PointLightItem& light = *lightSet.pointLights[index];
		appendBounds(light.pos, light.radius, index);
	}
	for (uint32_t index = 0;
		index < static_cast<uint32_t>(lightSet.spotLights.size()); ++index) {
		const SpotLightItem& light = *lightSet.spotLights[index];
		const Vector3 direction = Vector3::NormalizeOr(
			light.direction, Vector3(0.0f, 0.0f, 1.0f));
		const Vector3 center = light.pos + direction * (light.distance * 0.5f);
		appendBounds(center, light.distance,
			static_cast<uint32_t>(lightSet.pointLights.size()) + index);
	}

	uint32_t overflowCount = 0;
	auto visitClusters = [&](const ClusterBounds& bounds, auto&& visit) {
		for (uint32_t z = bounds.minZ; z <= bounds.maxZ; ++z) {
			for (uint32_t y = bounds.minY; y <= bounds.maxY; ++y) {
				for (uint32_t x = bounds.minX; x <= bounds.maxX; ++x) {
					const uint32_t clusterIndex =
						(z * constants.tileCountY + y) *
						constants.tileCountX + x;
					visit(clusterIndex);
				}
			}
		}
		};
	for (const ClusterBounds& bounds : boundsScratch) {
		visitClusters(bounds, [&](uint32_t clusterIndex) {
			uint32_t& count = clusterCountScratch_[clusterIndex];
			if (count < kMaxLightsPerCluster) {
				++count;
			} else {
				++overflowCount;
			}
			});
	}

	clusterHeaderScratch_.resize(constants.clusterCount);
	uint32_t totalIndexCount = 0;
	for (uint32_t clusterIndex = 0;
		clusterIndex < constants.clusterCount; ++clusterIndex) {
		LightClusterHeaderGPU& header =
			clusterHeaderScratch_[clusterIndex];
		header.offset = totalIndexCount;
		header.count = clusterCountScratch_[clusterIndex];
		totalIndexCount += header.count;
	}
	clusterLightIndexScratch_.resize(totalIndexCount);
	clusterCursorScratch_.assign(constants.clusterCount, 0);
	for (const ClusterBounds& bounds : boundsScratch) {
		visitClusters(bounds, [&](uint32_t clusterIndex) {
			uint32_t& cursor = clusterCursorScratch_[clusterIndex];
			const LightClusterHeaderGPU& header =
				clusterHeaderScratch_[clusterIndex];
			if (cursor < header.count) {
				clusterLightIndexScratch_[header.offset + cursor] =
					bounds.lightIndex;
				++cursor;
			}
			});
	}

	clusterConstants_.Upload(constants);
	clusterHeaders_.Upload(clusterHeaderScratch_);
	clusterLightIndices_.Upload(clusterLightIndexScratch_);
	FrameProfiler::GetInstance().SetClusterStatistics(
		constants.clusterCount, lightSet.GetLocalLightCount(),
		totalIndexCount, overflowCount);
}

Engine::DirectionalLightGPU Engine::ViewLightBufferSet::ToGPU(const DirectionalLightItem& item) {

	DirectionalLightGPU light{};

	light.color = item.color;
	light.direction = item.direction;
	light.intensity = item.intensity;
	light.shadowStrength = item.shadowStrength;

	return light;
}

Engine::PointLightGPU Engine::ViewLightBufferSet::ToGPU(const PointLightItem& item) {

	PointLightGPU light{};
	
	light.color = item.color;
	light.pos = item.pos;
	light.intensity = item.intensity;
	light.radius = item.radius;
	light.decay = item.decay;
	light.shadowStrength = item.shadowStrength;

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
