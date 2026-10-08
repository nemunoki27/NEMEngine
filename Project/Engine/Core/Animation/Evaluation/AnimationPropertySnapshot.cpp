#include "AnimationPropertySnapshot.h"

//============================================================================
//	include
//============================================================================
#include "AnimationValueOperations.h"

// c++
#include <algorithm>

bool Engine::AnimationPropertySnapshot::Contains(std::span<const AnimationPreviewBaseValue> values,
	const AnimationPropertyBinding& binding) {

	return std::any_of(values.begin(), values.end(), [&](const auto& value) {
		return AnimationValueOperations::SameBinding(value.binding, binding);
	});
}

bool Engine::AnimationPropertySnapshot::Capture(ECSWorld& world, const Entity& entity,
	const AnimationPropertyBinding& binding, std::vector<AnimationPreviewBaseValue>& values) {

	if (Contains(values, binding)) return true;
	const auto descriptor = AnimationPropertyRegistry::GetInstance().ResolveProperty(world, entity,
		binding.componentName, binding.propertyPath, binding.valueType);
	if (!descriptor || !descriptor->getValue || !descriptor->hasComponent || !descriptor->hasComponent(world, entity)) return false;
	// 未設定のMaterial値は復元時にoverrideを消す
	AnimationPreviewBaseValue captured;
	captured.binding = binding;
	captured.present = !descriptor->hasValue || descriptor->hasValue(world, entity);
	if (!descriptor->getValue(world, entity, captured.value)) return false;
	captured.restore = !descriptor->snapshotBindings;
	values.push_back(std::move(captured));
	// 一括Propertyは各対象の値と未設定を別々に保持する
	if (descriptor->snapshotBindings) {

		for (const auto& target : descriptor->snapshotBindings(world, entity)) Capture(world, entity, target, values);
	}
	return true;
}

void Engine::AnimationPropertySnapshot::CaptureRelativeTransform(ECSWorld& world, const Entity& entity,
	std::vector<AnimationPreviewBaseValue>& values) {

	// 相対移動の基準姿勢も開始時点で保持する
	Capture(world, entity, { "Transform", "localPos", AnimationValueType::Vector3 }, values);
	Capture(world, entity, { "Transform", "localRotation", AnimationValueType::Quaternion }, values);
	Capture(world, entity, { "Transform", "localPos2D", AnimationValueType::Vector2 }, values);
	Capture(world, entity, { "Transform", "localRotationZ", AnimationValueType::Float }, values);
}

void Engine::AnimationPropertySnapshot::CaptureClip(ECSWorld& world, const Entity& entity,
	const AnimationClipAsset& clip, bool relativeTransform, std::vector<AnimationPreviewBaseValue>& values) {

	// 未捕捉のBindingだけを追加する
	for (const auto& track : clip.curveTracks) Capture(world, entity, track.binding, values);
	if (relativeTransform) CaptureRelativeTransform(world, entity, values);
}

void Engine::AnimationPropertySnapshot::Restore(ECSWorld& world, const Entity& entity,
	std::span<const AnimationPreviewBaseValue> values) {

	for (const auto& value : values) {

		if (!value.restore) continue;
		const auto descriptor = AnimationPropertyRegistry::GetInstance().ResolveProperty(world, entity,
			value.binding.componentName, value.binding.propertyPath, value.binding.valueType);
		if (!descriptor || !descriptor->hasComponent || !descriptor->hasComponent(world, entity)) continue;
		// 開始時に存在した値と未設定を区別して戻す
		if (value.present) {
			if (descriptor->setValue) descriptor->setValue(world, entity, value.value);
		} else if (descriptor->clearValue) descriptor->clearValue(world, entity);
	}
}
