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
			localCenter = Engine::Vector2(
				(0.5f - text->pivot.x) * text->runtimeLayout.boundsSize.x,
				(0.5f - text->pivot.y) * text->runtimeLayout.boundsSize.y);
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

	bool IsTransitionTableEntry(Engine::ECSWorld& world, const Engine::CanvasComponent& canvas,
		const SelectableEntry& entry) {

		const Engine::UUID localFileID = GetLocalFileID(world, entry.entity);
		return localFileID && std::find(canvas.navigationTable.cells.begin(),
			canvas.navigationTable.cells.end(), localFileID) != canvas.navigationTable.cells.end();
	}

	SelectableEntry* FindFirstTransitionTableEntry(Engine::ECSWorld& world,
		std::vector<SelectableEntry>& entries, Engine::Entity canvasEntity,
		const Engine::CanvasComponent& canvas) {

		for (Engine::UUID localFileID : canvas.navigationTable.cells) {

			SelectableEntry* entry = FindEntryByLocalFileID(world, entries, canvasEntity, localFileID);
			if (entry && entry->selectable->interactable) {
				return entry;
			}
		}
		return nullptr;
	}

	SelectableEntry* FindTransitionTableNavigation(Engine::ECSWorld& world,
		std::vector<SelectableEntry>& entries, Engine::Entity canvasEntity,
		const Engine::CanvasComponent& canvas, const SelectableEntry& current,
		const Engine::Vector2& direction) {

		const int32_t rows = canvas.navigationTable.rows;
		const int32_t columns = canvas.navigationTable.columns;
		if (rows <= 0 || columns <= 0 || canvas.navigationTable.cells.empty()) {
			return nullptr;
		}

		const Engine::UUID currentLocalFileID = GetLocalFileID(world, current.entity);
		const auto currentCell = std::find(canvas.navigationTable.cells.begin(),
			canvas.navigationTable.cells.end(), currentLocalFileID);
		if (currentCell == canvas.navigationTable.cells.end()) {
			return FindFirstTransitionTableEntry(world, entries, canvasEntity, canvas);
		}

		const int32_t currentIndex = static_cast<int32_t>(
			std::distance(canvas.navigationTable.cells.begin(), currentCell));
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
			if (canvas.navigationTable.cells.size() <= index) {
				continue;
			}
			SelectableEntry* next = FindEntryByLocalFileID(
				world, entries, canvasEntity, canvas.navigationTable.cells[index]);
			if (next && next->entity != current.entity && next->selectable->interactable) {
				return next;
			}
		}
		return nullptr;
	}

	Engine::Vector2 ReadTriggeredNavigationDirection(Engine::Input& input) {

		if (input.TriggerKey(DIK_UP) || input.TriggerKey(DIK_W) ||
			input.TriggerGamepadButton(GamePadButtons::ARROW_UP)) {
			return Engine::Vector2(0.0f, -1.0f);
		}
		if (input.TriggerKey(DIK_DOWN) || input.TriggerKey(DIK_S) ||
			input.TriggerGamepadButton(GamePadButtons::ARROW_DOWN)) {
			return Engine::Vector2(0.0f, 1.0f);
		}
		if (input.TriggerKey(DIK_LEFT) || input.TriggerKey(DIK_A) ||
			input.TriggerGamepadButton(GamePadButtons::ARROW_LEFT)) {
			return Engine::Vector2(-1.0f, 0.0f);
		}
		if (input.TriggerKey(DIK_RIGHT) || input.TriggerKey(DIK_D) ||
			input.TriggerGamepadButton(GamePadButtons::ARROW_RIGHT)) {
			return Engine::Vector2(1.0f, 0.0f);
		}
		return {};
	}

	Engine::Vector2 ReadHeldNavigationDirection(Engine::Input& input, float stickThreshold) {

		if (input.PushKey(DIK_UP) || input.PushKey(DIK_W) ||
			input.PushGamepadButton(GamePadButtons::ARROW_UP)) {
			return Engine::Vector2(0.0f, -1.0f);
		}
		if (input.PushKey(DIK_DOWN) || input.PushKey(DIK_S) ||
			input.PushGamepadButton(GamePadButtons::ARROW_DOWN)) {
			return Engine::Vector2(0.0f, 1.0f);
		}
		if (input.PushKey(DIK_LEFT) || input.PushKey(DIK_A) ||
			input.PushGamepadButton(GamePadButtons::ARROW_LEFT)) {
			return Engine::Vector2(-1.0f, 0.0f);
		}
		if (input.PushKey(DIK_RIGHT) || input.PushKey(DIK_D) ||
			input.PushGamepadButton(GamePadButtons::ARROW_RIGHT)) {
			return Engine::Vector2(1.0f, 0.0f);
		}

		const Engine::Vector2 stick = input.GetLeftStickVal();
		if (stickThreshold <= std::abs(stick.x) || stickThreshold <= std::abs(stick.y)) {
			return std::abs(stick.x) > std::abs(stick.y) ?
				Engine::Vector2(stick.x < 0.0f ? -1.0f : 1.0f, 0.0f) :
				Engine::Vector2(0.0f, stick.y < 0.0f ? -1.0f : 1.0f);
		}
		return {};
	}

	bool IsSubmitTriggered(Engine::Input& input, const Engine::UISelectableComponent& selectable) {

		for (KeyDIKCode key : selectable.submitKeys) {
			if (input.TriggerKey(static_cast<BYTE>(key))) {
				return true;
			}
		}
		for (GamePadButtons button : selectable.submitGamepadButtons) {
			if (button != GamePadButtons::Counts && input.TriggerGamepadButton(button)) {
				return true;
			}
		}
		return false;
	}

	std::unordered_map<std::string, Engine::MaterialParameterValue>* ResolveMaterialParameters(
		Engine::ECSWorld& world, Engine::Entity target) {

		if (auto* sprite = world.TryGetComponent<Engine::SpriteRendererComponent>(target)) {
			return &sprite->parameterOverrides;
		}
		if (auto* text = world.TryGetComponent<Engine::TextRendererComponent>(target)) {
			return &text->parameterOverrides;
		}
		return nullptr;
	}

	const Engine::UITransitionStyle& ResolveStyle(const Engine::UISelectableComponent& selectable) {

		switch (selectable.runtimeState) {
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
			if (style->useAnimationClip || style->sound) {
				return true;
			}
		}
		return false;
	}

	void ApplySelectableBaseVisual(Engine::ECSWorld& world, Engine::Entity entity,
		Engine::UISelectableComponent& selectable) {

		if (!selectable.runtimeInitialized) {
			return;
		}
		if (auto* parameters = ResolveMaterialParameters(world, entity)) {
			if (selectable.runtimeHadBaseColor) {
				(*parameters)["color"].value = selectable.runtimeBaseColor;
			} else {
				parameters->erase("color");
			}
			if (selectable.runtimeHadBaseTexture) {
				(*parameters)["baseColorTexture"].value = selectable.runtimeBaseTexture;
			} else {
				parameters->erase("baseColorTexture");
			}
		}
		if (auto* transform = world.TryGetComponent<Engine::TransformComponent>(entity)) {
			transform->localScale = selectable.runtimeBaseScale;
			transform->isDirty = true;
		}
		selectable.runtimeCurrentColor = selectable.runtimeBaseColor;
		selectable.runtimeStartColor = selectable.runtimeBaseColor;
		selectable.runtimeCurrentScale = selectable.runtimeBaseScale;
		selectable.runtimeStartScale = selectable.runtimeBaseScale;
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

			if (!style->useAnimationClip || !style->animationClip) {
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
		if (audio->EnsureLoaded(fullPath.string())) {
			audio->PlayOneShot(fullPath.stem().string(), std::clamp(style.soundVolume, 0.0f, 1.0f));
		}
	}

	void RestoreSelectableVisual(Engine::ECSWorld& world, Engine::Entity entity,
		Engine::UISelectableComponent& selectable) {

		if (!selectable.runtimeInitialized) {
			return;
		}
		ApplySelectableBaseVisual(world, entity, selectable);

		selectable.runtimeState = Engine::UISelectableState::Normal;
		selectable.runtimePreviousState = Engine::UISelectableState::Normal;
		selectable.runtimeColorTransitionElapsed = 0.0f;
		selectable.runtimeScaleTransitionElapsed = 0.0f;
		selectable.runtimeHadBaseColor = false;
		selectable.runtimeHadBaseTexture = false;
		selectable.runtimeInitialized = false;
		selectable.runtimeSubmitted = false;
		selectable.runtimeSubmittedThisFrame = false;
	}

	void ResetCanvasInputRuntime(Engine::CanvasComponent& canvas) {

		canvas.runtimeSelectedLocalFileID = {};
		canvas.runtimeRepeatDirection = {};
		canvas.runtimeRepeatElapsed = 0.0f;
		canvas.runtimeRepeatStarted = false;
		canvas.runtimeInputLocked = false;
	}

	void InitializeSelectableRuntime(Engine::ECSWorld& world, Engine::Entity entity,
		Engine::UISelectableComponent& selectable) {

		if (selectable.runtimeInitialized) {
			return;
		}

		selectable.runtimeBaseColor = Engine::Color4::White();
		selectable.runtimeBaseTexture = {};
		selectable.runtimeHadBaseColor = false;
		selectable.runtimeHadBaseTexture = false;
		if (auto* parameters = ResolveMaterialParameters(world, entity)) {
			if (const auto colorIt = parameters->find("color"); colorIt != parameters->end()) {
				if (const auto* color = std::get_if<Engine::Color4>(&colorIt->second.value)) {
					selectable.runtimeBaseColor = *color;
					selectable.runtimeHadBaseColor = true;
				}
			}
			if (const auto textureIt = parameters->find("baseColorTexture"); textureIt != parameters->end()) {
				if (const auto* texture = std::get_if<Engine::AssetID>(&textureIt->second.value)) {
					selectable.runtimeBaseTexture = *texture;
					selectable.runtimeHadBaseTexture = true;
				}
			}
		}
		selectable.runtimeBaseScale = Engine::Vector3::AnyInit(1.0f);
		if (const auto* transform = world.TryGetComponent<Engine::TransformComponent>(entity)) {
			selectable.runtimeBaseScale = transform->localScale;
		}
		selectable.runtimeCurrentColor = selectable.runtimeBaseColor;
		selectable.runtimeStartColor = selectable.runtimeBaseColor;
		selectable.runtimeCurrentScale = selectable.runtimeBaseScale;
		selectable.runtimeStartScale = selectable.runtimeBaseScale;
		selectable.runtimePreviousState = selectable.runtimeState;
		const Engine::UITransitionStyle& style = ResolveStyle(selectable);
		selectable.runtimeColorTransitionElapsed = style.colorTransitionDuration;
		selectable.runtimeScaleTransitionElapsed = style.scaleTransitionDuration;
		selectable.runtimeInitialized = true;
	}

	bool ConfigureAnimationRuntime(Engine::ECSWorld& world, Engine::Entity entity,
		Engine::UISelectableComponent& selectable, Engine::SystemContext& context,
		Engine::UISelectableAnimationRuntime& runtime) {

		std::array<Engine::AssetID, 4> clips{};
		std::array<bool, 4> useClips{};
		const auto styles = GetStyles(selectable);
		for (size_t index = 0; index < styles.size(); ++index) {
			clips[index] = styles[index]->animationClip;
			useClips[index] = styles[index]->useAnimationClip;
		}

		const bool changed = !runtime.configured ||
			runtime.configuredClips != clips || runtime.configuredUseClips != useClips;
		if (changed) {

			if (runtime.applied) {
				RestoreAnimationBaseValues(world, entity, runtime.baseValues);
			}
			ApplySelectableBaseVisual(world, entity, selectable);
			runtime.configuredClips = clips;
			runtime.configuredUseClips = useClips;
			runtime.baseValues.clear();
			runtime.activeClip = {};
			runtime.time = 0.0f;
			runtime.configured = true;
			runtime.baseCaptured = false;
			runtime.stateInitialized = false;
			runtime.playing = false;
			runtime.applied = false;
		}
		if (!runtime.baseCaptured && context.assetDatabase && context.animationClipManager) {
			CaptureAnimationBaseValues(world, entity, selectable, context, runtime.baseValues);
			runtime.baseCaptured = true;
		}
		return changed;
	}

	bool UpdateSelectableAnimation(Engine::ECSWorld& world, Engine::Entity entity,
		Engine::UISelectableComponent& selectable, Engine::SystemContext& context,
		Engine::UISelectableAnimationRuntime& runtime, float deltaTime) {

		InitializeSelectableRuntime(world, entity, selectable);
		const bool hadState = runtime.stateInitialized;
		const uint8_t previousState = runtime.state;
		const bool configurationChanged =
			ConfigureAnimationRuntime(world, entity, selectable, context, runtime);
		const uint8_t currentState = static_cast<uint8_t>(GetStyleIndex(selectable.runtimeState));
		const bool actualStateChanged = hadState && previousState != currentState;
		const bool startState = configurationChanged || !hadState || actualStateChanged;
		const Engine::UITransitionStyle& style = ResolveStyle(selectable);

		if (startState) {

			if (runtime.applied) {
				RestoreAnimationBaseValues(world, entity, runtime.baseValues);
			}
			if (runtime.applied || style.useAnimationClip) {
				ApplySelectableBaseVisual(world, entity, selectable);
			}
			runtime.activeClip = style.useAnimationClip ? style.animationClip : Engine::AssetID{};
			runtime.time = 0.0f;
			runtime.state = currentState;
			runtime.stateInitialized = true;
			runtime.playing = style.useAnimationClip && static_cast<bool>(runtime.activeClip);
			runtime.applied = false;

			if (actualStateChanged &&
				selectable.runtimeState != Engine::UISelectableState::Submitted) {
				PlayStateSound(style, context);
			}
		}
		if (!style.useAnimationClip) {
			return false;
		}
		if (!runtime.activeClip || !context.assetDatabase || !context.animationClipManager) {
			runtime.playing = false;
			return true;
		}
		const Engine::AnimationClipAsset* clip =
			context.animationClipManager->GetOrLoad(*context.assetDatabase, runtime.activeClip);
		if (!clip) {
			runtime.playing = false;
			return true;
		}
		if (!runtime.playing && runtime.applied) {
			return true;
		}

		if (!startState) {
			runtime.time += (std::max)(deltaTime, 0.0f);
		}
		const float duration = (std::max)(clip->duration, 0.001f);
		runtime.time = (std::min)(runtime.time, duration);
		ApplyAnimationClipOnce(world, entity, *clip, runtime.time, runtime.baseValues);
		runtime.applied = true;
		if (runtime.time >= duration) {
			runtime.playing = false;
		}
		return true;
	}

	void UpdateSelectableVisual(Engine::ECSWorld& world, const SelectableEntry& entry, float deltaTime) {

		Engine::UISelectableComponent& selectable = *entry.selectable;
		InitializeSelectableRuntime(world, entry.entity, selectable);
		if (!selectable.runtimeInitialized) {
			return;
		}

		if (selectable.runtimePreviousState != selectable.runtimeState) {
			selectable.runtimePreviousState = selectable.runtimeState;
			selectable.runtimeColorTransitionElapsed = 0.0f;
			selectable.runtimeScaleTransitionElapsed = 0.0f;
			selectable.runtimeStartColor = selectable.runtimeCurrentColor;
			selectable.runtimeStartScale = selectable.runtimeCurrentScale;
		}
		const Engine::UITransitionStyle& style = ResolveStyle(selectable);
		if (style.useAnimationClip) {
			return;
		}
		const float elapsedTime = (std::max)(deltaTime, 0.0f);
		selectable.runtimeColorTransitionElapsed += elapsedTime;
		selectable.runtimeScaleTransitionElapsed += elapsedTime;
		const float colorProgress = style.colorTransitionDuration <= 0.0f ? 1.0f :
			std::clamp(selectable.runtimeColorTransitionElapsed / style.colorTransitionDuration, 0.0f, 1.0f);
		const float scaleProgress = style.scaleTransitionDuration <= 0.0f ? 1.0f :
			std::clamp(selectable.runtimeScaleTransitionElapsed / style.scaleTransitionDuration, 0.0f, 1.0f);
		const Engine::Color4 targetColor = selectable.runtimeBaseColor * style.color;
		const Engine::Vector3 targetScale(
			selectable.runtimeBaseScale.x * style.scale.x,
			selectable.runtimeBaseScale.y * style.scale.y,
			selectable.runtimeBaseScale.z);
		selectable.runtimeCurrentColor = Engine::Color4::Lerp(
			selectable.runtimeStartColor, targetColor, EasedValue(style.colorEasing, colorProgress));
		selectable.runtimeCurrentScale = Engine::Vector3::Lerp(
			selectable.runtimeStartScale, targetScale, EasedValue(style.scaleEasing, scaleProgress));

		if (auto* parameters = ResolveMaterialParameters(world, entry.entity)) {
			(*parameters)["color"].value = selectable.runtimeCurrentColor;
			if (style.overrideTexture) {
				(*parameters)["baseColorTexture"].value = style.texture;
			} else if (selectable.runtimeHadBaseTexture) {
				(*parameters)["baseColorTexture"].value = selectable.runtimeBaseTexture;
			} else {
				parameters->erase("baseColorTexture");
			}
		}
		if (auto* transform = world.TryGetComponent<Engine::TransformComponent>(entry.entity)) {
			if (transform->localScale != selectable.runtimeCurrentScale) {
				transform->localScale = selectable.runtimeCurrentScale;
				transform->isDirty = true;
			}
		}
	}

	bool IsSubmitTransitionFinished(const Engine::UISelectableComponent& selectable,
		const Engine::UISelectableAnimationRuntime* animationRuntime) {

		if (selectable.submitted.useAnimationClip) {
			return animationRuntime && animationRuntime->stateInitialized &&
				animationRuntime->state == GetStyleIndex(Engine::UISelectableState::Submitted) &&
				!animationRuntime->playing;
		}
		return selectable.submitted.colorTransitionDuration <= selectable.runtimeColorTransitionElapsed &&
			selectable.submitted.scaleTransitionDuration <= selectable.runtimeScaleTransitionElapsed;
	}

	void ClickButton(Engine::ECSWorld& world, Engine::Entity entity) {

		if (auto* button = world.TryGetComponent<Engine::UIImageButtonComponent>(entity); button && button->enabled) {
			button->runtimeClickedThisFrame = true;
		}
		if (auto* button = world.TryGetComponent<Engine::UITextButtonComponent>(entity); button && button->enabled) {
			button->runtimeClickedThisFrame = true;
		}
	}
}

//============================================================================
//	UIInputSystem classMethods
//============================================================================
void Engine::UIInputSystem::Update(ECSWorld& world, SystemContext& context) {

	UIRuntimeService& runtimeService = UIRuntimeService::GetInstance();
	runtimeService.SetGameplayInputBlocked(false);
	world.ForEach<UIImageButtonComponent>([](Entity, UIImageButtonComponent& button) {
		button.runtimeClickedThisFrame = false;
		});
	world.ForEach<UITextButtonComponent>([](Entity, UITextButtonComponent& button) {
		button.runtimeClickedThisFrame = false;
		});
	world.ForEach<UISelectableComponent>([](Entity, UISelectableComponent& selectable) {
		selectable.runtimeSubmittedThisFrame = false;
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
		if (!selectable) {
			continue;
		}
		SelectableEntry entry{};
		entry.entity = element.entity;
		entry.canvas = element.canvas;
		entry.selectable = selectable;
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
	world.ForEach<UISelectableComponent>([&](Entity entity, UISelectableComponent& selectable) {

		if (!activeSelectableEntities.contains(world.GetUUID(entity))) {
			RestoreSelectableVisual(world, entity, selectable);
		}
		});

	// 入力を無効にしたCanvasの選択状態を破棄
	world.ForEach<CanvasComponent>([isPlay](Entity, CanvasComponent& canvas) {

		if (!canvas.enabled || (!isPlay && !canvas.inputInEditMode)) {
			ResetCanvasInputRuntime(canvas);
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
		if (!canvas.blockInputAfterSubmit) {
			canvas.runtimeInputLocked = false;
		}
		SelectableEntry* selected = FindEntryByLocalFileID(world, entries,
			canvasEntity, canvas.runtimeSelectedLocalFileID);
		if (canvas.navigationMode == CanvasNavigationMode::TransitionTable) {
			if (!selected || !selected->selectable->interactable ||
				!IsTransitionTableEntry(world, canvas, *selected)) {
				selected = FindEntryByLocalFileID(world, entries,
					canvasEntity, canvas.firstSelectedLocalFileID);
			}
			if (!selected || !selected->selectable->interactable ||
				!IsTransitionTableEntry(world, canvas, *selected)) {
				selected = FindFirstTransitionTableEntry(world, entries, canvasEntity, canvas);
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
		canvas.runtimeSelectedLocalFileID = selected ? GetLocalFileID(world, selected->entity) : UUID{};
	}

	Input* input = Input::GetInstance();
	bool consumedInput = false;
	Entity activeCanvas = Entity::Null();
	for (Entity candidate : canvases) {
		const auto& canvas = world.GetComponent<CanvasComponent>(candidate);
		if (!canvas.runtimeSelectedLocalFileID) {
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
		!world.GetComponent<CanvasComponent>(activeCanvas).runtimeInputLocked) {

		auto& canvas = world.GetComponent<CanvasComponent>(activeCanvas);
		Vector2 heldDirection = ReadTriggeredNavigationDirection(*input);
		if (heldDirection == Vector2{}) {
			heldDirection = ReadHeldNavigationDirection(*input, canvas.stickThreshold);
		}
		if (heldDirection != Vector2{}) {
			if (heldDirection != canvas.runtimeRepeatDirection) {
				canvas.runtimeRepeatDirection = heldDirection;
				canvas.runtimeRepeatElapsed = 0.0f;
				canvas.runtimeRepeatStarted = false;
				direction = heldDirection;
				navigationTriggered = true;
			} else {
				canvas.runtimeRepeatElapsed += context.unscaledDeltaTime;
				const float threshold = canvas.runtimeRepeatStarted ?
					(std::max)(canvas.repeatInterval, 0.01f) : (std::max)(canvas.repeatDelay, 0.0f);
				if (threshold <= canvas.runtimeRepeatElapsed) {
					canvas.runtimeRepeatElapsed = 0.0f;
					canvas.runtimeRepeatStarted = true;
					direction = heldDirection;
					navigationTriggered = true;
				}
			}
		} else {
			canvas.runtimeRepeatDirection = {};
			canvas.runtimeRepeatElapsed = 0.0f;
			canvas.runtimeRepeatStarted = false;
		}
	}

	if (navigationTriggered && world.IsAlive(activeCanvas)) {

		auto& canvas = world.GetComponent<CanvasComponent>(activeCanvas);
		SelectableEntry* current = FindEntryByLocalFileID(world, entries,
			activeCanvas, canvas.runtimeSelectedLocalFileID);
		SelectableEntry* next = nullptr;
		if (current) {
			if (canvas.navigationMode == CanvasNavigationMode::TransitionTable) {
				next = FindTransitionTableNavigation(world, entries,
					activeCanvas, canvas, *current, direction);
			} else {
				next = FindAutomaticNavigation(entries, *current, direction, canvas.wrapNavigation);
			}
		}
		if (next && next->selectable->interactable) {
			canvas.runtimeSelectedLocalFileID = GetLocalFileID(world, next->entity);
			consumedInput = true;
		}
	}

	if (input && world.IsAlive(activeCanvas)) {

		auto& canvas = world.GetComponent<CanvasComponent>(activeCanvas);
		if (!canvas.runtimeInputLocked) {
			if (SelectableEntry* selected = FindEntryByLocalFileID(world, entries,
				activeCanvas, canvas.runtimeSelectedLocalFileID);
				selected && IsSubmitTriggered(*input, *selected->selectable)) {

				selected->selectable->runtimeSubmitted = true;
				selected->selectable->runtimeSubmittedThisFrame = true;
				selected->selectable->runtimePreviousState = UISelectableState::Selected;
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
					canvas.runtimeInputLocked = true;
					canvas.runtimeRepeatDirection = {};
					canvas.runtimeRepeatElapsed = 0.0f;
					canvas.runtimeRepeatStarted = false;
				}
			}
		}
	}

	// 入力状態から表示状態を決定して遷移を進める
	for (SelectableEntry& entry : entries) {

		auto& canvas = world.GetComponent<CanvasComponent>(entry.canvas);
		const UUID localFileID = GetLocalFileID(world, entry.entity);
		if (entry.selectable->runtimeSubmitted && canvas.blockInputAfterSubmit) {
			canvas.runtimeInputLocked = true;
		}
		if (entry.selectable->runtimeSubmitted && canvas.runtimeInputLocked) {
			entry.selectable->runtimeState = UISelectableState::Submitted;
		} else if (!entry.selectable->interactable) {
			entry.selectable->runtimeSubmitted = false;
			entry.selectable->runtimeState = UISelectableState::Disabled;
		} else if (entry.selectable->runtimeSubmitted) {
			entry.selectable->runtimeState = UISelectableState::Submitted;
		} else if (canvas.runtimeSelectedLocalFileID == localFileID) {
			entry.selectable->runtimeState = UISelectableState::Selected;
		} else {
			entry.selectable->runtimeState = UISelectableState::Normal;
		}

		UISelectableAnimationRuntime* animationRuntime = nullptr;
		auto runtime = animationRuntimes_.find(world.GetUUID(entry.entity));
		if (runtime != animationRuntimes_.end() || HasStateTransitionRuntime(*entry.selectable)) {
			if (runtime == animationRuntimes_.end()) {
				runtime = animationRuntimes_.try_emplace(world.GetUUID(entry.entity)).first;
			}
			animationRuntime = &runtime->second;
			UpdateSelectableAnimation(world, entry.entity, *entry.selectable,
				context, runtime->second, context.unscaledDeltaTime);
		}
		UpdateSelectableVisual(world, entry, context.unscaledDeltaTime);

		if (entry.selectable->runtimeSubmitted && !canvas.runtimeInputLocked &&
			IsSubmitTransitionFinished(*entry.selectable, animationRuntime)) {
			entry.selectable->runtimeSubmitted = false;
		}
	}

	if (isPlay && world.IsAlive(activeCanvas)) {
		const auto& canvas = world.GetComponent<CanvasComponent>(activeCanvas);
		runtimeService.SetGameplayInputBlocked(canvas.blockGameplayInput && consumedInput);
	}
}

void Engine::UIInputSystem::OnWorldExit(ECSWorld& world, [[maybe_unused]] SystemContext& context) {

	for (const auto& [uuid, runtime] : animationRuntimes_) {

		const Entity entity = world.FindByUUID(uuid);
		if (world.IsAlive(entity) && runtime.applied) {
			RestoreAnimationBaseValues(world, entity, runtime.baseValues);
		}
	}
	animationRuntimes_.clear();
	world.ForEach<UISelectableComponent>([&](Entity entity, UISelectableComponent& selectable) {
		RestoreSelectableVisual(world, entity, selectable);
		});
	world.ForEach<CanvasComponent>([](Entity, CanvasComponent& canvas) {
		ResetCanvasInputRuntime(canvas);
		});
	UIRuntimeService::GetInstance().Clear(world);
}
