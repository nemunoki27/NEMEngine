#include "UIInputSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/UI/UIRuntimeService.h>
#include <Engine/Core/World/Components/UI/CanvasComponent.h>
#include <Engine/Core/World/Components/UI/UISelectableComponent.h>
#include <Engine/Core/World/Components/UI/UIButtonComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/Runtime/Context/EngineContext.h>
#include <Engine/Core/Platform/Input/InputSystem.h>

// c++
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <variant>

//============================================================================
//	UIInputSystem internal
//============================================================================
namespace {

	struct SelectableEntry {

		Engine::Entity entity = Engine::Entity::Null();
		Engine::Entity target = Engine::Entity::Null();
		Engine::Entity canvas = Engine::Entity::Null();
		Engine::UISelectableComponent* selectable = nullptr;
		const Engine::UIElementRuntime* element = nullptr;
		const Engine::UIElementRuntime* targetElement = nullptr;
		Engine::Vector2 center{};
		int32_t sortingLayer = 0;
		int32_t sortingOrder = 0;
		uint32_t hierarchyOrder = 0;
	};

	Engine::UUID GetLocalFileID(Engine::ECSWorld& world, Engine::Entity entity) {

		const auto* sceneObject = world.TryGetComponent<Engine::SceneObjectComponent>(entity);
		return sceneObject ? sceneObject->localFileID : Engine::UUID{};
	}

	Engine::Entity ResolveTarget(Engine::ECSWorld& world, Engine::Entity owner, Engine::UUID localFileID) {

		if (!localFileID) {
			return owner;
		}
		const Engine::Entity target = Engine::SceneObjectUtility::FindByLocalFileID(world, localFileID);
		return world.IsAlive(target) ? target : owner;
	}

	void ResolveRendererSort(Engine::ECSWorld& world, Engine::Entity target,
		int32_t& sortingLayer, int32_t& sortingOrder) {

		if (const auto* sprite = world.TryGetComponent<Engine::SpriteRendererComponent>(target)) {
			sortingLayer += sprite->layer;
			sortingOrder += sprite->order;
		} else if (const auto* text = world.TryGetComponent<Engine::TextRendererComponent>(target)) {
			sortingLayer += text->layer;
			sortingOrder += text->order;
		}
	}

	Engine::Vector2 ResolveElementCenter(Engine::ECSWorld& world, Engine::Entity target,
		const Engine::UIElementRuntime& runtime) {

		Engine::Vector2 localCenter{};
		if (const auto* sprite = world.TryGetComponent<Engine::SpriteRendererComponent>(target)) {
			localCenter = Engine::Vector2(
				(0.5f - sprite->pivot.x) * sprite->size.x,
				(0.5f - sprite->pivot.y) * sprite->size.y);
		} else if (const auto* text = world.TryGetComponent<Engine::TextRendererComponent>(target)) {
			localCenter = Engine::Vector2(
				(0.5f - text->pivot.x) * text->runtimeLayout.boundsSize.x,
				(0.5f - text->pivot.y) * text->runtimeLayout.boundsSize.y);
		}
		const Engine::Vector3 center = Engine::Vector3::Transform(
			Engine::Vector3(localCenter.x, localCenter.y, 0.0f), runtime.screenMatrix);
		return Engine::Vector2(center.x, center.y);
	}

	bool IsPointInside(Engine::ECSWorld& world, const SelectableEntry& entry, const Engine::Vector2& point) {

		Engine::Vector2 min{};
		Engine::Vector2 max{};
		const Engine::Matrix4x4* matrix = nullptr;
		if (entry.selectable->useCustomHitArea) {

			const Engine::Vector2 halfSize = entry.selectable->hitAreaSize * 0.5f;
			min = entry.selectable->hitAreaOffset - halfSize;
			max = entry.selectable->hitAreaOffset + halfSize;
			matrix = &entry.element->screenMatrix;
		} else if (const auto* sprite = world.TryGetComponent<Engine::SpriteRendererComponent>(entry.target)) {

			min = Engine::Vector2(-sprite->pivot.x * sprite->size.x, -sprite->pivot.y * sprite->size.y);
			max = Engine::Vector2((1.0f - sprite->pivot.x) * sprite->size.x,
				(1.0f - sprite->pivot.y) * sprite->size.y);
			matrix = &entry.targetElement->screenMatrix;
		} else if (const auto* text = world.TryGetComponent<Engine::TextRendererComponent>(entry.target)) {

			if (!text->runtimeLayout.valid) {
				return false;
			}
			min = Engine::Vector2(-text->pivot.x * text->runtimeLayout.boundsSize.x,
				-text->pivot.y * text->runtimeLayout.boundsSize.y);
			max = Engine::Vector2((1.0f - text->pivot.x) * text->runtimeLayout.boundsSize.x,
				(1.0f - text->pivot.y) * text->runtimeLayout.boundsSize.y);
			matrix = &entry.targetElement->screenMatrix;
		}
		if (!matrix) {
			return false;
		}

		const Engine::Matrix4x4 inverse = Engine::Matrix4x4::Inverse(*matrix);
		const Engine::Vector3 local = Engine::Vector3::Transform(
			Engine::Vector3(point.x, point.y, 0.0f), inverse);
		return min.x <= local.x && local.x <= max.x && min.y <= local.y && local.y <= max.y;
	}

	bool IsDrawnLater(const SelectableEntry& lhs, const SelectableEntry& rhs) {

		if (lhs.sortingLayer != rhs.sortingLayer) {
			return lhs.sortingLayer > rhs.sortingLayer;
		}
		if (lhs.sortingOrder != rhs.sortingOrder) {
			return lhs.sortingOrder > rhs.sortingOrder;
		}
		return lhs.hierarchyOrder > rhs.hierarchyOrder;
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
			if (entry.canvas == canvas && entry.selectable->interactable &&
				entry.selectable->navigationMode != Engine::UINavigationMode::None) {
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
				!candidate.selectable->interactable || candidate.selectable->navigationMode == Engine::UINavigationMode::None) {
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
				!candidate.selectable->interactable || candidate.selectable->navigationMode == Engine::UINavigationMode::None) {
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
		case Engine::UISelectableState::Highlighted: return selectable.highlighted;
		case Engine::UISelectableState::Pressed: return selectable.pressed;
		case Engine::UISelectableState::Selected: return selectable.selected;
		case Engine::UISelectableState::Disabled: return selectable.disabled;
		case Engine::UISelectableState::Normal:
		default: return selectable.normal;
		}
	}

	void RestoreSelectableTarget(Engine::ECSWorld& world, Engine::UISelectableComponent& selectable) {

		if (!selectable.runtimeInitialized || !selectable.runtimeTargetLocalFileID) {
			return;
		}
		const Engine::Entity target = Engine::SceneObjectUtility::FindByLocalFileID(
			world, selectable.runtimeTargetLocalFileID);
		if (!world.IsAlive(target)) {
			return;
		}
		if (auto* parameters = ResolveMaterialParameters(world, target)) {
			(*parameters)["color"].value = selectable.runtimeBaseColor;
			if (selectable.runtimeHadBaseTexture) {
				(*parameters)["baseColorTexture"].value = selectable.runtimeBaseTexture;
			} else {
				parameters->erase("baseColorTexture");
			}
		}
		if (auto* transform = world.TryGetComponent<Engine::TransformComponent>(target)) {
			transform->localScale = selectable.runtimeBaseScale;
			transform->isDirty = true;
		}
	}

	void InitializeSelectableRuntime(Engine::ECSWorld& world, Engine::Entity owner,
		Engine::Entity target, Engine::UISelectableComponent& selectable) {

		const Engine::UUID targetLocalFileID = GetLocalFileID(world, target);
		if (selectable.runtimeInitialized && selectable.runtimeTargetLocalFileID == targetLocalFileID) {
			return;
		}
		RestoreSelectableTarget(world, selectable);

		selectable.runtimeBaseColor = Engine::Color4::White();
		selectable.runtimeBaseTexture = {};
		selectable.runtimeHadBaseTexture = false;
		if (auto* parameters = ResolveMaterialParameters(world, target)) {
			if (const auto colorIt = parameters->find("color"); colorIt != parameters->end()) {
				if (const auto* color = std::get_if<Engine::Color4>(&colorIt->second.value)) {
					selectable.runtimeBaseColor = *color;
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
		if (const auto* transform = world.TryGetComponent<Engine::TransformComponent>(target)) {
			selectable.runtimeBaseScale = transform->localScale;
		}
		selectable.runtimeCurrentColor = selectable.runtimeBaseColor;
		selectable.runtimeStartColor = selectable.runtimeBaseColor;
		selectable.runtimeCurrentScale = selectable.runtimeBaseScale;
		selectable.runtimeStartScale = selectable.runtimeBaseScale;
		selectable.runtimeTargetLocalFileID = targetLocalFileID ? targetLocalFileID : GetLocalFileID(world, owner);
		selectable.runtimePreviousState = selectable.runtimeState;
		selectable.runtimeTransitionElapsed = selectable.transitionDuration;
		selectable.runtimeInitialized = true;
	}

	void UpdateSelectableVisual(Engine::ECSWorld& world, const SelectableEntry& entry, float deltaTime) {

		Engine::UISelectableComponent& selectable = *entry.selectable;
		InitializeSelectableRuntime(world, entry.entity, entry.target, selectable);
		if (!selectable.runtimeInitialized) {
			return;
		}

		if (selectable.runtimePreviousState != selectable.runtimeState) {
			selectable.runtimePreviousState = selectable.runtimeState;
			selectable.runtimeTransitionElapsed = 0.0f;
			selectable.runtimeStartColor = selectable.runtimeCurrentColor;
			selectable.runtimeStartScale = selectable.runtimeCurrentScale;
		}
		const Engine::UITransitionStyle& style = ResolveStyle(selectable);
		selectable.runtimeTransitionElapsed += (std::max)(deltaTime, 0.0f);
		const float progress = selectable.transitionDuration <= 0.0f ? 1.0f :
			std::clamp(selectable.runtimeTransitionElapsed / selectable.transitionDuration, 0.0f, 1.0f);
		const Engine::Color4 targetColor = selectable.runtimeBaseColor * style.color;
		const Engine::Vector3 targetScale(
			selectable.runtimeBaseScale.x * style.scale.x,
			selectable.runtimeBaseScale.y * style.scale.y,
			selectable.runtimeBaseScale.z);
		selectable.runtimeCurrentColor = Engine::Color4::Lerp(selectable.runtimeStartColor, targetColor, progress);
		selectable.runtimeCurrentScale = Engine::Vector3::Lerp(selectable.runtimeStartScale, targetScale, progress);

		if (auto* parameters = ResolveMaterialParameters(world, entry.target)) {
			(*parameters)["color"].value = selectable.runtimeCurrentColor;
			if (style.overrideTexture) {
				(*parameters)["baseColorTexture"].value = style.texture;
			} else if (selectable.runtimeHadBaseTexture) {
				(*parameters)["baseColorTexture"].value = selectable.runtimeBaseTexture;
			} else {
				parameters->erase("baseColorTexture");
			}
		}
		if (auto* transform = world.TryGetComponent<Engine::TransformComponent>(entry.target)) {
			if (transform->localScale != selectable.runtimeCurrentScale) {
				transform->localScale = selectable.runtimeCurrentScale;
				transform->isDirty = true;
			}
		}
	}

	void ClickButton(Engine::ECSWorld& world, Engine::Entity entity) {

		if (auto* button = world.TryGetComponent<Engine::UIButtonComponent>(entity); button && button->enabled) {
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
	world.ForEach<UIButtonComponent>([](Entity, UIButtonComponent& button) {
		button.runtimeClickedThisFrame = false;
		});

	// Edit中は入力でシーンデータを書き換えない
	if (context.mode != WorldMode::Play) {
		return;
	}
	if (runtimeService.GetElements(world).empty()) {
		runtimeService.Build(world, EngineContext::GetWindowSetting().gameSizeFloat);
	}

	std::vector<SelectableEntry> entries;
	for (const UIElementRuntime& element : runtimeService.GetElements(world)) {

		auto* selectable = world.TryGetComponent<UISelectableComponent>(element.entity);
		if (!selectable) {
			continue;
		}
		SelectableEntry entry{};
		entry.entity = element.entity;
		entry.target = ResolveTarget(world, element.entity, selectable->targetLocalFileID);
		entry.canvas = element.canvas;
		entry.selectable = selectable;
		entry.element = &element;
		entry.targetElement = runtimeService.Find(world, entry.target);
		if (!entry.targetElement || entry.targetElement->canvas != entry.canvas) {
			entry.target = entry.entity;
			entry.targetElement = entry.element;
		}
		entry.center = ResolveElementCenter(world, entry.target, *entry.targetElement);
		entry.sortingLayer = element.canvasSortingLayer;
		entry.sortingOrder = element.canvasOrder;
		ResolveRendererSort(world, entry.target, entry.sortingLayer, entry.sortingOrder);
		entry.hierarchyOrder = entry.targetElement->hierarchyOrder;
		entries.emplace_back(entry);
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
		SelectableEntry* selected = FindEntryByLocalFileID(world, entries,
			canvasEntity, canvas.runtimeSelectedLocalFileID);
		if (!selected || !selected->selectable->interactable) {
			selected = FindEntryByLocalFileID(world, entries, canvasEntity, canvas.firstSelectedLocalFileID);
		}
		if (!selected || !selected->selectable->interactable) {
			selected = FindFirstInteractable(entries, canvasEntity);
		}
		canvas.runtimeSelectedLocalFileID = selected ? GetLocalFileID(world, selected->entity) : UUID{};
		canvas.runtimeHoveredLocalFileID = {};
	}

	Input* input = Input::GetInstance();
	SelectableEntry* hovered = nullptr;
	const std::optional<Vector2> mousePosition = input ? input->GetMousePosInView(InputViewArea::Game) : std::nullopt;
	if (mousePosition) {
		for (SelectableEntry& entry : entries) {
			if (!entry.selectable->interactable || !IsPointInside(world, entry, *mousePosition)) {
				continue;
			}
			if (!hovered || IsDrawnLater(entry, *hovered)) {
				hovered = &entry;
			}
		}
	}
	if (hovered) {
		auto& canvas = world.GetComponent<CanvasComponent>(hovered->canvas);
		canvas.runtimeHoveredLocalFileID = GetLocalFileID(world, hovered->entity);
		if (canvas.mouseHoverSelect) {
			canvas.runtimeSelectedLocalFileID = canvas.runtimeHoveredLocalFileID;
		}
	}

	bool consumedInput = false;
	if (input && input->TriggerMouse(MouseButton::Left) && hovered) {
		auto& canvas = world.GetComponent<CanvasComponent>(hovered->canvas);
		canvas.runtimePressedLocalFileID = GetLocalFileID(world, hovered->entity);
		canvas.runtimeSelectedLocalFileID = canvas.runtimePressedLocalFileID;
		consumedInput = true;
	}
	if (input && input->ReleaseMouse(MouseButton::Left)) {
		for (Entity canvasEntity : canvases) {
			auto& canvas = world.GetComponent<CanvasComponent>(canvasEntity);
			if (canvas.runtimePressedLocalFileID && hovered && hovered->canvas == canvasEntity &&
				canvas.runtimePressedLocalFileID == GetLocalFileID(world, hovered->entity)) {
				ClickButton(world, hovered->entity);
				consumedInput = true;
			}
			canvas.runtimePressedLocalFileID = {};
		}
	}

	// ポインター対象か、現在選択を持つ最前面Canvasをナビゲーション対象にする
	Entity activeCanvas = hovered ? hovered->canvas : Entity::Null();
	if (!world.IsAlive(activeCanvas)) {
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
	}

	Vector2 direction{};
	bool navigationTriggered = false;
	if (input && world.IsAlive(activeCanvas)) {

		if (input->TriggerKey(DIK_UP) || input->TriggerKey(DIK_W) ||
			input->TriggerGamepadButton(GamePadButtons::ARROW_UP)) {
			direction = Vector2(0.0f, -1.0f);
			navigationTriggered = true;
		} else if (input->TriggerKey(DIK_DOWN) || input->TriggerKey(DIK_S) ||
			input->TriggerGamepadButton(GamePadButtons::ARROW_DOWN)) {
			direction = Vector2(0.0f, 1.0f);
			navigationTriggered = true;
		} else if (input->TriggerKey(DIK_LEFT) || input->TriggerKey(DIK_A) ||
			input->TriggerGamepadButton(GamePadButtons::ARROW_LEFT)) {
			direction = Vector2(-1.0f, 0.0f);
			navigationTriggered = true;
		} else if (input->TriggerKey(DIK_RIGHT) || input->TriggerKey(DIK_D) ||
			input->TriggerGamepadButton(GamePadButtons::ARROW_RIGHT)) {
			direction = Vector2(1.0f, 0.0f);
			navigationTriggered = true;
		}

		auto& canvas = world.GetComponent<CanvasComponent>(activeCanvas);
		const Vector2 stick = input->GetLeftStickVal();
		Vector2 stickDirection{};
		if (canvas.stickThreshold <= std::abs(stick.x) || canvas.stickThreshold <= std::abs(stick.y)) {
			stickDirection = std::abs(stick.x) > std::abs(stick.y) ?
				Vector2(stick.x < 0.0f ? -1.0f : 1.0f, 0.0f) :
				Vector2(0.0f, stick.y < 0.0f ? -1.0f : 1.0f);
		}
		if (stickDirection != Vector2{}) {
			if (stickDirection != canvas.runtimeRepeatDirection) {
				canvas.runtimeRepeatDirection = stickDirection;
				canvas.runtimeRepeatElapsed = 0.0f;
				canvas.runtimeRepeatStarted = false;
				direction = stickDirection;
				navigationTriggered = true;
			} else {
				canvas.runtimeRepeatElapsed += context.unscaledDeltaTime;
				const float threshold = canvas.runtimeRepeatStarted ? canvas.repeatInterval : canvas.repeatDelay;
				if (threshold <= canvas.runtimeRepeatElapsed) {
					canvas.runtimeRepeatElapsed = 0.0f;
					canvas.runtimeRepeatStarted = true;
					direction = stickDirection;
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
			if (current->selectable->navigationMode == UINavigationMode::Explicit) {
				UUID target{};
				if (direction.x < 0.0f) { target = current->selectable->leftLocalFileID; }
				else if (0.0f < direction.x) { target = current->selectable->rightLocalFileID; }
				else if (direction.y < 0.0f) { target = current->selectable->upLocalFileID; }
				else { target = current->selectable->downLocalFileID; }
				next = FindEntryByLocalFileID(world, entries, activeCanvas, target);
			} else if (current->selectable->navigationMode == UINavigationMode::Automatic) {
				next = FindAutomaticNavigation(entries, *current, direction, canvas.wrapNavigation);
			}
		}
		if (next && next->selectable->interactable) {
			canvas.runtimeSelectedLocalFileID = GetLocalFileID(world, next->entity);
			consumedInput = true;
		}
	}

	if (input && world.IsAlive(activeCanvas) &&
		(input->TriggerKey(DIK_RETURN) || input->TriggerKey(DIK_SPACE) ||
			input->TriggerGamepadButton(GamePadButtons::A))) {

		auto& canvas = world.GetComponent<CanvasComponent>(activeCanvas);
		if (SelectableEntry* selected = FindEntryByLocalFileID(world, entries,
			activeCanvas, canvas.runtimeSelectedLocalFileID)) {
			ClickButton(world, selected->entity);
			consumedInput = true;
		}
	}

	// 入力状態から表示状態を決定して遷移を進める
	for (SelectableEntry& entry : entries) {

		const auto& canvas = world.GetComponent<CanvasComponent>(entry.canvas);
		const UUID localFileID = GetLocalFileID(world, entry.entity);
		if (!entry.selectable->interactable) {
			entry.selectable->runtimeState = UISelectableState::Disabled;
		} else if (canvas.runtimePressedLocalFileID == localFileID &&
			canvas.runtimeHoveredLocalFileID == localFileID) {
			entry.selectable->runtimeState = UISelectableState::Pressed;
		} else if (canvas.runtimeHoveredLocalFileID == localFileID) {
			entry.selectable->runtimeState = UISelectableState::Highlighted;
		} else if (canvas.runtimeSelectedLocalFileID == localFileID) {
			entry.selectable->runtimeState = UISelectableState::Selected;
		} else {
			entry.selectable->runtimeState = UISelectableState::Normal;
		}
		UpdateSelectableVisual(world, entry, context.unscaledDeltaTime);
	}

	if (world.IsAlive(activeCanvas)) {
		const auto& canvas = world.GetComponent<CanvasComponent>(activeCanvas);
		const bool pointerCaptured = canvas.runtimeHoveredLocalFileID || canvas.runtimePressedLocalFileID;
		runtimeService.SetGameplayInputBlocked(canvas.blockGameplayInput && (pointerCaptured || consumedInput));
	}
}

void Engine::UIInputSystem::OnWorldExit(ECSWorld& world, [[maybe_unused]] SystemContext& context) {

	UIRuntimeService::GetInstance().Clear(world);
}
