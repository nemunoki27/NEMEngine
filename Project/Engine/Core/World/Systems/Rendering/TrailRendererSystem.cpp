#include "TrailRendererSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/TrailRendererComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>

#include <cmath>

using namespace Engine;

namespace {

	bool IsFinite(const Vector3& v) {

		return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
	}

	Vector3 Forward(const TransformComponent& transform) {

		const auto& z = transform.worldMatrix.m[2];
		return Vector3::NormalizeOr({z[0], z[1], z[2]}, {0, 0, 1});
	}
}

void Engine::AgeTrail(TrailRendererRecord& record, double deltaTime) {

	if (!std::isfinite(deltaTime) || deltaTime < 0) return;
	auto& runtime = record.runtime;
	runtime.time += deltaTime;
	const double cutoff = runtime.time - record.settings.lifetime;
	while (!runtime.points.empty() && runtime.points.front().time <= cutoff) runtime.points.pop_front();
}

void Engine::AppendTrailSample(TrailRendererRecord& record, const Vector3& root, const Vector3& tip,
	const Vector3& normal, bool force) {

	auto& runtime = record.runtime;
	if (record.settings.lifetime <= 0 || !IsFinite(root) || !IsFinite(tip) || !IsFinite(normal)) {
		runtime.breakBefore = true;
		return;
	}
	TrailSample point{root, tip, normal, runtime.time, runtime.breakBefore || runtime.points.empty()};
	if (record.settings.is2D) {
		point.root.z = point.tip.z = 0.0f;
		point.normal = {0, 0, 1};
	}
	if (!force && !runtime.breakBefore && !runtime.points.empty()) {
		const auto& last = runtime.points.back();
		const auto a = point.root - last.root, b = point.tip - last.tip;
		const float distance = (std::max)(Vector3::Dot(a, a), Vector3::Dot(b, b));
		if (distance <= 0 || distance < record.settings.minVertexDistance * record.settings.minVertexDistance) return;
	}
	runtime.points.push_back(point);
	runtime.breakBefore = false;
}

void Engine::ClearTrail(ECSWorld& world, Entity owner) {

	if (auto* record = TryGetTrailRenderer(world, owner)) {
		record->runtime.points.clear();
		record->runtime.breakBefore = true;
	}
}

void Engine::BreakTrail(ECSWorld& world, Entity owner) {

	if (auto* record = TryGetTrailRenderer(world, owner)) record->runtime.breakBefore = true;
}

void Engine::SetTrailEmitting(ECSWorld& world, Entity owner, bool emitting) {

	if (auto* record = TryGetTrailRenderer(world, owner); record && record->settings.emitting != emitting) {
		record->settings.emitting = emitting;
		record->runtime.breakBefore = true;
		world.MarkComponentModified<TrailRendererComponent>(owner);
	}
}

void Engine::AddTrailPosition(ECSWorld& world, Entity owner, const Vector3& position) {

	auto* record = TryGetTrailRenderer(world, owner);
	if (!record || record->settings.mode != TrailMode::SinglePoint) return;
	const auto* transform = world.TryGetComponentForBinding<TransformComponent>(owner);
	AppendTrailSample(*record, position, position, transform ? Forward(*transform) : Vector3(0, 0, 1), true);
}

void Engine::AddTrailSegment(ECSWorld& world, Entity owner, const Vector3& root, const Vector3& tip) {

	auto* record = TryGetTrailRenderer(world, owner);
	if (record && record->settings.mode == TrailMode::TwoPoints) AppendTrailSample(*record, root, tip, {0, 0, 1}, true);
}

namespace {

	void AdvanceTrail(ECSWorld& world, Entity owner, TrailRendererRecord& record, double deltaTime) {

		AgeTrail(record, deltaTime);
		if (!record.settings.emitting) return;
		const auto* transform = world.TryGetComponent<TransformComponent>(owner);
		if (!transform) { record.runtime.breakBefore = true; return; }
		if (record.settings.mode == TrailMode::SinglePoint) {
			const Vector3 position = transform->worldMatrix.GetTranslationValue();
			AppendTrailSample(record, position, position, Forward(*transform));
			return;
		}
		const auto resolve = [&](const SceneEntityReference& ref, Entity overrideEntity) -> const TransformComponent* {
			const auto target = overrideEntity != Entity::Null() ? overrideEntity : ref.Resolve(world, owner);
			return world.IsAlive(target) && !world.IsPendingDestroy(target) && IsEntityActiveInHierarchy(world, target) ?
				world.TryGetComponent<TransformComponent>(target) : nullptr;
		};
		const auto* root = resolve(record.settings.root, record.runtime.rootOverride);
		const auto* tip = resolve(record.settings.tip, record.runtime.tipOverride);
		if (!root || !tip) { record.runtime.breakBefore = true; return; }
		AppendTrailSample(record, root->worldMatrix.GetTranslationValue(), tip->worldMatrix.GetTranslationValue(), Forward(*transform));
	}
}

void TrailRendererSystem::LateUpdate(ECSWorld& world, SystemContext& context) {

	world.ForEach<TrailRendererComponent>([&](Entity owner, TrailRendererComponent& component) {
		auto* record = world.GetStorage().Get<TrailRendererStorage>().TryGet(component.handle);
		if (!record) return;
		if (!IsEntityActiveInHierarchy(world, owner)) {
			record->runtime.points.clear();
			record->runtime.breakBefore = true;
			return;
		}
		if (record->runtime.timelineHoldsHistory) return;
		const float scaled = context.mode == WorldMode::Edit ? context.unscaledDeltaTime : context.deltaTime;
		const auto clock = ResolveLocalTime(world, owner, record->settings.timeSource, TimeChannel::Particle, scaled, context.unscaledDeltaTime);
		if (clock.valid && 0 < clock.deltaTime) AdvanceTrail(world, owner, *record, clock.deltaTime);
	});
}
