#include "AnimationPropertyEditCapture.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Evaluation/AnimationValueOperations.h>
#include <Engine/Core/Animation/Timeline/TimelineBuiltinEvaluation.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Editor/Tools/Builtin/Animation/AnimationClipEditorUtility.h>

// c++
#include <algorithm>

using namespace Engine;

namespace {

	struct CapturedProperty {

		AnimationPropertyDescriptor descriptor;
		AnimationPropertyValue value{};
		bool present = true;
		AnimationPropertyValue baseline{};
	};
	struct CapturedComponent {

		std::string name;
		std::vector<CapturedProperty> properties;
		Quaternion originRotation = Quaternion::Identity();
		float originAngle = 0.0f;
	};
	struct CaptureState {

		Entity target = Entity::Null();
		std::weak_ptr<IAnimationPropertyRecorder> recorder;
		AnimationPropertyQueryContext query;
		std::vector<CapturedComponent> components;
	};

	// Transformの同じ軸を異なる型で二重記録しない
	bool IsCanonicalTransform(const ECSWorld& world, Entity entity, const AnimationPropertyDescriptor& descriptor) {

		if (descriptor.componentName != "Transform") return true;
		const auto* transform = world.TryGetComponent<TransformComponent>(entity);
		if (!transform) return false;
		const bool planar = transform->dimension == Dimension::Type2D;
		const auto& path = descriptor.propertyPath;
		return planar ? path == "localPos2D" || path == "localRotationZ" || path == "localScale2D" :
			path == "localPos" || path == "localRotation" || path == "localScale";
	}
}

void Engine::AnimationPropertyEditCapture::SetRecorder(ECSWorld& world, Entity target,
	std::weak_ptr<IAnimationPropertyRecorder> recorder, const AnimationPropertyQueryContext& query) {

	auto& state = world.GetStorage().Get<CaptureState>();
	state.target = target;
	state.recorder = std::move(recorder);
	state.query = query;
}

void Engine::AnimationPropertyEditCapture::ClearRecorder(ECSWorld& world, const IAnimationPropertyRecorder* recorder) {

	auto& state = world.GetStorage().Get<CaptureState>();
	const auto current = state.recorder.lock();
	if (current && current.get() != recorder) return;
	state.components.clear();
	state.recorder.reset();
	state.target = Entity::Null();
}

void Engine::AnimationPropertyEditCapture::Begin(ECSWorld& world, Entity target, std::string_view component) {

	auto& state = world.GetStorage().Get<CaptureState>();
	if (state.target != target || state.recorder.expired() || !world.IsAlive(target)) return;
	if (std::ranges::any_of(state.components, [&](const auto& item) { return item.name == component; })) return;
	CapturedComponent capture;
	capture.name = component;
	if (const auto* transform = world.TryGetComponent<TransformComponent>(target)) {
		AnimationPropertyValue rotation = transform->localRotation;
		TryGetTimelinePropertyBase(world, target, {"Transform", "localRotation", AnimationValueType::Quaternion}, rotation);
		if (const auto* value = std::get_if<Quaternion>(&rotation)) capture.originRotation = *value;
		AnimationPropertyValue angle = Quaternion::ToEulerDegrees(transform->localRotation).z;
		TryGetTimelinePropertyBase(world, target, {"Transform", "localRotationZ", AnimationValueType::Float}, angle);
		if (const auto* value = std::get_if<float>(&angle)) capture.originAngle = *value;
	}
	const auto descriptors = AnimationPropertyRegistry::GetInstance().CollectProperties(world, target, state.query);
	for (const auto& descriptor : descriptors) {
		if (descriptor.componentName != component || !descriptor.getValue || !descriptor.setValue || descriptor.snapshotBindings ||
			!IsCanonicalTransform(world, target, descriptor)) continue;
		CapturedProperty property;
		property.descriptor = descriptor;
		property.present = !descriptor.hasValue || descriptor.hasValue(world, target);
		if (descriptor.getValue(world, target, property.value)) {
			property.baseline = property.value;
			TryGetTimelinePropertyBase(world, target, {descriptor.componentName, descriptor.propertyPath, descriptor.valueType}, property.baseline);
			capture.properties.push_back(std::move(property));
		}
	}
	if (!capture.properties.empty()) state.components.push_back(std::move(capture));
}

bool Engine::AnimationPropertyEditCapture::Commit(ECSWorld& world, Entity target, std::string_view component) {

	auto& state = world.GetStorage().Get<CaptureState>();
	if (state.target != target || !world.IsAlive(target)) return false;
	const auto found = std::ranges::find_if(state.components, [&](const auto& item) { return item.name == component; });
	if (found == state.components.end()) return false;
	auto capture = std::move(*found);
	state.components.erase(found);
	const auto recorder = state.recorder.lock();
	if (!recorder) return false;
	std::vector<RecordedAnimationProperty> changes;
	std::vector<size_t> changed;
	for (size_t i = 0; i < capture.properties.size(); ++i) {
		const auto& property = capture.properties[i];
		AnimationPropertyValue after;
		if (!property.descriptor.getValue(world, target, after) ||
			AnimationClipEditorUtility::ApproxEqualValue(property.value, after)) continue;
		const auto& descriptor = property.descriptor;
		changes.push_back({{descriptor.componentName, descriptor.propertyPath, descriptor.valueType}, property.value, after,
			property.baseline, capture.originRotation, capture.originAngle});
		changed.push_back(i);
	}
	if (changes.empty()) return false;
	recorder->Record(changes);
	for (size_t index : changed) {
		const auto& property = capture.properties[index];
		if (!property.present && property.descriptor.clearValue) property.descriptor.clearValue(world, target);
		else property.descriptor.setValue(world, target, property.value);
	}
	return true;
}

void Engine::AnimationPropertyEditCapture::Cancel(ECSWorld& world, Entity target, std::string_view component) {

	auto& state = world.GetStorage().Get<CaptureState>();
	if (state.target == target) std::erase_if(state.components, [&](const auto& item) { return item.name == component; });
}

bool Engine::AnimationPropertyEditCapture::IsEditing(const ECSWorld& world) {

	const auto* state = world.GetStorage().TryGet<CaptureState>();
	return state && !state->recorder.expired() && !state->components.empty();
}

bool Engine::AnimationPropertyEditCapture::HasRecorder(const ECSWorld& world) {

	const auto* state = world.GetStorage().TryGet<CaptureState>();
	return state && !state->recorder.expired();
}

bool Engine::AnimationPropertyEditCapture::RestoreHistory(ECSWorld& world, bool redo) {

	const auto* state = world.GetStorage().TryGet<CaptureState>();
	const auto recorder = state ? state->recorder.lock() : nullptr;
	return recorder && recorder->RestoreHistory(redo);
}
