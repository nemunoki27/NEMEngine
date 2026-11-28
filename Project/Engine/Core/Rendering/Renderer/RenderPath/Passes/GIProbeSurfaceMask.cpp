#include "GIProbeSurfaceMask.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Meshes/Utility/MeshBoundsUtility.h>
#include <Engine/Core/Rendering/Meshes/Utility/MeshSurfaceGeometry.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>

// c++
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <future>
#include <limits>
#include <map>
#include <tuple>
#include <stop_token>

namespace {
	using namespace Engine;
	using Brick = GIProbeSurfaceMask::Brick;
	using MaskMap = std::map<Brick, uint64_t>;

	struct Surface {
		std::shared_ptr<const MeshSurfaceGeometry> mesh;
		uint32_t subMesh = 0;
		Matrix4x4 inverse{};
		float radiusScale = 0.0f;
		GIProbeSurfaceMask::Bounds bounds;

		bool operator==(const Surface&) const = default;
	};

	float MaximumCoordinate(const Vector3& point) {
		return std::max({std::abs(point.x), std::abs(point.y), std::abs(point.z)});
	}

	uint64_t GetSurfaceMask(const Brick& brick, const Surface& surface, uint64_t mask) {

		const float spacing = std::ldexp(0.75f, brick[3]);
		const Vector3 first{(4.0f * brick[0] + 0.5f) * spacing, (4.0f * brick[1] + 0.5f) * spacing,
			(4.0f * brick[2] + 0.5f) * spacing};
		const auto& sphere = surface.bounds.sphere;
		const Vector3 center{sphere.x, sphere.y, sphere.z};
		// 近傍カーネルの半径に、法線バイアスとFP32ワールド座標・境界変換の丸め誤差を含める
		const float magnitude = std::max(MaximumCoordinate(first) + 4.0f * spacing, MaximumCoordinate(center) + sphere.w);
		const float reach = 2.0f * spacing + 0.005f + magnitude * (32.0f * std::numeric_limits<float>::epsilon());
		uint64_t candidates = surface.bounds.GetProbeMask(first, spacing, reach, mask);
		if (!surface.mesh) return mask | candidates;
		while (candidates) {
			const uint32_t cell = std::countr_zero(candidates);
			candidates &= candidates - 1;
			const Vector3 step{float(cell & 3u) * spacing, float((cell >> 2) & 3u) * spacing, float(cell >> 4) * spacing};
			const Vector3 local = Vector3::Transform(first + step, surface.inverse);
			const float radius = reach * surface.radiusScale + MaximumCoordinate(local) * 1e-5f;
			if (surface.mesh->IntersectsSphere(surface.subMesh, local, radius)) mask |= uint64_t{1} << cell;
		}
		return mask;
	}

	uint64_t GetBrickMask(const Brick& brick, const std::vector<Surface>& surfaces, uint64_t mask) {

		for (const auto& surface : surfaces) {
			if (mask == UINT64_MAX) break;
			mask = GetSurfaceMask(brick, surface, mask);
		}
		return mask;
	}

	// ブリックの同じワールド格子を、スロット番号・品質の変更をまたいで識別する
	Brick GetBrickKey(const Brick& brick, float baseSpacing, uint32_t shift) {

		const int32_t level = brick[3] == 3 ? static_cast<int32_t>((shift + 1u) / 2u) :
			brick[3] == 2 ? static_cast<int32_t>(shift) : 0;
		return {brick[0], brick[1], brick[2], level + std::ilogb(baseSpacing / 0.75f)};
	}
}

struct Engine::GIProbeSurfaceMask::Cache {
	struct Pose {
		const MeshSurfaceGeometry* mesh = nullptr;
		uint32_t subMesh = 0;
		Matrix4x4 transform{};
		Matrix4x4 inverse{};
		float inverseScale = 0.0f;
		uint32_t stableFrames = 0;
	};
	std::vector<Pose> poses;
	std::vector<Surface> surfaces;
	MaskMap masks;
	uint64_t generation = 0;
	uint64_t pendingGeneration = 0;
	std::future<MaskMap> worker;
	std::stop_source cancellation;
	std::vector<Surface> current, bounds;
	std::vector<Brick> missing;

	~Cache() { cancellation.request_stop(); }
};

uint64_t Engine::GIProbeSurfaceMask::Bounds::GetProbeMask(const Vector3& first, float spacing, float reach, uint64_t skip) const {

	const bool box = halfExtent.x >= 0.0f;
	const float radius = box ? reach : reach + sphere.w;
	const float radiusSquared = radius * radius;
	const auto touches = [&](const Vector3& delta) {
		if (!box) return Vector3::Dot(delta, delta) <= radiusSquared;
		const Vector3 gap{std::max(std::abs(delta.x) - halfExtent.x, 0.0f),
			std::max(std::abs(delta.y) - halfExtent.y, 0.0f), std::max(std::abs(delta.z) - halfExtent.z, 0.0f)};
		return Vector3::Dot(gap, gap) <= radiusSquared;
	};
	const Vector3 offset{sphere.x - first.x, sphere.y - first.y, sphere.z - first.z};
	const float span = 3.0f * spacing;
	const Vector3 nearPoint{std::clamp(offset.x, 0.0f, span), std::clamp(offset.y, 0.0f, span), std::clamp(offset.z, 0.0f, span)};
	if (!touches(offset - nearPoint)) return 0;
	const Vector3 farthest{std::max(std::abs(offset.x), std::abs(span - offset.x)),
		std::max(std::abs(offset.y), std::abs(span - offset.y)), std::max(std::abs(offset.z), std::abs(span - offset.z))};
	if (touches(farthest)) return ~skip;
	uint64_t remaining = ~skip;
	uint64_t mask = 0;
	while (remaining) {
		const uint32_t cell = std::countr_zero(remaining);
		remaining &= remaining - 1;
		const Vector3 step{float(cell & 3u) * spacing, float((cell >> 2) & 3u) * spacing, float(cell >> 4) * spacing};
		if (touches(step - offset)) mask |= uint64_t{1} << cell;
	}
	return mask;
}

bool Engine::GIProbeSurfaceMask::TryGetWorldBounds(const RaytracingGeometryBounds& geometry, const Matrix4x4& world, Bounds& bounds) {

	if (geometry.deformed && !geometry.probeBoundsGuaranteed) return false;
	Vector3 center = geometry.center;
	float radius = geometry.radius;
	if (geometry.probeBoundsGuaranteed) {
		center = {geometry.probeBounds.x, geometry.probeBounds.y, geometry.probeBounds.z};
		radius = geometry.probeBounds.w;
	}
	if (!(radius >= 0.0f)) return false;
	Vector3 halfExtent{-1, -1, -1};
	const auto& extent = geometry.probeHalfExtent;
	if (geometry.probeBoundsGuaranteed && extent.x >= 0 && extent.y >= 0 && extent.z >= 0) {
		halfExtent = MeshBoundsUtility::TransformHalfExtent(world, center, extent);
		if (!std::isfinite(halfExtent.x) || !std::isfinite(halfExtent.y) || !std::isfinite(halfExtent.z)) return false;
	}
	MeshBoundsUtility::TransformSphere(world, center, radius);
	if (!std::isfinite(radius) || !std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z)) return false;
	bounds = {{center.x, center.y, center.z, radius}, halfExtent};
	return true;
}

void Engine::GIProbeSurfaceMask::Release() {

	if (!cache_) return;
	cache_->cancellation.request_stop();
	if (!cache_->worker.valid() || cache_->worker.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
		cache_.reset();
		return;
	}
	// ワーカーが読み終えるまで描画を待たせず、View側の旧シーン参照を先に外す
	if (!cache_->surfaces.empty()) ++cache_->generation;
	cache_->surfaces.clear();
	cache_->current.clear();
	cache_->bounds.clear();
	cache_->poses.clear();
	cache_->masks.clear();
}

void Engine::GIProbeSurfaceMask::Update(const SceneExecutionContext& context, std::span<const Brick> bricks,
	float baseSpacing, uint32_t coarseShift, std::vector<uint64_t>& masks) {

	const auto& scene = context.raytracing;
	if (!scene.probeReceiversCovered || bricks.empty()) {
		Release();
		masks.clear();
		return;
	}
	if (!cache_) cache_ = std::make_shared<Cache>();
	auto& cache = *cache_;
	cache.current.clear();
	cache.bounds.clear();
	cache.poses.resize(scene.geometryBounds.size());
	for (size_t i = 0; i < scene.geometryBounds.size(); ++i) {
		const auto& geometry = scene.geometryBounds[i];
		if (!geometry.probeBoundsGuaranteed || geometry.instanceIndex >= scene.instanceWorldMatrices.size()) {
			Release();
			masks.clear();
			return;
		}
		const auto& world = scene.instanceWorldMatrices[geometry.instanceIndex];
		Surface surface{};
		if (!TryGetWorldBounds(geometry, world, surface.bounds)) {
			Release();
			masks.clear();
			return;
		}
		if (geometry.probeSurface && !geometry.deformed) {
			auto& pose = cache.poses[i];
			constexpr uint32_t kStationaryFrames = 8;
			const auto transform = geometry.probeLocalMatrix * world;
			const bool sameMesh = pose.mesh == geometry.probeSurface.get() && pose.subMesh == geometry.probeSubMesh;
			if (!sameMesh || pose.transform != transform) {
				pose.mesh = geometry.probeSurface.get();
				pose.subMesh = geometry.probeSubMesh;
				pose.transform = transform;
				pose.stableFrames = sameMesh ? 0u : kStationaryFrames;
				pose.inverseScale = 0.0f;
				if (Matrix4x4::TryInverse(transform, pose.inverse)) {
					Vector3 origin{};
					float inverseScale = 1.0f, scale = 1.0f;
					MeshBoundsUtility::TransformSphere(pose.inverse, origin, inverseScale);
					MeshBoundsUtility::TransformSphere(transform, origin, scale);
					if (inverseScale * scale < 10000.0f) pose.inverseScale = inverseScale * 1.0001f;
				}
			} else if (pose.stableFrames < kStationaryFrames) {
				++pose.stableFrames;
			}
			// 移動中のメッシュは毎フレームの境界球で覆い、静止メッシュの結果を再利用する
			if (pose.inverseScale > 0.0f && pose.stableFrames == kStationaryFrames) {
				surface.mesh = geometry.probeSurface;
				surface.subMesh = geometry.probeSubMesh;
				surface.inverse = pose.inverse;
				surface.radiusScale = pose.inverseScale;
				cache.current.push_back(surface);
				continue;
			}
		}
		cache.bounds.push_back(surface);
	}
	// 複数のスキンサブメッシュが共有する変形範囲は一度だけ調べる
	auto order = [](const Surface& l, const Surface& r) {
		const auto key = [](const Surface& s) {
			const auto& a = s.bounds.sphere;
			const auto& h = s.bounds.halfExtent;
			return std::tie(a.x, a.y, a.z, a.w, h.x, h.y, h.z);
		};
		return key(l) < key(r);
	};
	std::sort(cache.bounds.begin(), cache.bounds.end(), order);
	cache.bounds.erase(std::unique(cache.bounds.begin(), cache.bounds.end()), cache.bounds.end());

	if (cache.surfaces != cache.current) {
		cache.cancellation.request_stop();
		cache.surfaces.swap(cache.current);
		cache.masks.clear();
		++cache.generation;
	}
	if (cache.worker.valid() && cache.worker.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
		auto result = cache.worker.get();
		if (cache.pendingGeneration == cache.generation) cache.masks.merge(result);
	}
	cache.missing.clear();
	masks.resize(bricks.size());
	for (size_t slot = 0; slot < bricks.size(); ++slot) {
		if (!bricks[slot][3]) {
			masks[slot] = 0;
			continue;
		}
		const auto key = GetBrickKey(bricks[slot], baseSpacing, coarseShift);
		uint64_t mask = 0;
		if (!cache.surfaces.empty()) {
			const auto found = cache.masks.find(key);
			if (found == cache.masks.end()) {
				// 非同期計算中のブリックは全セルを描画する
				if (!cache.worker.valid()) cache.missing.push_back(key);
				masks[slot] = UINT64_MAX;
				continue;
			}
			mask = found->second;
		}
		masks[slot] = GetBrickMask(key, cache.bounds, mask);
	}
	if (!cache.missing.empty()) {
		if (cache.masks.size() > 2 * bricks.size()) {
			MaskMap current;
			for (const auto& brick : bricks) {
				if (!brick[3]) continue;
				auto entry = cache.masks.extract(GetBrickKey(brick, baseSpacing, coarseShift));
				if (entry) current.insert(std::move(entry));
			}
			cache.masks.swap(current);
		}
		cache.pendingGeneration = cache.generation;
		cache.cancellation = std::stop_source{};
		cache.worker = std::async(std::launch::async, [surfaces = cache.surfaces, missing = cache.missing,
			stop = cache.cancellation.get_token()] {
			MaskMap result;
			for (const auto& key : missing) {
				if (stop.stop_requested()) break;
				result.emplace(key, GetBrickMask(key, surfaces, 0));
			}
			return result;
		});
	}
}
