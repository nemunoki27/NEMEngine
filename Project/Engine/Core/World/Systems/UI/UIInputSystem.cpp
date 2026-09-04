#include "UIInputSystem.h"

//============================================================================
//	include
//============================================================================
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

//============================================================================
//	UIInputSystem internal
//============================================================================
namespace {

	struct SelectableEntry {

		Engine::Entity entity = Engine::Entity::Null();
		Engine::Entity canvas = Engine::Entity::Null();
		Engine::UISelectableComponent* selectable = nullptr;
		Engine::UISelectableRuntimeComponent* runtime = nullptr;
		const Engine::UIElementRuntime* element = nullptr;
		Engine::Vector2 center{};
	};

	Engine::UUID GetLocalFileID(Engine::ECSWorld& world, Engine::Entity entity) {

		const auto* sceneObject = world.TryGetComponent<Engine::SceneObjectComponent>(entity);
		return sceneObject ? sceneObject->localFileID : Engine::UUID{};
	}

	Engine::Vector2 ResolveElementCenter(Engine::ECSWorld& world, Engine::Entity entity,
		const Engine::UIElementRuntime& runtime) {

		Engine::Vector2 localCenter{};
		if (const auto* sprite = world.TryGetComponent<Engine::SpriteRendererComponent>(entity)) {
			localCenter = Engine::Vector2(
				(0.5f - sprite->pivot.x) * sprite->size.x,
				(0.5f - sprite->pivot.y) * sprite->size.y);
		} else if (const auto* text = world.TryGetComponent<Engine::TextRendererComponent>(entity)) {
			const auto* layout =
				world.TryGetComponent<Engine::TextLayoutRuntimeComponent>(entity);
			const Engine::Vector2 boundsSize =
				layout ? layout->boundsSize : Engine::Vector2::AnyInit(0.0f);
			localCenter = Engine::Vector2(
				(0.5f - text->pivot.x) * boundsSize.x,
				(0.5f - text->pivot.y) * boundsSize.y);
		}
		const Engine::Vector3 center = Engine::Vector3::Transform(
			Engine::Vector3(localCenter.x, localCenter.y, 0.0f), runtime.screenMatrix);
		return Engine::Vector2(center.x, center.y);
	}

	SelectableEntry* FindEntryByLocalFileID(Engine::ECSWorld& world,
		std::vector<SelectableEntry>& entries, Engine::Entity canvas, Engine::UUID localFileID) {

		if (!localFileID) {
			return nullptr;
		}
		for (SelectableEntry& entry : entries) {
			if (entry.canvas == canvas && GetLocalFileID(world, entry.entity) == localFileID) {
				return &entry;
			}
		}
		return nullptr;
	}

	SelectableEntry* FindFirstInteractable(std::vector<SelectableEntry>& entries, Engine::Entity canvas) {

		for (SelectableEntry& entry : entries) {
			if (entry.canvas == canvas && entry.selectable->interactable) {
				return &entry;
			}
		}
		return nullptr;
	}

	SelectableEntry* FindAutomaticNavigation(std::vector<SelectableEntry>& entries,
		const SelectableEntry& current, const Engine::Vector2& direction, bool wrap) {

		SelectableEntry* best = nullptr;
		float bestScore = (std::numeric_limits<float>::max)();
		for (SelectableEntry& candidate : entries) {

			if (candidate.entity == current.entity || candidate.canvas != current.canvas ||
				!candidate.selectable->interactable) {
				continue;
			}
			const Engine::Vector2 delta = candidate.center - current.center;
			const float forward = Engine::Vector2::Dot(delta, direction);
			if (forward <= 0.001f) {
				continue;
			}
			const float perpendicular = std::abs(delta.x * direction.y - delta.y * direction.x);
			const float score = forward + perpendicular * 2.0f;
			if (score < bestScore) {
				bestScore = score;
				best = &candidate;
			}
		}
		if (best || !wrap) {
			return best;
		}

		// 指定方向の反対端から最も近い要素へラップする
		float edge = (std::numeric_limits<float>::max)();
		for (SelectableEntry& candidate : entries) {

			if (candidate.entity == current.entity || candidate.canvas != current.canvas ||
				!candidate.selectable->interactable) {
				continue;
			}
			const float projection = Engine::Vector2::Dot(candidate.center, direction);
			const Engine::Vector2 delta = candidate.center - current.center;
			const float perpendicular = std::abs(delta.x * direction.y - delta.y * direction.x);
			const float score = projection + perpendicular * 0.25f;
			if (score < edge) {
				edge = score;
				best = &candidate;
			}
		}
		return best;
	}

	bool IsTransitionTableEntry(Engine::ECSWorld& world,
		std::span<const Engine::CanvasNavigationCell> cells,
		const SelectableEntry& entry) {

		const Engine::UUID localFileID = GetLocalFileID(world, entry.entity);
		return localFileID && std::find_if(cells.begin(), cells.end(),
			[localFileID](const Engine::CanvasNavigationCell& cell) {
				return cell.localFileID == localFileID;
			}) != cells.end();
	}

	SelectableEntry* FindFirstTransitionTableEntry(Engine::ECSWorld& world,
		std::vector<SelectableEntry>& entries, Engine::Entity canvasEntity,
		std::span<const Engine::CanvasNavigationCell> cells) {

		for (const Engine::CanvasNavigationCell& cell : cells) {

			SelectableEntry* entry = FindEntryByLocalFileID(
				world, entries, canvasEntity, cell.localFileID);
			if (entry && entry->selectable->interactable) {
				return entry;
			}
		}
		return nullptr;
	}

	SelectableEntry* FindTransitionTableNavigation(Engine::ECSWorld& world,
		std::vector<SelectableEntry>& entries, Engine::Entity canvasEntity,
		const Engine::CanvasComponent& canvas, const SelectableEntry& current,
		const Engine::Vector2& direction,
		std::span<const Engine::CanvasNavigationCell> cells) {

		const int32_t rows = canvas.navigationRows;
		const int32_t columns = canvas.navigationColumns;
		if (rows <= 0 || columns <= 0 || cells.empty()) {
			return nullptr;
		}

		const Engine::UUID currentLocalFileID = GetLocalFileID(world, current.entity);
		const auto currentCell = std::find_if(cells.begin(), cells.end(),
			[currentLocalFileID](const Engine::CanvasNavigationCell& cell) {
				return cell.localFileID == currentLocalFileID;
			});
		if (currentCell == cells.end()) {
			return FindFirstTransitionTableEntry(
				world, entries, canvasEntity, cells);
		}

		const int32_t currentIndex = static_cast<int32_t>(
			std::distance(cells.begin(), currentCell));
		int32_t row = currentIndex / columns;
		int32_t column = currentIndex % columns;
		const int32_t directionX = direction.x < 0.0f ? -1 : (0.0f < direction.x ? 1 : 0);
		const int32_t directionY = direction.y < 0.0f ? -1 : (0.0f < direction.y ? 1 : 0);
		const int32_t maxStep = directionX != 0 ? columns : rows;

		for (int32_t step = 0; step < maxStep; ++step) {

			row += directionY;
			column += directionX;
			if (canvas.wrapNavigation) {
				if (row < 0) { row = rows - 1; }
				else if (rows <= row) { row = 0; }
				if (column < 0) { column = columns - 1; }
				else if (columns <= column) { column = 0; }
			} else if (row < 0 || rows <= row || column < 0 || columns <= column) {
				return nullptr;
			}

			const size_t index = static_cast<size_t>(row * columns + column);
			if (cells.size() <= index) {
				continue;
			}
			SelectableEntry* next = FindEntryByLocalFileID(
				world, entries, canvasEntity, cells[index].localFileID);
			if (next && next->entity != current.entity && next->selectable->interactable) {
				return next;
			}
		}
		return nullptr;
	}

	bool IsBindingTriggered(Engine::Input& input,
		std::span<const Engine::CanvasInputBinding> bindings,
		Engine::CanvasInputAction action, Engine::CanvasInputDevice device) {

		for (const Engine::CanvasInputBinding& binding : bindings) {
			if (binding.action != action || binding.device != device) {
				continue;
			}
			const bool triggered = device == Engine::CanvasInputDevice::Keyboard ?
				input.TriggerKey(static_cast<BYTE>(binding.code)) :
				input.TriggerGamepadButton(
					static_cast<GamePadButtons>(binding.code));
			if (triggered) {
				return true;
			}
		}
		return false;
	}

	bool IsBindingHeld(Engine::Input& input,
		std::span<const Engine::CanvasInputBinding> bindings,
		Engine::CanvasInputAction action, Engine::CanvasInputDevice device) {

		for (const Engine::CanvasInputBinding& binding : bindings) {
			if (binding.action != action || binding.device != device) {
				continue;
			}
			const bool held = device == Engine::CanvasInputDevice::Keyboard ?
				input.PushKey(static_cast<BYTE>(binding.code)) :
				input.PushGamepadButton(
					static_cast<GamePadButtons>(binding.code));
			if (held) {
				return true;
			}
		}
		return false;
	}

	Engine::Vector2 ReadTriggeredNavigationDirection(Engine::Input& input,
		const Engine::CanvasComponent& canvas,
		std::span<const Engine::CanvasInputBinding> bindings) {

		if ((canvas.keyboardInputEnabled &&
			IsBindingTriggered(input, bindings,
				Engine::CanvasInputAction::Up, Engine::CanvasInputDevice::Keyboard)) ||
			(canvas.gamepadInputEnabled &&
				IsBindingTriggered(input, bindings,
					Engine::CanvasInputAction::Up, Engine::CanvasInputDevice::Gamepad))) {
			return Engine::Vector2(0.0f, -1.0f);
		}
		if ((canvas.keyboardInputEnabled &&
			IsBindingTriggered(input, bindings,
				Engine::CanvasInputAction::Down, Engine::CanvasInputDevice::Keyboard)) ||
			(canvas.gamepadInputEnabled &&
				IsBindingTriggered(input, bindings,
					Engine::CanvasInputAction::Down, Engine::CanvasInputDevice::Gamepad))) {
			return Engine::Vector2(0.0f, 1.0f);
		}
		if ((canvas.keyboardInputEnabled &&
			IsBindingTriggered(input, bindings,
				Engine::CanvasInputAction::Left, Engine::CanvasInputDevice::Keyboard)) ||
			(canvas.gamepadInputEnabled &&
				IsBindingTriggered(input, bindings,
					Engine::CanvasInputAction::Left, Engine::CanvasInputDevice::Gamepad))) {
			return Engine::Vector2(-1.0f, 0.0f);
		}
		if ((canvas.keyboardInputEnabled &&
			IsBindingTriggered(input, bindings,
				Engine::CanvasInputAction::Right, Engine::CanvasInputDevice::Keyboard)) ||
			(canvas.gamepadInputEnabled &&
				IsBindingTriggered(input, bindings,
					Engine::CanvasInputAction::Right, Engine::CanvasInputDevice::Gamepad))) {
			return Engine::Vector2(1.0f, 0.0f);
		}
		return {};
	}

	Engine::Vector2 ReadHeldNavigationDirection(Engine::Input& input,
		const Engine::CanvasComponent& canvas,
		std::span<const Engine::CanvasInputBinding> bindings) {

		if ((canvas.keyboardInputEnabled &&
			IsBindingHeld(input, bindings,
				Engine::CanvasInputAction::Up, Engine::CanvasInputDevice::Keyboard)) ||
			(canvas.gamepadInputEnabled &&
				IsBindingHeld(input, bindings,
					Engine::CanvasInputAction::Up, Engine::CanvasInputDevice::Gamepad))) {
			return Engine::Vector2(0.0f, -1.0f);
		}
		if ((canvas.keyboardInputEnabled &&
			IsBindingHeld(input, bindings,
				Engine::CanvasInputAction::Down, Engine::CanvasInputDevice::Keyboard)) ||
			(canvas.gamepadInputEnabled &&
				IsBindingHeld(input, bindings,
					Engine::CanvasInputAction::Down, Engine::CanvasInputDevice::Gamepad))) {
			return Engine::Vector2(0.0f, 1.0f);
		}
		if ((canvas.keyboardInputEnabled &&
			IsBindingHeld(input, bindings,
				Engine::CanvasInputAction::Left, Engine::CanvasInputDevice::Keyboard)) ||
			(canvas.gamepadInputEnabled &&
				IsBindingHeld(input, bindings,
					Engine::CanvasInputAction::Left, Engine::CanvasInputDevice::Gamepad))) {
			return Engine::Vector2(-1.0f, 0.0f);
		}
		if ((canvas.keyboardInputEnabled &&
			IsBindingHeld(input, bindings,
				Engine::CanvasInputAction::Right, Engine::CanvasInputDevice::Keyboard)) ||
			(canvas.gamepadInputEnabled &&
				IsBindingHeld(input, bindings,
					Engine::CanvasInputAction::Right, Engine::CanvasInputDevice::Gamepad))) {
			return Engine::Vector2(1.0f, 0.0f);
		}

		const Engine::Vector2 stick = canvas.gamepadInputEnabled &&
			canvas.gamepadLeftStickEnabled ? input.GetLeftStickVal() : Engine::Vector2{};
		if (canvas.stickThreshold <= std::abs(stick.x) ||
			canvas.stickThreshold <= std::abs(stick.y)) {
			return std::abs(stick.x) > std::abs(stick.y) ?
				Engine::Vector2(stick.x < 0.0f ? -1.0f : 1.0f, 0.0f) :
				Engine::Vector2(0.0f, stick.y < 0.0f ? -1.0f : 1.0f);
		}
		return {};
	}

	bool IsSubmitTriggered(Engine::Input& input,
		const Engine::CanvasComponent& canvas,
		std::span<const Engine::CanvasInputBinding> bindings) {

		return (canvas.keyboardInputEnabled &&
			IsBindingTriggered(input, bindings,
				Engine::CanvasInputAction::Submit,
				Engine::CanvasInputDevice::Keyboard)) ||
			(canvas.gamepadInputEnabled &&
				IsBindingTriggered(input, bindings,
					Engine::CanvasInputAction::Submit,
					Engine::CanvasInputDevice::Gamepad));
	}

	Engine::MaterialParameterSet* ResolveMaterialParameters(
		Engine::ECSWorld& world, Engine::Entity target) {

		if (auto* sprite = world.TryGetComponent<Engine::SpriteRendererComponent>(target)) {
			return &sprite->materialInstance;
		}
		if (auto* text = world.TryGetComponent<Engine::TextRendererComponent>(target)) {
			return &text->materialInstance;
		}
		return nullptr;
	}

	bool SetMaterialParameter(Engine::MaterialParameterSet& parameters,
		Engine::MaterialParameterID id, std::string_view name,
		Engine::MaterialParameterSemantic semantic,
		const Engine::MaterialParameterValue& value) {

		const Engine::MaterialParameterSet& readOnly = parameters;
		const Engine::MaterialParameterValue* current = readOnly.Find(id);
		if (current && current->value == value.value) {
			return false;
		}
		parameters.Set(id, name, semantic, value);
		return true;
	}

	void NotifyRendererMaterialModified(Engine::ECSWorld& world) {

		// UI遷移は保存対象の編集ではないため描画キャッシュだけを更新する
		world.MarkRenderDataModified();
	}

	const Engine::UITransitionStyle& ResolveStyle(
		const Engine::UISelectableComponent& selectable,
		const Engine::UISelectableRuntimeComponent& runtime) {

		switch (runtime.state) {
		case Engine::UISelectableState::Selected: return selectable.selected;
		case Engine::UISelectableState::Submitted: return selectable.submitted;
		case Engine::UISelectableState::Disabled: return selectable.disabled;
		case Engine::UISelectableState::Normal:
		default: return selectable.normal;
		}
	}

	size_t GetStyleIndex(Engine::UISelectableState state) {

		switch (state) {
		case Engine::UISelectableState::Selected: return 1;
		case Engine::UISelectableState::Submitted: return 2;
		case Engine::UISelectableState::Disabled: return 3;
		case Engine::UISelectableState::Normal:
		default: return 0;
		}
	}

	std::array<const Engine::UITransitionStyle*, 4> GetStyles(
		const Engine::UISelectableComponent& selectable) {

		return { &selectable.normal,&selectable.selected,&selectable.submitted,&selectable.disabled };
	}

	bool HasStateTransitionRuntime(const Engine::UISelectableComponent& selectable) {

		for (const Engine::UITransitionStyle* style : GetStyles(selectable)) {
			if ((style->animationEnabled && style->useAnimationClip) || style->sound) {
				return true;
			}
		}
		return false;
	}

	void ApplySelectableBaseVisual(Engine::ECSWorld& world, Engine::Entity entity,
		Engine::UISelectableRuntimeComponent& runtime) {

		if (!runtime.initialized) {
			return;
		}
		if (auto* parameters = ResolveMaterialParameters(world, entity)) {
			bool materialChanged = false;
			if (runtime.hadBaseColor) {
				Engine::MaterialParameterValue value{};
				value.value = runtime.baseColor;
				materialChanged |= SetMaterialParameter(*parameters,
					Engine::MaterialParameterIDs::BaseColor,
					Engine::MaterialParameterNames::BaseColor,
					Engine::MaterialParameterSemantic::BaseColor,
					value);
			} else {
				materialChanged |= parameters->erase(
					Engine::MaterialParameterIDs::BaseColor) != 0;
			}
			if (runtime.hadBaseTexture) {
				Engine::MaterialParameterValue value{};
				value.value = runtime.baseTexture;
				materialChanged |= SetMaterialParameter(*parameters,
					Engine::MaterialParameterIDs::BaseColorTexture,
					Engine::MaterialParameterNames::BaseColorTexture,
					Engine::MaterialParameterSemantic::BaseColorTexture,
					value);
			} else {
				materialChanged |= parameters->erase(
					Engine::MaterialParameterIDs::BaseColorTexture) != 0;
			}
			if (materialChanged) {
				NotifyRendererMaterialModified(world);
			}
		}
		if (auto* transform = world.TryGetComponent<Engine::TransformComponent>(entity)) {
			if (transform->localScale != runtime.baseScale) {
				transform->localScale = runtime.baseScale;
				Engine::MarkTransformSubtreeDirty(world, entity);
			}
		}
		runtime.currentColor = runtime.baseColor;
		runtime.startColor = runtime.baseColor;
		runtime.currentScale = runtime.baseScale;
		runtime.startScale = runtime.baseScale;
	}

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
					world, entity, base.binding.componentName,
					base.binding.propertyPath, base.binding.valueType);
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
		Engine::AnimationClipEvaluator::EvaluateClipValues(
			world, entity, clip, resolvedTime, baseValues, values);
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
			audio->PlayOneShot(
				Engine::Algorithm::PathToUTF8(fullPath.stem()),
				std::clamp(style.soundVolume, 0.0f, 1.0f));
		}
	}

	void RestoreSelectableVisual(Engine::ECSWorld& world, Engine::Entity entity,
		Engine::UISelectableRuntimeComponent& runtime) {

		if (!runtime.initialized) {
			return;
		}
		ApplySelectableBaseVisual(world, entity, runtime);

		// Canvas入力対象から外れた時点で次回初期化用の状態へ戻す
		runtime = {};
	}

	void ResetStateThisFrame(Engine::UISelectableRuntimeComponent& runtime) {

		runtime.normalThisFrame = false;
		runtime.selectedThisFrame = false;
		runtime.submittedThisFrame = false;
		runtime.disabledThisFrame = false;
	}

	void SetStateThisFrame(Engine::UISelectableRuntimeComponent& runtime,
		Engine::UISelectableState state) {

		switch (state) {
		case Engine::UISelectableState::Normal:
			runtime.normalThisFrame = true;
			break;
		case Engine::UISelectableState::Selected:
			runtime.selectedThisFrame = true;
			break;
		case Engine::UISelectableState::Submitted:
			runtime.submittedThisFrame = true;
			break;
		case Engine::UISelectableState::Disabled:
			runtime.disabledThisFrame = true;
			break;
		}
	}

	void ResetCanvasInputRuntime(Engine::CanvasRuntimeComponent& runtime) {

		runtime = {};
	}

	void InitializeSelectableRuntime(Engine::ECSWorld& world, Engine::Entity entity,
		const Engine::UISelectableComponent& selectable,
		Engine::UISelectableRuntimeComponent& runtime) {

		if (runtime.initialized) {
			return;
		}

		runtime.baseColor = Engine::Color4::White();
		runtime.baseTexture = {};
		runtime.hadBaseColor = false;
		runtime.hadBaseTexture = false;
		if (auto* parameters = ResolveMaterialParameters(world, entity)) {
			if (const Engine::MaterialParameterValue* value =
				parameters->Find(
					Engine::MaterialParameterIDs::BaseColor)) {

				if (const auto* color =
					std::get_if<Engine::Color4>(
						&value->value)) {
					runtime.baseColor = *color;
					runtime.hadBaseColor = true;
				}
			}
			if (const Engine::MaterialParameterValue* value =
				parameters->Find(Engine::MaterialParameterIDs::BaseColorTexture)) {

				if (const auto* texture =
					std::get_if<Engine::AssetID>(
						&value->value)) {
					runtime.baseTexture = *texture;
					runtime.hadBaseTexture = true;
				}
			}
		}
		runtime.baseScale = Engine::Vector3::AnyInit(1.0f);
		if (const auto* transform = world.TryGetComponent<Engine::TransformComponent>(entity)) {
			runtime.baseScale = transform->localScale;
		}
		runtime.currentColor = runtime.baseColor;
		runtime.startColor = runtime.baseColor;
		runtime.currentScale = runtime.baseScale;
		runtime.startScale = runtime.baseScale;
		runtime.previousState = runtime.state;
		const Engine::UITransitionStyle& style =
			ResolveStyle(selectable, runtime);
		runtime.colorTransitionElapsed = style.colorTransitionDuration;
		runtime.scaleTransitionElapsed = style.scaleTransitionDuration;
		runtime.initialized = true;
	}

	bool ConfigureAnimationRuntime(Engine::ECSWorld& world, Engine::Entity entity,
		const Engine::UISelectableComponent& selectable,
		Engine::UISelectableRuntimeComponent& selectableRuntime,
		Engine::SystemContext& context,
		Engine::UISelectableAnimationRuntime& animationRuntime) {

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
		const Engine::UISelectableComponent& selectable,
		Engine::UISelectableRuntimeComponent& selectableRuntime,
		Engine::SystemContext& context,
		Engine::UISelectableAnimationRuntime& animationRuntime,
		float deltaTime) {

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
			context.animationClipManager->GetOrLoad(
				*context.assetDatabase, animationRuntime.activeClip);
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

	void UpdateSelectableVisual(Engine::ECSWorld& world, const SelectableEntry& entry, float deltaTime) {

		Engine::UISelectableComponent& selectable = *entry.selectable;
		Engine::UISelectableRuntimeComponent& runtime = *entry.runtime;
		InitializeSelectableRuntime(
			world, entry.entity, selectable, runtime);
		if (!runtime.initialized) {
			return;
		}

		if (runtime.previousState != runtime.state) {
			runtime.previousState = runtime.state;
			runtime.colorTransitionElapsed = 0.0f;
			runtime.scaleTransitionElapsed = 0.0f;
			runtime.startColor = runtime.currentColor;
			runtime.startScale = runtime.currentScale;
		}
		const Engine::UITransitionStyle& style =
			ResolveStyle(selectable, runtime);
		if (!style.animationEnabled) {
			ApplySelectableBaseVisual(world, entry.entity, runtime);
			if (style.overrideTexture) {
				if (auto* parameters = ResolveMaterialParameters(world, entry.entity)) {
					Engine::MaterialParameterValue value{};
					value.value = style.texture;
					if (SetMaterialParameter(*parameters,
						Engine::MaterialParameterIDs::BaseColorTexture,
						Engine::MaterialParameterNames::BaseColorTexture,
						Engine::MaterialParameterSemantic::BaseColorTexture,
						value)) {
						NotifyRendererMaterialModified(world);
					}
				}
			}
			return;
		}
		if (style.useAnimationClip) {
			if (style.overrideTexture) {
				if (auto* parameters = ResolveMaterialParameters(world, entry.entity)) {
					Engine::MaterialParameterValue value{};
					value.value = style.texture;
					if (SetMaterialParameter(*parameters,
						Engine::MaterialParameterIDs::BaseColorTexture,
						Engine::MaterialParameterNames::BaseColorTexture,
						Engine::MaterialParameterSemantic::BaseColorTexture,
						value)) {
						NotifyRendererMaterialModified(world);
					}
				}
			}
			return;
		}
		const float elapsedTime = (std::max)(deltaTime, 0.0f);
		runtime.colorTransitionElapsed += elapsedTime;
		runtime.scaleTransitionElapsed += elapsedTime;
		const float colorProgress = style.colorTransitionDuration <= 0.0f ? 1.0f :
			std::clamp(runtime.colorTransitionElapsed /
				style.colorTransitionDuration, 0.0f, 1.0f);
		const float scaleProgress = style.scaleTransitionDuration <= 0.0f ? 1.0f :
			std::clamp(runtime.scaleTransitionElapsed /
				style.scaleTransitionDuration, 0.0f, 1.0f);
		const Engine::Color4 targetColor = runtime.baseColor * style.color;
		const Engine::Vector3 targetScale(
			runtime.baseScale.x * style.scale.x,
			runtime.baseScale.y * style.scale.y,
			runtime.baseScale.z);
		runtime.currentColor = Engine::Color4::Lerp(
			runtime.startColor, targetColor,
			EasedValue(style.colorEasing, colorProgress));
		runtime.currentScale = Engine::Vector3::Lerp(
			runtime.startScale, targetScale,
			EasedValue(style.scaleEasing, scaleProgress));

		if (auto* parameters = ResolveMaterialParameters(world, entry.entity)) {
			bool materialChanged = false;
			Engine::MaterialParameterValue color{};
			color.value = runtime.currentColor;
			materialChanged |= SetMaterialParameter(*parameters,
				Engine::MaterialParameterIDs::BaseColor,
				Engine::MaterialParameterNames::BaseColor,
				Engine::MaterialParameterSemantic::BaseColor,
				color);
			if (style.overrideTexture) {
				Engine::MaterialParameterValue texture{};
				texture.value = style.texture;
				materialChanged |= SetMaterialParameter(*parameters,
					Engine::MaterialParameterIDs::BaseColorTexture,
					Engine::MaterialParameterNames::BaseColorTexture,
					Engine::MaterialParameterSemantic::BaseColorTexture,
					texture);
			} else if (runtime.hadBaseTexture) {
				Engine::MaterialParameterValue texture{};
				texture.value = runtime.baseTexture;
				materialChanged |= SetMaterialParameter(*parameters,
					Engine::MaterialParameterIDs::BaseColorTexture,
					Engine::MaterialParameterNames::BaseColorTexture,
					Engine::MaterialParameterSemantic::BaseColorTexture,
					texture);
			} else {
				materialChanged |= parameters->erase(
					Engine::MaterialParameterIDs::BaseColorTexture) != 0;
			}
			if (materialChanged) {
				NotifyRendererMaterialModified(world);
			}
		}
		if (auto* transform = world.TryGetComponent<Engine::TransformComponent>(entry.entity)) {
			if (transform->localScale != runtime.currentScale) {
				transform->localScale = runtime.currentScale;
				Engine::MarkTransformSubtreeDirty(world, entry.entity);
			}
		}
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

	void ClickButton(Engine::ECSWorld& world, Engine::Entity entity) {

		if (auto* button = world.TryGetComponent<Engine::UIImageButtonComponent>(entity); button && button->enabled) {
			if (auto* runtime =
				world.TryGetComponent<Engine::UIImageButtonRuntimeComponent>(entity)) {
				runtime->clickedThisFrame = true;
			}
		}
		if (auto* button = world.TryGetComponent<Engine::UITextButtonComponent>(entity); button && button->enabled) {
			if (auto* runtime =
				world.TryGetComponent<Engine::UITextButtonRuntimeComponent>(entity)) {
				runtime->clickedThisFrame = true;
			}
		}
	}
}

//============================================================================
//	UIInputSystem classMethods
//============================================================================
void Engine::UIInputSystem::Update(ECSWorld& world, SystemContext& context) {

	UIRuntimeService& runtimeService = UIRuntimeService::GetInstance();
	runtimeService.SetGameplayInputBlocked(false);
	world.ForEach<UIImageButtonRuntimeComponent>(
		[](Entity, UIImageButtonRuntimeComponent& runtime) {
		runtime.clickedThisFrame = false;
		});
	world.ForEach<UITextButtonRuntimeComponent>(
		[](Entity, UITextButtonRuntimeComponent& runtime) {
		runtime.clickedThisFrame = false;
		});
	world.ForEach<UISelectableRuntimeComponent>(
		[](Entity, UISelectableRuntimeComponent& runtime) {
		ResetStateThisFrame(runtime);
		});

	const bool isPlay = context.mode == WorldMode::Play;
	if (runtimeService.GetElements(world).empty()) {
		runtimeService.Build(world, EngineContext::GetWindowSetting().gameSizeFloat);
	}

	std::vector<SelectableEntry> entries;
	std::unordered_set<UUID> activeSelectableEntities;
	for (const UIElementRuntime& element : runtimeService.GetElements(world)) {

		if (!world.IsAlive(element.entity) || !world.IsAlive(element.canvas)) {
			continue;
		}
		const auto* canvas = world.TryGetComponent<CanvasComponent>(element.canvas);
		if (!canvas || !canvas->enabled || (!isPlay && !canvas->inputInEditMode)) {
			continue;
		}
		auto* selectable = world.TryGetComponent<UISelectableComponent>(element.entity);
		auto* selectableRuntime =
			world.TryGetComponent<UISelectableRuntimeComponent>(element.entity);
		if (!selectable || !selectableRuntime) {
			continue;
		}
		SelectableEntry entry{};
		entry.entity = element.entity;
		entry.canvas = element.canvas;
		entry.selectable = selectable;
		entry.runtime = selectableRuntime;
		entry.element = &element;
		entry.center = ResolveElementCenter(world, entry.entity, *entry.element);
		entries.emplace_back(entry);
		activeSelectableEntities.emplace(world.GetUUID(entry.entity));
	}

	// Canvasから外れたUIはクリップ適用前の値へ戻して再生状態を破棄する
	for (auto it = animationRuntimes_.begin(); it != animationRuntimes_.end();) {

		if (activeSelectableEntities.contains(it->first)) {
			++it;
			continue;
		}
		const Entity entity = world.FindByUUID(it->first);
		if (world.IsAlive(entity)) {
			if (it->second.applied) {
				RestoreAnimationBaseValues(world, entity, it->second.baseValues);
			}
		}
		it = animationRuntimes_.erase(it);
	}

	// Canvas入力対象から外れたUIは開始前の表示へ戻す
	world.ForEach<UISelectableComponent, UISelectableRuntimeComponent>(
		[&](Entity entity, [[maybe_unused]] UISelectableComponent& selectable,
			UISelectableRuntimeComponent& selectableRuntime) {

		if (!activeSelectableEntities.contains(world.GetUUID(entity))) {
			RestoreSelectableVisual(world, entity, selectableRuntime);
		}
		});

	// 入力を無効にしたCanvasの選択状態を破棄
	world.ForEach<CanvasComponent, CanvasRuntimeComponent>(
		[isPlay](Entity, CanvasComponent& canvas,
			CanvasRuntimeComponent& runtime) {

		if (!canvas.enabled || (!isPlay && !canvas.inputInEditMode)) {
			ResetCanvasInputRuntime(runtime);
		}
			});
	if (entries.empty()) {
		return;
	}

	// Canvasごとに選択状態を初期化する
	std::vector<Entity> canvases;
	for (const SelectableEntry& entry : entries) {
		if (std::find(canvases.begin(), canvases.end(), entry.canvas) == canvases.end()) {
			canvases.emplace_back(entry.canvas);
		}
	}
	for (Entity canvasEntity : canvases) {

		auto& canvas = world.GetComponent<CanvasComponent>(canvasEntity);
		auto& canvasRuntime =
			world.GetComponent<CanvasRuntimeComponent>(canvasEntity);
		const std::span<const CanvasNavigationCell> cells =
			GetCanvasNavigationCells(world, canvasEntity);
		if (!canvas.blockInputAfterSubmit) {
			canvasRuntime.inputLocked = false;
		}
		SelectableEntry* selected = FindEntryByLocalFileID(world, entries,
			canvasEntity, canvasRuntime.selectedLocalFileID);
		if (canvas.navigationMode == CanvasNavigationMode::TransitionTable) {
			if (!selected || !selected->selectable->interactable ||
				!IsTransitionTableEntry(world, cells, *selected)) {
				selected = FindEntryByLocalFileID(world, entries,
					canvasEntity, canvas.firstSelectedLocalFileID);
			}
			if (!selected || !selected->selectable->interactable ||
				!IsTransitionTableEntry(world, cells, *selected)) {
				selected = FindFirstTransitionTableEntry(
					world, entries, canvasEntity, cells);
			}
		} else {
			if (!selected || !selected->selectable->interactable) {
				selected = FindEntryByLocalFileID(world, entries,
					canvasEntity, canvas.firstSelectedLocalFileID);
			}
			if (!selected || !selected->selectable->interactable) {
				selected = FindFirstInteractable(entries, canvasEntity);
			}
		}
		canvasRuntime.selectedLocalFileID =
			selected ? GetLocalFileID(world, selected->entity) : UUID{};
	}

	Input* input = Input::GetInstance();
	bool consumedInput = false;
	Entity activeCanvas = Entity::Null();
	for (Entity candidate : canvases) {
		const auto& canvas = world.GetComponent<CanvasComponent>(candidate);
		const auto& canvasRuntime =
			world.GetComponent<CanvasRuntimeComponent>(candidate);
		if (!canvasRuntime.selectedLocalFileID) {
			continue;
		}
		if (!world.IsAlive(activeCanvas)) {
			activeCanvas = candidate;
			continue;
		}
		const auto& current = world.GetComponent<CanvasComponent>(activeCanvas);
		if (current.sortingLayer < canvas.sortingLayer ||
			(current.sortingLayer == canvas.sortingLayer && current.order < canvas.order)) {
			activeCanvas = candidate;
		}
	}

	Vector2 direction{};
	bool navigationTriggered = false;
	if (input && world.IsAlive(activeCanvas) &&
		!world.GetComponent<CanvasRuntimeComponent>(activeCanvas).inputLocked) {

		auto& canvas = world.GetComponent<CanvasComponent>(activeCanvas);
		auto& canvasRuntime =
			world.GetComponent<CanvasRuntimeComponent>(activeCanvas);
		const std::span<const CanvasInputBinding> bindings =
			GetCanvasInputBindings(world, activeCanvas);
		Vector2 heldDirection = ReadTriggeredNavigationDirection(
			*input, canvas, bindings);
		if (heldDirection == Vector2{}) {
			heldDirection = ReadHeldNavigationDirection(
				*input, canvas, bindings);
		}
		if (heldDirection != Vector2{}) {
			if (heldDirection != canvasRuntime.repeatDirection) {
				canvasRuntime.repeatDirection = heldDirection;
				canvasRuntime.repeatElapsed = 0.0f;
				canvasRuntime.repeatStarted = false;
				direction = heldDirection;
				navigationTriggered = true;
			} else {
				canvasRuntime.repeatElapsed += context.unscaledDeltaTime;
				const float threshold = canvasRuntime.repeatStarted ?
					(std::max)(canvas.repeatInterval, 0.01f) : (std::max)(canvas.repeatDelay, 0.0f);
				if (threshold <= canvasRuntime.repeatElapsed) {
					canvasRuntime.repeatElapsed = 0.0f;
					canvasRuntime.repeatStarted = true;
					direction = heldDirection;
					navigationTriggered = true;
				}
			}
		} else {
			canvasRuntime.repeatDirection = {};
			canvasRuntime.repeatElapsed = 0.0f;
			canvasRuntime.repeatStarted = false;
		}
	}

	if (navigationTriggered && world.IsAlive(activeCanvas)) {

		auto& canvas = world.GetComponent<CanvasComponent>(activeCanvas);
		auto& canvasRuntime =
			world.GetComponent<CanvasRuntimeComponent>(activeCanvas);
		SelectableEntry* current = FindEntryByLocalFileID(world, entries,
			activeCanvas, canvasRuntime.selectedLocalFileID);
		SelectableEntry* next = nullptr;
		if (current) {
			if (canvas.navigationMode == CanvasNavigationMode::TransitionTable) {
				next = FindTransitionTableNavigation(world, entries,
					activeCanvas, canvas, *current, direction,
					GetCanvasNavigationCells(world, activeCanvas));
			} else {
				next = FindAutomaticNavigation(entries, *current, direction, canvas.wrapNavigation);
			}
		}
		if (next && next->selectable->interactable) {
			canvasRuntime.selectedLocalFileID =
				GetLocalFileID(world, next->entity);
			consumedInput = true;
		}
	}

	if (input && world.IsAlive(activeCanvas)) {

		auto& canvas = world.GetComponent<CanvasComponent>(activeCanvas);
		auto& canvasRuntime =
			world.GetComponent<CanvasRuntimeComponent>(activeCanvas);
		if (!canvasRuntime.inputLocked) {
			if (SelectableEntry* selected = FindEntryByLocalFileID(world, entries,
				activeCanvas, canvasRuntime.selectedLocalFileID);
				selected && IsSubmitTriggered(
					*input, canvas, GetCanvasInputBindings(world, activeCanvas))) {

				selected->runtime->submitted = true;
				selected->runtime->submittedThisFrame = true;
				selected->runtime->previousState =
					UISelectableState::Selected;
				if (auto runtime = animationRuntimes_.find(world.GetUUID(selected->entity));
					runtime != animationRuntimes_.end()) {
					if (runtime->second.stateInitialized &&
						runtime->second.state == GetStyleIndex(UISelectableState::Submitted)) {
						runtime->second.state = static_cast<uint8_t>(
							GetStyleIndex(UISelectableState::Selected));
					}
				}
				PlayStateSound(selected->selectable->submitted, context);
				ClickButton(world, selected->entity);
				consumedInput = true;

				if (canvas.blockInputAfterSubmit) {
					canvasRuntime.inputLocked = true;
					canvasRuntime.repeatDirection = {};
					canvasRuntime.repeatElapsed = 0.0f;
					canvasRuntime.repeatStarted = false;
				}
			}
		}
	}

	// 入力状態から表示状態を決定して遷移を進める
	for (SelectableEntry& entry : entries) {

		auto& canvas = world.GetComponent<CanvasComponent>(entry.canvas);
		auto& canvasRuntime =
			world.GetComponent<CanvasRuntimeComponent>(entry.canvas);
		const UUID localFileID = GetLocalFileID(world, entry.entity);
		UISelectableRuntimeComponent& selectableRuntime = *entry.runtime;
		const UISelectableState previousState = selectableRuntime.state;
		if (selectableRuntime.submitted && canvas.blockInputAfterSubmit) {
			canvasRuntime.inputLocked = true;
		}
		if (selectableRuntime.submitted && canvasRuntime.inputLocked) {
			selectableRuntime.state = UISelectableState::Submitted;
		} else if (!entry.selectable->interactable) {
			selectableRuntime.submitted = false;
			selectableRuntime.state = UISelectableState::Disabled;
		} else if (selectableRuntime.submitted) {
			selectableRuntime.state = UISelectableState::Submitted;
		} else if (canvasRuntime.selectedLocalFileID == localFileID) {
			selectableRuntime.state = UISelectableState::Selected;
		} else {
			selectableRuntime.state = UISelectableState::Normal;
		}
		if (previousState != selectableRuntime.state) {
			SetStateThisFrame(selectableRuntime, selectableRuntime.state);
		}

		UISelectableAnimationRuntime* animationRuntime = nullptr;
		auto runtime = animationRuntimes_.find(world.GetUUID(entry.entity));
		if (runtime != animationRuntimes_.end() || HasStateTransitionRuntime(*entry.selectable)) {
			if (runtime == animationRuntimes_.end()) {
				runtime = animationRuntimes_.try_emplace(world.GetUUID(entry.entity)).first;
			}
			animationRuntime = &runtime->second;
			UpdateSelectableAnimation(world, entry.entity,
				*entry.selectable, selectableRuntime,
				context, runtime->second, context.unscaledDeltaTime);
		}
		UpdateSelectableVisual(world, entry, context.unscaledDeltaTime);

		if (selectableRuntime.submitted && !canvasRuntime.inputLocked &&
			IsSubmitTransitionFinished(
				*entry.selectable, selectableRuntime, animationRuntime)) {
			selectableRuntime.submitted = false;
		}
	}

	if (isPlay && world.IsAlive(activeCanvas)) {
		const auto& canvas = world.GetComponent<CanvasComponent>(activeCanvas);
		runtimeService.SetGameplayInputBlocked(canvas.blockGameplayInput && consumedInput);
	}
}

void Engine::UIInputSystem::OnWorldExit(ECSWorld& world, [[maybe_unused]] SystemContext& context) {

	RestoreEditModeVisuals(world);
	UIRuntimeService::GetInstance().Clear(world);
}

void Engine::UIInputSystem::RestoreEditModeVisuals(ECSWorld& world) {

	for (const auto& [uuid, runtime] : animationRuntimes_) {

		const Entity entity = world.FindByUUID(uuid);
		if (world.IsAlive(entity) && runtime.applied) {
			RestoreAnimationBaseValues(world, entity, runtime.baseValues);
		}
	}
	animationRuntimes_.clear();
	world.ForEach<UISelectableRuntimeComponent>(
		[&](Entity entity, UISelectableRuntimeComponent& runtime) {
		RestoreSelectableVisual(world, entity, runtime);
		});
	world.ForEach<CanvasRuntimeComponent>(
		[](Entity, CanvasRuntimeComponent& runtime) {
			ResetCanvasInputRuntime(runtime);
			});
	UIRuntimeService::GetInstance().SetGameplayInputBlocked(false);
}
