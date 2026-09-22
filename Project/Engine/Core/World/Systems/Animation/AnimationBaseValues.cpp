#include "AnimationBaseValues.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Animation/AnimationPlayerComponent.h>
#include <Engine/Core/Animation/Clips/AnimationClipManager.h>
#include <Engine/Core/Animation/Properties/AnimationPropertyRegistry.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Systems/Behavior/BehaviorSystem.h>

// c++
#include <algorithm>
#include <cmath>

namespace Engine::AnimationBaseValues {

	void CaptureBaseValues(ECSWorld& world, const Entity& entity,
		const AnimationPlayerComponent& player, SystemContext& context, std::vector<AnimationPreviewBaseValue>& out) {

		out.clear();
		if (!context.assetDatabase || !context.animationClipManager) {
			return;
		}

		// 同じbindingを重複させずに現在値を捕捉する
		const auto captureBase = [&](const std::string& componentName, const std::string& propertyPath,
			AnimationValueType valueType) {

				for (const AnimationPreviewBaseValue& base : out) {
					if (base.binding.componentName == componentName && base.binding.propertyPath == propertyPath) {
						return;
					}
				}
				const std::optional<AnimationPropertyDescriptor> desc = AnimationPropertyRegistry::GetInstance().ResolveProperty(
					world, entity, componentName, propertyPath, valueType);
				if (!desc || !desc->getValue || !desc->hasComponent || !desc->hasComponent(world, entity)) {
					return;
				}
				AnimationPreviewBaseValue base{};
				base.binding.componentName = componentName;
				base.binding.propertyPath = propertyPath;
				base.binding.valueType = valueType;
				// material override未設定などは値が無いので、復元時に除去できるよう有無を記録する
				base.present = !desc->hasValue || desc->hasValue(world, entity);
				if (desc->getValue(world, entity, base.value)) {
					out.emplace_back(std::move(base));
				}
			};

		// 全グループの全クリップが触るプロパティの和集合を一度だけ捕捉する
		bool anyRelative = false;
		for (const AnimationGroup& group : player.groups) {
			for (const AnimationState& state : group.states) {

				const AnimationClipAsset* clip = context.animationClipManager->GetOrLoad(*context.assetDatabase, state.clip);
				if (!clip) {
					continue;
				}
				anyRelative |= state.relativeTransform;
				for (const AnimationCurveTrack& track : clip->curveTracks) {
					captureBase(track.binding.componentName, track.binding.propertyPath, track.binding.valueType);
				}
			}
		}

		// 向き相対クリップは基準の位置/回転が必須なので、未アニメでも捕捉しておく
		if (anyRelative) {
			captureBase("Transform", "localPos", AnimationValueType::Vector3);
			captureBase("Transform", "localRotation", AnimationValueType::Quaternion);
			captureBase("Transform", "localPos2D", AnimationValueType::Vector2);
			captureBase("Transform", "localRotationZ", AnimationValueType::Float);
		}
	}

	void RestoreBaseValues(ECSWorld& world, const Entity& entity, const std::vector<AnimationPreviewBaseValue>& base) {

		for (const AnimationPreviewBaseValue& value : base) {

			const std::optional<AnimationPropertyDescriptor> desc = AnimationPropertyRegistry::GetInstance().ResolveProperty(
				world, entity, value.binding.componentName, value.binding.propertyPath, value.binding.valueType);
			if (!desc || !desc->hasComponent || !desc->hasComponent(world, entity)) {
				continue;
			}
			// Preview前に値が有ったものは戻し、無かったものは除去して既定へ戻す
			if (value.present) {
				if (desc->setValue) {
					desc->setValue(world, entity, value.value);
				}
			} else if (desc->clearValue) {
				desc->clearValue(world, entity);
			}
		}
	}
}
