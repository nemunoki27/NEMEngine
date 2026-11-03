#include "MotionAnimationComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/ECS/World/PendingStorageCleanup.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>

// c++
#include <stdexcept>
#include <unordered_map>

using namespace Engine;

namespace {

	struct RemovedPreviewPose {

		Vector3 position{};
		Quaternion rotation{};
		bool applyPosition = false;
		bool applyRotation = false;
	};
	struct RemovedPreviewStorage {

		std::unordered_map<Engine::UUID, RemovedPreviewPose> poses;
	};

	// 保存参照が変わったときだけ実行中の参照と経路を作り直す
	void ApplySettings(MotionAnimationRecord& record, MotionAnimationSettings candidate) {

		const bool targetChanged = nlohmann::json(record.settings.target) != nlohmann::json(candidate.target);
		const bool splineChanged = nlohmann::json(record.settings.spline) != nlohmann::json(candidate.spline);
		const bool modeChanged = record.settings.mode != candidate.mode;
		if (targetChanged) {
			record.runtime.hasTargetOverride = false;
			record.runtime.targetOverride = Entity::Null();
		}
		if (splineChanged) {
			record.runtime.hasSplineOverride = false;
			record.runtime.splineOverride = Entity::Null();
		}
		if (splineChanged || modeChanged) record.runtime.splineSamples.clear();
		record.runtime.resourcesDirty |= targetChanged || splineChanged || modeChanged ||
			record.settings.rotation != candidate.rotation || record.settings.applyRotation != candidate.applyRotation;
		record.settings = std::move(candidate);
	}

	// 値コピーされた参照に新しい所有枠を割り当てる
	MotionAnimationRecord& EnsureMotion(ECSWorld& world, Entity entity, MotionAnimationComponent& component) {

		auto& storage = world.GetStorage().Get<MotionAnimationStorage>();
		auto* record = storage.TryGet(component.handle);
		const auto owner = world.GetUUID(entity);
		if (record && record->owner == owner) return *record;
		component.handle = storage.Emplace(MotionAnimationRecord{record ? record->settings : MotionAnimationSettings{}, {}, owner});
		return *storage.TryGet(component.handle);
	}
}

void MotionAnimationComponent::InitializeStorage(ECSWorld& world, const Entity& entity, MotionAnimationComponent& component) {

	EnsureMotion(world, entity, component);
}

void MotionAnimationComponent::OnAdded(ECSWorld& world, const Entity& entity, MotionAnimationComponent& component) {

	InitializeStorage(world, entity, component);
}

void MotionAnimationComponent::OnRemoved(ECSWorld& world, const Entity& entity) {

	const auto* existing = world.GetStorage().TryGet<RemovedPreviewStorage>();
	if (!existing) return;
	auto& poses = world.GetStorage().Get<RemovedPreviewStorage>().poses;
	const auto found = poses.find(world.GetUUID(entity));
	if (found == poses.end()) return;
	// 保持Componentの移動完了後に、公開されたTransformへ復元する
	if (auto* transform = world.TryGetComponent<TransformComponent>(entity)) {
		if (found->second.applyPosition) transform->localPos = found->second.position;
		if (found->second.applyRotation) transform->localRotation = found->second.rotation;
		MarkTransformSubtreeDirty(world, entity);
	}
	poses.erase(found);
}

void MotionAnimationComponent::ReleaseStorage(ECSWorld& world, const Entity& entity, MotionAnimationComponent& component) {

	auto& storage = world.GetStorage().Get<MotionAnimationStorage>();
	auto* record = storage.TryGet(component.handle);
	if (record && record->owner == world.GetUUID(entity)) {
		const auto& runtime = record->runtime;
		if (runtime.edit && runtime.initialized && (runtime.appliedPosition || runtime.appliedRotation) &&
			world.GetLifetime()->IsAlive() && !world.IsPendingDestroy(entity)) {
			world.GetStorage().Get<RemovedPreviewStorage>().poses[record->owner] = {
				runtime.originalLocalPosition, runtime.originalLocalRotation, runtime.appliedPosition, runtime.appliedRotation};
		}
		storage.Release(component.handle);
	}
	component.handle = {};
}

void MotionAnimationComponent::DeserializeECS(ECSWorld& world, const Entity& entity,
	const nlohmann::json& in, MotionAnimationComponent& component) {

	MotionAnimationSettings candidate;
	from_json(in, candidate);
	ApplySettings(EnsureMotion(world, entity, component), std::move(candidate));
}

void MotionAnimationComponent::SerializeECS(const ECSWorld& world, [[maybe_unused]] const Entity& entity,
	const MotionAnimationComponent& component, nlohmann::json& out) {

	const auto* storage = world.GetStorage().TryGet<MotionAnimationStorage>();
	const auto* record = storage ? storage->TryGet(component.handle) : nullptr;
	if (!record) throw std::logic_error("MotionAnimationの保存データが見つかりません");
	to_json(out, record->settings);
}

void MotionAnimationComponent::CloneStorage(const ECSWorld& sourceWorld, ECSWorld& destinationWorld, const Entity& entity,
	const MotionAnimationComponent& source, MotionAnimationComponent& destination) {

	const auto* storage = sourceWorld.GetStorage().TryGet<MotionAnimationStorage>();
	const auto* record = storage ? storage->TryGet(source.handle) : nullptr;
	destination.handle = destinationWorld.GetStorage().Get<MotionAnimationStorage>().Emplace(
		MotionAnimationRecord{record ? record->settings : MotionAnimationSettings{}, {}, destinationWorld.GetUUID(entity)});
}

const MotionAnimationRecord* Engine::TryGetMotionAnimation(const ECSWorld& world, Entity entity) {

	const auto* component = world.TryGetComponentForBinding<MotionAnimationComponent>(entity);
	const auto* storage = world.GetStorage().TryGet<MotionAnimationStorage>();
	return component && storage ? storage->TryGet(component->handle) : nullptr;
}

MotionAnimationRecord* Engine::TryGetMotionAnimation(ECSWorld& world, Entity entity) {

	auto* component = world.TryGetComponentForBinding<MotionAnimationComponent>(entity);
	if (!component) return nullptr;
	auto& record = EnsureMotion(world, entity, *component);
	TrackPendingStorage<MotionAnimationComponent, MotionAnimationStorage>(world, entity, &MotionAnimationComponent::handle);
	return &record;
}

void Engine::SetMotionAnimationSettings(ECSWorld& world, Entity entity, const MotionAnimationSettings& settings) {

	nlohmann::json json = settings;
	MotionAnimationSettings candidate = json.get<MotionAnimationSettings>();
	PreserveTimeSourceRuntime(settings.timeSource, candidate.timeSource);
	if (auto* record = TryGetMotionAnimation(world, entity)) {
		ApplySettings(*record, std::move(candidate));
		world.MarkComponentModified<MotionAnimationComponent>(entity);
	}
}
