#include "UISelectableAnimation.h"

//============================================================================
//	include
//============================================================================
#include "UISelectableStyle.h"
#include <Engine/Core/World/UI/UIRuntimeService.h>
#include <Engine/Core/World/Components/UI/CanvasComponent.h>
#include <Engine/Core/World/Components/UI/UISelectableComponent.h>
#include <Engine/Core/World/Components/UI/UIImageButtonComponent.h>
#include <Engine/Core/World/Components/UI/UITextButtonComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/Animation/Clips/AnimationClipManager.h>
#include <Engine/Core/Animation/Properties/AnimationPropertyRegistry.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Audio/AudioSystem.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Runtime/Context/EngineContext.h>
#include <Engine/Core/Platform/Input/InputSystem.h>

// c++
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace Engine::UISelectableAnimation {
	using namespace UISelectableStyle;

	void CaptureAnimationBaseValue(Engine::ECSWorld& world, Engine::Entity entity,
		const std::string& componentName, const std::string& propertyPath,
		Engine::AnimationValueType valueType, std::vector<Engine::AnimationPreviewBaseValue>& out) {

		for (const Engine::AnimationPreviewBaseValue& base : out) {
			if (base.binding.componentName == componentName &&
				base.binding.propertyPath == propertyPath && base.binding.valueType == valueType) {
				return;
			}
		}
		const std::optional<Engine::AnimationPropertyDescriptor> descriptor =
			Engine::AnimationPropertyRegistry::GetInstance().ResolveProperty(
				world, entity, componentName, propertyPath, valueType);
		if (!descriptor || !descriptor->getValue || !descriptor->hasComponent ||
			!descriptor->hasComponent(world, entity)) {
			return;
		}

		Engine::AnimationPreviewBaseValue base{};
		base.binding.componentName = componentName;
		base.binding.propertyPath = propertyPath;
		base.binding.valueType = valueType;
		base.present = !descriptor->hasValue || descriptor->hasValue(world, entity);
		if (descriptor->getValue(world, entity, base.value)) {
			out.emplace_back(std::move(base));
		}
	}

	void CaptureAnimationBaseValues(Engine::ECSWorld& world, Engine::Entity entity,
		const Engine::UISelectableComponent& selectable, Engine::SystemContext& context,
		std::vector<Engine::AnimationPreviewBaseValue>& out) {

		out.clear();
		if (!context.assetDatabase || !context.animationClipManager) {
			return;
		}
		for (const Engine::UITransitionStyle* style : GetStyles(selectable)) {

			if (!style->animationEnabled || !style->useAnimationClip || !style->animationClip) {
				continue;
			}
			const Engine::AnimationClipAsset* clip =
				context.animationClipManager->GetOrLoad(*context.assetDatabase, style->animationClip);
			if (!clip) {
				continue;
			}
			for (const Engine::AnimationCurveTrack& track : clip->curveTracks) {
				CaptureAnimationBaseValue(world, entity, track.binding.componentName,
					track.binding.propertyPath, track.binding.valueType, out);
			}
			if (clip->relativeTransform) {
				CaptureAnimationBaseValue(world, entity, "Transform", "localPos",
					Engine::AnimationValueType::Vector3, out);
				CaptureAnimationBaseValue(world, entity, "Transform", "localRotation",
					Engine::AnimationValueType::Quaternion, out);
				CaptureAnimationBaseValue(world, entity, "Transform", "localPos2D",
					Engine::AnimationValueType::Vector2, out);
				CaptureAnimationBaseValue(world, entity, "Transform", "localRotationZ",
					Engine::AnimationValueType::Float, out);
			}
		}
	}

	void RestoreAnimationBaseValues(Engine::ECSWorld& world, Engine::Entity entity,
		std::span<const Engine::AnimationPreviewBaseValue> baseValues) {

		for (const Engine::AnimationPreviewBaseValue& base : baseValues) {

			const std::optional<Engine::AnimationPropertyDescriptor> descriptor =
				Engine::AnimationPropertyRegistry::GetInstance().ResolveProperty(
					world, entity, base.binding.componentName, base.binding.propertyPath, base.binding.valueType);
			if (!descriptor || !descriptor->hasComponent || !descriptor->hasComponent(world, entity)) {
				continue;
			}
			if (base.present) {
				if (descriptor->setValue) {
					descriptor->setValue(world, entity, base.value);
				}
			} else if (descriptor->clearValue) {
				descriptor->clearValue(world, entity);
			}
		}
	}

	void ApplyAnimationClipOnce(Engine::ECSWorld& world, Engine::Entity entity,
		const Engine::AnimationClipAsset& clip, float time,
		std::span<const Engine::AnimationPreviewBaseValue> baseValues) {

		Engine::AnimationResolvedTime resolvedTime{};
		resolvedTime.clipTime = std::clamp(time, 0.0f, (std::max)(clip.duration, 0.001f));

		std::vector<Engine::AnimationEvaluatedValue> values;
		Engine::AnimationClipEvaluator::EvaluateClipValues( world, entity, clip, resolvedTime, baseValues, values);
		if (clip.relativeTransform) {
			Engine::AnimationResolvedTime neutralTime{};
			std::vector<Engine::AnimationEvaluatedValue> neutralValues;
			Engine::AnimationClipEvaluator::EvaluateClipValues(
				world, entity, clip, neutralTime, baseValues, neutralValues);
			Engine::AnimationClipEvaluator::ComposeRelativeTransform(values, baseValues, neutralValues);
		} else {
			Engine::AnimationClipEvaluator::PreserveUnkeyedChannels(world, entity, clip, values);
		}
		Engine::AnimationClipEvaluator::WriteValues(world, entity, values);
	}

	void PlayStateSound(const Engine::UITransitionStyle& style, Engine::SystemContext& context) {

		if (!style.sound || !context.assetDatabase) {
			return;
		}
		const std::filesystem::path fullPath = context.assetDatabase->ResolveFullPath(style.sound);
		if (fullPath.empty()) {
			return;
		}

		Engine::Audio* audio = Engine::Audio::GetInstance();
		if (audio->EnsureLoaded(fullPath)) {
			audio->PlayOneShot( Engine::Algorithm::PathToUTF8(fullPath.stem()),
				std::clamp(style.soundVolume, 0.0f, 1.0f));
		}
	}

	bool ConfigureAnimationRuntime(Engine::ECSWorld& world, Engine::Entity entity,
		const Engine::UISelectableComponent& selectable, Engine::UISelectableRuntimeComponent& selectableRuntime,
		Engine::SystemContext& context, Engine::UISelectableAnimationRuntime& animationRuntime) {

		std::array<Engine::AssetID, 4> clips{};
		std::array<bool, 4> animations{};
		std::array<bool, 4> useClips{};
		const auto styles = GetStyles(selectable);
		for (size_t index = 0; index < styles.size(); ++index) {
			clips[index] = styles[index]->animationClip;
			animations[index] = styles[index]->animationEnabled;
			useClips[index] = styles[index]->useAnimationClip;
		}

		const bool changed = !animationRuntime.configured ||
			animationRuntime.configuredClips != clips ||
			animationRuntime.configuredAnimations != animations ||
			animationRuntime.configuredUseClips != useClips;
		if (changed) {

			if (animationRuntime.applied) {
				RestoreAnimationBaseValues(
					world, entity, animationRuntime.baseValues);
			}
			ApplySelectableBaseVisual(world, entity, selectableRuntime);
			animationRuntime.configuredClips = clips;
			animationRuntime.configuredAnimations = animations;
			animationRuntime.configuredUseClips = useClips;
			animationRuntime.baseValues.clear();
			animationRuntime.activeClip = {};
			animationRuntime.time = 0.0f;
			animationRuntime.configured = true;
			animationRuntime.baseCaptured = false;
			animationRuntime.stateInitialized = false;
			animationRuntime.playing = false;
			animationRuntime.applied = false;
		}
		if (!animationRuntime.baseCaptured &&
			context.assetDatabase && context.animationClipManager) {
			CaptureAnimationBaseValues(
				world, entity, selectable, context, animationRuntime.baseValues);
			animationRuntime.baseCaptured = true;
		}
		return changed;
	}

	bool UpdateSelectableAnimation(Engine::ECSWorld& world, Engine::Entity entity,
		const Engine::UISelectableComponent& selectable, Engine::UISelectableRuntimeComponent& selectableRuntime,
		Engine::SystemContext& context, Engine::UISelectableAnimationRuntime& animationRuntime, float deltaTime) {

		InitializeSelectableRuntime(
			world, entity, selectable, selectableRuntime);
		const bool hadState = animationRuntime.stateInitialized;
		const uint8_t previousState = animationRuntime.state;
		const bool configurationChanged =
			ConfigureAnimationRuntime(world, entity, selectable,
				selectableRuntime, context, animationRuntime);
		const uint8_t currentState =
			static_cast<uint8_t>(GetStyleIndex(selectableRuntime.state));
		const bool actualStateChanged = hadState && previousState != currentState;
		const bool startState = configurationChanged || !hadState || actualStateChanged;
		const Engine::UITransitionStyle& style =
			ResolveStyle(selectable, selectableRuntime);

		if (startState) {

			if (animationRuntime.applied) {
				RestoreAnimationBaseValues(
					world, entity, animationRuntime.baseValues);
			}
			if (animationRuntime.applied ||
				(style.animationEnabled && style.useAnimationClip)) {
				ApplySelectableBaseVisual(world, entity, selectableRuntime);
			}
			animationRuntime.activeClip =
				style.animationEnabled && style.useAnimationClip ?
				style.animationClip : Engine::AssetID{};
			animationRuntime.time = 0.0f;
			animationRuntime.state = currentState;
			animationRuntime.stateInitialized = true;
			animationRuntime.playing =
				style.animationEnabled && style.useAnimationClip &&
				static_cast<bool>(animationRuntime.activeClip);
			animationRuntime.applied = false;

			if (actualStateChanged &&
				selectableRuntime.state != Engine::UISelectableState::Submitted) {
				PlayStateSound(style, context);
			}
		}
		if (!style.animationEnabled || !style.useAnimationClip) {
			return false;
		}
		if (!animationRuntime.activeClip ||
			!context.assetDatabase || !context.animationClipManager) {
			animationRuntime.playing = false;
			return true;
		}
		const Engine::AnimationClipAsset* clip =
			context.animationClipManager->GetOrLoad( *context.assetDatabase, animationRuntime.activeClip);
		if (!clip) {
			animationRuntime.playing = false;
			return true;
		}
		if (!animationRuntime.playing && animationRuntime.applied) {
			return true;
		}

		if (!startState) {
			animationRuntime.time += (std::max)(deltaTime, 0.0f);
		}
		const float duration = (std::max)(clip->duration, 0.001f);
		animationRuntime.time =
			(std::min)(animationRuntime.time, duration);
		ApplyAnimationClipOnce(world, entity, *clip,
			animationRuntime.time, animationRuntime.baseValues);
		animationRuntime.applied = true;
		if (animationRuntime.time >= duration) {
			animationRuntime.playing = false;
		}
		return true;
	}

	bool IsSubmitTransitionFinished(const Engine::UISelectableComponent& selectable,
		const Engine::UISelectableRuntimeComponent& selectableRuntime,
		const Engine::UISelectableAnimationRuntime* animationRuntime) {

		if (!selectable.submitted.animationEnabled) {
			return true;
		}
		if (selectable.submitted.useAnimationClip) {
			return animationRuntime && animationRuntime->stateInitialized &&
				animationRuntime->state == GetStyleIndex(Engine::UISelectableState::Submitted) &&
				!animationRuntime->playing;
		}
		return selectable.submitted.colorTransitionDuration <=
			selectableRuntime.colorTransitionElapsed &&
			selectable.submitted.scaleTransitionDuration <=
			selectableRuntime.scaleTransitionElapsed;
	}
}
