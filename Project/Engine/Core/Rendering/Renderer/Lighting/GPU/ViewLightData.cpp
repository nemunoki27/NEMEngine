#include "ViewLightData.h"

//============================================================================
//	include
//============================================================================
#include "LightGPUConversion.h"
#include <Engine/Core/Foundation/Time/FrameProfiler.h>

#include <algorithm>
#include <cmath>
#include <execution>
#include <thread>

namespace {

	uint64_t HashBytes(uint64_t hash, const void* data, size_t size) {

		const auto* bytes = static_cast<const uint8_t*>(data);
		for (size_t i = 0; i < size; ++i) {
			hash ^= bytes[i];
			hash *= 1099511628211ull;
		}
		return hash;
	}

	uint64_t BuildViewHash(const Engine::PerViewLightSet& lightSet) {

		uint64_t hash = 1469598103934665603ull;
		if (!lightSet.view || !lightSet.camera) {
			return hash;
		}
		hash = HashBytes(hash, &lightSet.view->valid,
			sizeof(lightSet.view->valid));
		hash = HashBytes(hash, &lightSet.view->width,
			sizeof(lightSet.view->width));
		hash = HashBytes(hash, &lightSet.view->height,
			sizeof(lightSet.view->height));
		hash = HashBytes(hash, &lightSet.camera->matrices.viewMatrix,
			sizeof(lightSet.camera->matrices.viewMatrix));
		hash = HashBytes(hash, &lightSet.camera->matrices.projectionMatrix,
			sizeof(lightSet.camera->matrices.projectionMatrix));
		hash = HashBytes(hash, &lightSet.camera->nearClip,
			sizeof(lightSet.camera->nearClip));
		hash = HashBytes(hash, &lightSet.camera->farClip,
			sizeof(lightSet.camera->farClip));
		hash = HashBytes(hash, &lightSet.camera->cullingMask,
			sizeof(lightSet.camera->cullingMask));
		hash = HashBytes(hash, &lightSet.sceneInstanceID,
			sizeof(lightSet.sceneInstanceID));
		return hash;
	}

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

void Engine::ViewLightData::Update(const PerViewLightSet& lightSet) {

	const uint64_t viewHash = BuildViewHash(lightSet);
	if (cacheValid_ &&
		cachedLightRevision_ == lightSet.sourceRevision &&
		cachedViewHash_ == viewHash) {

		return;
	}

	// ライト数を設定
	LightCountsGPU counts{};
	counts.directionalCount = lightSet.GetDirectionalCount();
	counts.pointCount = lightSet.GetPointCount();
	counts.spotCount = lightSet.GetSpotCount();
	counts.rectCount = lightSet.GetRectCount();
	counts.localCount = lightSet.GetLocalLightCount();
	lightCountsScratch_ = counts;

	// 平行光源
	FillLightScratch(lightSet.directionalLights, directionalScratch_,
		[](const DirectionalLightItem& item) {
			return LightGPUConversion::ToGPU(item);
		});
	// 点光源
	FillLightScratch(lightSet.pointLights, pointScratch_,
		[](const PointLightItem& item) {
			return LightGPUConversion::ToGPU(item);
		});
	// 矩形面光源
	FillLightScratch(lightSet.rectLights, rectScratch_,
		[](const RectLightItem& item) {
			return LightGPUConversion::ToGPU(item);
		});
	// スポットライト
	FillLightScratch(lightSet.spotLights, spotScratch_,
		[](const SpotLightItem& item) {
			return LightGPUConversion::ToGPU(item);
		});

	BuildClusters(lightSet);
	cachedLightRevision_ = lightSet.sourceRevision;
	cachedViewHash_ = viewHash;
	cacheValid_ = true;
	++dataSerial_;
	if (dataSerial_ == 0) {
		dataSerial_ = 1;
	}
}

void Engine::ViewLightData::Reset() {

	directionalScratch_.clear();
	pointScratch_.clear();
	rectScratch_.clear();
	spotScratch_.clear();
	directionalScratch_.shrink_to_fit();
	pointScratch_.shrink_to_fit();
	rectScratch_.shrink_to_fit();
	spotScratch_.shrink_to_fit();
	clusterHeaderScratch_.clear();
	clusterLightIndexScratch_.clear();
	clusterCountScratch_.clear();
	clusterCursorScratch_.clear();
	clusterBoundsScratch_.clear();
	clusterWorkerCursorScratch_.clear();
	clusterWorkerIndicesScratch_.clear();
	lightCountsScratch_ = {};
	clusterConstantsScratch_ = {};
	cachedLightRevision_ = 0;
	cachedViewHash_ = 0;
	dataSerial_ = 0;
	cacheValid_ = false;
}

void Engine::ViewLightData::BuildClusters(
	const PerViewLightSet& lightSet) {

	constexpr uint32_t kTileSize = 32;
	constexpr uint32_t kZSliceCount = 16;

	LightClusterConstantsGPU constants{};
	constants.tileSize = kTileSize;
	constants.zSliceCount = kZSliceCount;
	clusterHeaderScratch_.clear();
	clusterLightIndexScratch_.clear();

	if (!lightSet.view || !lightSet.camera ||
		!lightSet.view->valid || !lightSet.camera->valid ||
		lightSet.view->width == 0 || lightSet.view->height == 0) {
		clusterConstantsScratch_ = constants;
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
	clusterBoundsScratch_.clear();
	clusterBoundsScratch_.reserve(lightSet.GetLocalLightCount());

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

		ClusterBoundsScratch bounds{};
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
		clusterBoundsScratch_.emplace_back(bounds);
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
	for (uint32_t index = 0;
		index < static_cast<uint32_t>(lightSet.rectLights.size()); ++index) {
		const RectLightItem& light = *lightSet.rectLights[index];
		appendBounds(light.pos, light.attenuationRadius,
			static_cast<uint32_t>(
				lightSet.pointLights.size() +
				lightSet.spotLights.size()) + index);
	}

	uint32_t overflowCount = 0;
	auto visitClusters = [&](const ClusterBoundsScratch& bounds, auto&& visit) {
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
	uint64_t clusterVisitCount = 0;
	for (const ClusterBoundsScratch& bounds : clusterBoundsScratch_) {
		const uint64_t xCount =
			bounds.maxX - bounds.minX + 1;
		const uint64_t yCount =
			bounds.maxY - bounds.minY + 1;
		const uint64_t zCount =
			bounds.maxZ - bounds.minZ + 1;
		clusterVisitCount += xCount * yCount * zCount;
	}

	const uint32_t hardwareThreads =
		(std::max)(std::thread::hardware_concurrency(), 1u);
	const uint32_t workerCount = (std::min)({
		hardwareThreads, 16u,
		static_cast<uint32_t>(clusterBoundsScratch_.size())
		});
	const bool useParallel =
		1 < workerCount && 8192 < clusterVisitCount;

	clusterHeaderScratch_.resize(constants.clusterCount);
	uint32_t totalIndexCount = 0;
	uint32_t maxLightsPerCluster = 0;
	if (useParallel) {

		const size_t workerStride = constants.clusterCount;
		clusterWorkerCursorScratch_.assign(
			workerStride * workerCount, 0);
		clusterWorkerIndicesScratch_.resize(workerCount);
		for (uint32_t worker = 0;
			worker < workerCount; ++worker) {
			clusterWorkerIndicesScratch_[worker] = worker;
		}

		// ワーカーごとの領域へ数えることでatomic競合を避ける
		std::for_each(std::execution::par,
			clusterWorkerIndicesScratch_.begin(),
			clusterWorkerIndicesScratch_.end(),
			[&](uint32_t worker) {
				uint32_t* counts =
					clusterWorkerCursorScratch_.data() +
					workerStride * worker;
				for (size_t boundsIndex = worker;
					boundsIndex < clusterBoundsScratch_.size();
					boundsIndex += workerCount) {
					visitClusters(
						clusterBoundsScratch_[boundsIndex],
						[&](uint32_t clusterIndex) {
							++counts[clusterIndex];
						});
				}
			});

		for (uint32_t clusterIndex = 0;
			clusterIndex < constants.clusterCount;
			++clusterIndex) {
			LightClusterHeaderGPU& header =
				clusterHeaderScratch_[clusterIndex];
			header.offset = totalIndexCount;
			header.count = 0;
			for (uint32_t worker = 0;
				worker < workerCount; ++worker) {
				const size_t cursorIndex =
					workerStride * worker + clusterIndex;
				const uint32_t count =
					clusterWorkerCursorScratch_[cursorIndex];
				clusterWorkerCursorScratch_[cursorIndex] =
					header.offset + header.count;
				header.count += count;
			}
			totalIndexCount += header.count;
			maxLightsPerCluster = (std::max)(
				maxLightsPerCluster, header.count);
		}

		clusterLightIndexScratch_.resize(totalIndexCount);
		std::for_each(std::execution::par,
			clusterWorkerIndicesScratch_.begin(),
			clusterWorkerIndicesScratch_.end(),
			[&](uint32_t worker) {
				uint32_t* cursors =
					clusterWorkerCursorScratch_.data() +
					workerStride * worker;
				for (size_t boundsIndex = worker;
					boundsIndex < clusterBoundsScratch_.size();
					boundsIndex += workerCount) {
					const ClusterBoundsScratch& bounds =
						clusterBoundsScratch_[boundsIndex];
					visitClusters(bounds,
						[&](uint32_t clusterIndex) {
							clusterLightIndexScratch_[
								cursors[clusterIndex]++] =
								bounds.lightIndex;
						});
				}
			});
	} else {

		for (const ClusterBoundsScratch& bounds :
			clusterBoundsScratch_) {
			visitClusters(bounds,
				[&](uint32_t clusterIndex) {
					++clusterCountScratch_[clusterIndex];
				});
		}
		for (uint32_t clusterIndex = 0;
			clusterIndex < constants.clusterCount;
			++clusterIndex) {
			LightClusterHeaderGPU& header =
				clusterHeaderScratch_[clusterIndex];
			header.offset = totalIndexCount;
			header.count =
				clusterCountScratch_[clusterIndex];
			totalIndexCount += header.count;
			maxLightsPerCluster = (std::max)(
				maxLightsPerCluster, header.count);
		}
		clusterLightIndexScratch_.resize(totalIndexCount);
		clusterCursorScratch_.assign(
			constants.clusterCount, 0);
		for (const ClusterBoundsScratch& bounds :
			clusterBoundsScratch_) {
			visitClusters(bounds,
				[&](uint32_t clusterIndex) {
					uint32_t& cursor =
						clusterCursorScratch_[clusterIndex];
					const LightClusterHeaderGPU& header =
						clusterHeaderScratch_[clusterIndex];
					if (cursor < header.count) {
						clusterLightIndexScratch_[
							header.offset + cursor] =
							bounds.lightIndex;
						++cursor;
					}
				});
			}
	}

	constants.maxLightsPerCluster = maxLightsPerCluster;
	clusterConstantsScratch_ = constants;
	FrameProfiler::GetInstance().SetClusterStatistics(
		constants.clusterCount, lightSet.GetLocalLightCount(),
		totalIndexCount, overflowCount);
}
