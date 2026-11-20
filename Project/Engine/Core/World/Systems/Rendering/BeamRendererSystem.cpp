#include "BeamRendererSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/BeamRendererComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Systems/Transform/TransformWorldUtility.h>

#include <cmath>

using namespace Engine;

namespace {

	void ClearGeometry(BeamRendererRuntime& runtime) {

		runtime.points.clear();
		runtime.hasHit = false;
		runtime.hit = {};
		runtime.hitSegment = -1;
	}

	// 同じシードと再生時刻で稲妻の折れ点を再現する
	float SignedNoise(uint32_t seed) {

		seed ^= seed >> 16;
		seed *= 0x7feb352du;
		seed ^= seed >> 15;
		seed *= 0x846ca68bu;
		seed ^= seed >> 16;
		return static_cast<float>(seed >> 8) * (2.0f / 16777216.0f) - 1.0f;
	}

	Vector3 SideVector(const Vector3& direction, bool is2D) {

		const Vector3 axis = is2D || std::abs(direction.z) < 0.8f ? Vector3{0, 0, 1} : Vector3{0, 1, 0};
		return Vector3::NormalizeOr(Vector3::Cross(direction, axis), {1, 0, 0});
	}

	void AddSpan(BeamRendererRecord& record, const Vector3& start, const Vector3& end, uint32_t seed) {

		auto& points = record.runtime.points;
		const auto& s = record.settings;
		const Vector3 delta = end - start;
		if (delta.Length() < 0.00001f) return;
		const Vector3 direction = Vector3::NormalizeOr(delta, {1, 0, 0});
		const auto side = SideVector(direction, s.is2D);
		const auto up = Vector3::Cross(direction, side);
		if (points.empty()) points.push_back({start, 0, 0, 1, true});
		for (int i = 1; i <= s.subdivisions; ++i) {
			const float t = static_cast<float>(i) / static_cast<float>(s.subdivisions);
			Vector3 position = i == s.subdivisions ? end : start + delta * t;
			if (s.mode == BeamMode::Lightning && i < s.subdivisions) {
				const auto key = seed + 2u * i;
				Vector3 offset = side * SignedNoise(key);
				if (!s.is2D) offset += up * SignedNoise(key + 1);
				position += offset * (s.amplitude * 4 * t * (1 - t));
			}
			const float distance = points.back().distance + (position - points.back().position).Length();
			points.push_back({position, 0, distance});
		}
	}

	void AddBranches(ECSWorld& world, BeamRendererRecord& record, uint32_t seed, std::span<const Entity> excluded) {

		const auto& s = record.settings;
		auto& points = record.runtime.points;
		const size_t count = points.size();
		const float total = points.back().distance;
		for (int i = 0; i < s.branchCount && 0 < s.branchLength && 0 < s.branchWidth; ++i) {
			const uint32_t key = seed + 0x9e3779b9u * (i + 1);
			const float t = (i + 0.5f + 0.4f * SignedNoise(key)) / s.branchCount;
			const size_t index = (std::min)(static_cast<size_t>(t * (count - 1)), count - 2);
			const BeamPoint root = points[index];
			const Vector3 forward = Vector3::NormalizeOr(points[index + 1].position - root.position, {1, 0, 0});
			const auto side = SideVector(forward, s.is2D);
			Vector3 offset = forward * 0.45f + side * (SignedNoise(key + 1) < 0 ? -1.0f : 1.0f);
			if (!s.is2D) offset += Vector3::Cross(forward, side) * SignedNoise(key + 2);
			const float length = (std::min)(s.branchLength, total - root.distance);
			const Vector3 delta = Vector3::NormalizeOr(offset, side) * length;
			points.push_back({root.position, root.progress, root.distance, s.branchWidth, true});
			for (int j = 1; j <= 4; ++j) {
				const float u = j * 0.25f;
				Vector3 pos = root.position + delta * u + side * (SignedNoise(key + j + 2) * length * 0.4f * u * (1 - u));
				RaycastHit3D hit{};
				const bool clipped = s.stopOnHit && RaycastBeamSegment(world, points.back().position, pos, s.is2D,
					s.collisionMask, s.triggerInteraction, excluded, hit);
				if (clipped) pos = hit.point;
				const float distance = points.back().distance + (pos - points.back().position).Length();
				points.push_back({pos, (std::min)(1.0f, distance / total), distance, clipped ? 0 : s.branchWidth * (1 - u)});
				if (clipped) break;
			}
		}
	}
}

void Engine::UpdateBeamGeometry(ECSWorld& world, Entity owner, BeamRendererRecord& record) {

	auto& runtime = record.runtime;
	const auto& s = record.settings;
	ClearGeometry(runtime);
	if (!runtime.playing || !IsEntityActiveInHierarchy(world, owner)) return;
	auto& excluded = runtime.excluded;
	auto& anchors = runtime.anchors;
	excluded.clear();
	anchors.clear();
	bool complete = true;
	const auto resolve = [&](const BeamEndpoint& point) {
		Entity target = owner;
		if (point.mode == BeamEndpointMode::Object) {
			target = point.runtimeObject.IsValid() ? point.runtimeObject : point.object.Resolve(world, owner);
			if (s.stopOnHit && target.IsValid()) excluded.push_back(target);
		}
		if (!complete) return;
		Vector3 position = point.position;
		if (point.mode != BeamEndpointMode::World) {
			const auto* transform = world.IsAlive(target) && !world.IsPendingDestroy(target) && IsEntityActiveInHierarchy(world, target)
				? world.TryGetComponent<TransformComponent>(target) : nullptr;
			if (!transform) { complete = false; return; }
			ResolvedWorldTransform resolved;
			const bool resolveNow = (transform->isDirty || runtime.timelineControlled) && TransformWorldUtility::ResolveWorldTransform(world, target, resolved);
			position = Vector3::Transform(position, resolveNow ? resolved.matrix : transform->worldMatrix);
		}
		if (s.is2D) position.z = 0;
		if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) { complete = false; return; }
		anchors.push_back(position);
	};
	if (s.stopOnHit) excluded.push_back(owner);
	resolve(s.start);
	for (const auto& waypoint : s.waypoints) resolve(waypoint);
	resolve(s.end);
	const auto tick = static_cast<uint32_t>(std::fmod(std::floor((std::max)(0.0, runtime.time) * s.frequency), 4294967296.0));
	const uint32_t seed = s.seed ^ (tick * 0x85ebca6bu);
	for (size_t i = 1; i < anchors.size(); ++i) {
		Vector3 end = anchors[i];
		if (s.stopOnHit && RaycastBeamSegment(world, anchors[i - 1], end, s.is2D, s.collisionMask, s.triggerInteraction, excluded, runtime.hit)) {
			runtime.hasHit = true;
			runtime.hitSegment = static_cast<int32_t>(i - 1);
			end = runtime.hit.point;
		}
		AddSpan(record, anchors[i - 1], end, seed + static_cast<uint32_t>(i) * 0x9e3779b9u);
		if (runtime.hasHit) break;
	}
	if (runtime.points.empty()) return;
	const float total = runtime.points.back().distance;
	for (auto& point : runtime.points) point.progress = point.distance / total;
	if (s.mode == BeamMode::Lightning) AddBranches(world, record, seed, excluded);
}

void Engine::PlayBeam(ECSWorld& world, Entity owner) {

	if (auto* record = TryGetBeamRenderer(world, owner)) {
		auto& runtime = record->runtime;
		runtime.initialized = runtime.playing = true;
		runtime.time = 0;
		UpdateBeamGeometry(world, owner, *record);
	}
}

void Engine::StopBeam(ECSWorld& world, Entity owner) {

	if (auto* record = TryGetBeamRenderer(world, owner)) {
		record->runtime.initialized = true;
		record->runtime.playing = false;
		record->runtime.time = 0;
		ClearGeometry(record->runtime);
	}
}

void BeamRendererSystem::LateUpdate(ECSWorld& world, SystemContext& context) {

	world.ForEach<BeamRendererComponent>([&](Entity owner, BeamRendererComponent& component) {
		auto* record = world.GetStorage().Get<BeamRendererStorage>().TryGet(component.handle);
		if (!record) return;
		auto& runtime = record->runtime;
		if (!IsEntityActiveInHierarchy(world, owner)) { ClearGeometry(runtime); return; }
		if (!runtime.initialized) {
			runtime.initialized = true;
			runtime.playing = record->settings.playOnStart;
		}
		if (!runtime.playing) { ClearGeometry(runtime); return; }
		if (!runtime.timelineControlled) {
			const float scaled = context.mode == WorldMode::Edit ? context.unscaledDeltaTime : context.deltaTime;
			const auto clock = ResolveLocalTime(world, owner, record->settings.timeSource, TimeChannel::Particle, scaled, context.unscaledDeltaTime);
			if (clock.valid && 0 < clock.deltaTime) runtime.time += clock.deltaTime;
			if (record->settings.playbackMode == BeamPlaybackMode::OneShot && record->settings.duration <= runtime.time) {
				runtime.playing = false;
				ClearGeometry(runtime);
				return;
			}
		}
		UpdateBeamGeometry(world, owner, *record);
	});
}
