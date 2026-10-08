#include "UIInputSystem.h"

//============================================================================
//	include
//============================================================================
#include "UIElementSelection.h"
#include "UINavigationInput.h"
#include "UISelectableStyle.h"
#include "UISelectableAnimation.h"
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
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <variant>

using namespace Engine::UIElementSelection;
using namespace Engine::UINavigationInput;
using namespace Engine::UISelectableStyle;
using namespace Engine::UISelectableAnimation;

//============================================================================
//	UIInputSystem internal
//============================================================================
namespace {

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
	runtimeService.ClearGameplayInputBlocks();
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
	if (isPlay) {
		world.ForEach<CanvasComponent>([&](Entity entity, CanvasComponent& canvas) {
			const auto* sceneObject = world.TryGetComponent<SceneObjectComponent>(entity);
			if (canvas.enabled && (!sceneObject || sceneObject->activeInHierarchy) &&
				canvas.inputBlockMode == CanvasInputBlockMode::WhileVisible) {
				runtimeService.SetGameplayInputBlocked(canvas.playerIndex, true);
			}
			});
	}
	if (runtimeService.GetElements(world).empty()) {
		runtimeService.Build(world, EngineContext::GetWindowSetting().gameSizeFloat);
	}

	std::vector<UISelectableEntry> entries;
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
		UISelectableEntry entry{};
		entry.entity = element.entity;
		entry.canvas = element.canvas;
		entry.selectable = selectable;
		entry.runtime = selectableRuntime;
		entry.element = &element;
		entry.center = ResolveElementCenter(world, entry.entity, *entry.element);
		entries.emplace_back(entry);
		activeSelectableEntities.emplace(world.GetUUID(entry.entity));
	}

	visuals_.DiscardInactive(world, activeSelectableEntities);

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
	for (const UISelectableEntry& entry : entries) {
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
		UISelectableEntry* selected = FindEntryByLocalFileID(world, entries,
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
	std::array<bool, Input::kMaxPlayers> consumedInput{};
	std::array<Entity, Input::kMaxPlayers> activeCanvases{};
	for (Entity candidate : canvases) {
		const auto& canvas = world.GetComponent<CanvasComponent>(candidate);
		if (Input::kMaxPlayers <= canvas.playerIndex) {
			continue;
		}
		const auto& canvasRuntime =
			world.GetComponent<CanvasRuntimeComponent>(candidate);
		if (!canvasRuntime.selectedLocalFileID) {
			continue;
		}
		Entity& activeCanvas = activeCanvases[canvas.playerIndex];
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

	// マウス位置にある最前面UIを選択する
	UISelectableEntry* pointerEntry = nullptr;
	if (input) {
		const std::optional<Vector2> mousePosition = input->GetMousePosInView(InputViewArea::Game);
		if (mousePosition) {
			pointerEntry = FindPointerEntry(world, entries, *mousePosition);
		}
	}
	if (pointerEntry) {
		const auto& canvas = world.GetComponent<CanvasComponent>(pointerEntry->canvas);
		if (Input::kMaxPlayers <= canvas.playerIndex ||
			!input->IsPlayerKeyboardMouseEnabled(canvas.playerIndex)) {
			pointerEntry = nullptr;
		}
	}
	if (pointerEntry) {
		const auto& canvas = world.GetComponent<CanvasComponent>(pointerEntry->canvas);
		Entity& activeCanvas = activeCanvases[canvas.playerIndex];
		activeCanvas = pointerEntry->canvas;
		auto& canvasRuntime = world.GetComponent<CanvasRuntimeComponent>(activeCanvas);
		const UUID pointedLocalFileID = GetLocalFileID(world, pointerEntry->entity);
		if (canvasRuntime.selectedLocalFileID != pointedLocalFileID) {
			canvasRuntime.selectedLocalFileID = pointedLocalFileID;
			consumedInput[canvas.playerIndex] = true;
		}
	}

	for (uint32_t playerIndex = 0; playerIndex < Input::kMaxPlayers; ++playerIndex) {
		Entity activeCanvas = activeCanvases[playerIndex];
		Vector2 direction{};
		bool navigationTriggered = false;
		if (!input || !input->IsGameplayInputAvailable(playerIndex) ||
			!world.IsAlive(activeCanvas)) {
			continue;
		}
		if (!world.GetComponent<CanvasRuntimeComponent>(activeCanvas).inputLocked) {

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
						(std::max)(canvas.repeatInterval, 0.01f) :
						(std::max)(canvas.repeatDelay, 0.0f);
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

		if (navigationTriggered) {

			auto& canvas = world.GetComponent<CanvasComponent>(activeCanvas);
			auto& canvasRuntime =
				world.GetComponent<CanvasRuntimeComponent>(activeCanvas);
			UISelectableEntry* current = FindEntryByLocalFileID(world, entries,
				activeCanvas, canvasRuntime.selectedLocalFileID);
			UISelectableEntry* next = nullptr;
			if (current) {
				if (canvas.navigationMode == CanvasNavigationMode::TransitionTable) {
					next = FindTransitionTableNavigation(world, entries,
						activeCanvas, canvas, *current, direction,
						GetCanvasNavigationCells(world, activeCanvas));
				} else {
					next = FindAutomaticNavigation(
						entries, *current, direction, canvas.wrapNavigation);
				}
			}
			if (next && next->selectable->interactable) {
				canvasRuntime.selectedLocalFileID =
					GetLocalFileID(world, next->entity);
				consumedInput[playerIndex] = true;
			}
		}

		auto& canvas = world.GetComponent<CanvasComponent>(activeCanvas);
		auto& canvasRuntime =
			world.GetComponent<CanvasRuntimeComponent>(activeCanvas);
		if (!canvasRuntime.inputLocked) {
			UISelectableEntry* selected = FindEntryByLocalFileID(world, entries,
				activeCanvas, canvasRuntime.selectedLocalFileID);
			const bool pointerSubmitted = pointerEntry == selected &&
				input->IsPlayerKeyboardMouseEnabled(playerIndex) && input->TriggerMouseLeft();
			if (selected && (pointerSubmitted || IsSubmitTriggered(
				*input, canvas, GetCanvasInputBindings(world, activeCanvas)))) {

				selected->runtime->submitted = true;
				selected->runtime->submittedThisFrame = true;
				selected->runtime->previousState =
					UISelectableState::Selected;
				visuals_.ResetSubmitState(world.GetUUID(selected->entity));
				PlayStateSound(world, selected->entity, selected->selectable->submitted, context);
				ClickButton(world, selected->entity);
				consumedInput[playerIndex] = true;

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
	for (UISelectableEntry& entry : entries) {

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

		UISelectableAnimationRuntime* animationRuntime = visuals_.UpdateAnimation(world, entry, context);
		UpdateSelectableVisual(world, entry, context.unscaledDeltaTime);

		if (selectableRuntime.submitted && !canvasRuntime.inputLocked &&
			IsSubmitTransitionFinished(
				*entry.selectable, selectableRuntime, animationRuntime)) {
			selectableRuntime.submitted = false;
		}
	}

	if (isPlay) {
		for (uint32_t playerIndex = 0; playerIndex < Input::kMaxPlayers; ++playerIndex) {
			const Entity activeCanvas = activeCanvases[playerIndex];
			if (!world.IsAlive(activeCanvas)) {
				continue;
			}
			const auto& canvas = world.GetComponent<CanvasComponent>(activeCanvas);
			if (canvas.inputBlockMode == CanvasInputBlockMode::ConsumedFrame &&
				consumedInput[playerIndex]) {
				runtimeService.SetGameplayInputBlocked(playerIndex, true);
			}
		}
	}
}

void Engine::UIInputSystem::OnWorldExit(ECSWorld& world, [[maybe_unused]] SystemContext& context) {

	RestoreEditModeVisuals(world);
	UIRuntimeService::GetInstance().Clear(world);
}

void Engine::UIInputSystem::RestoreEditModeVisuals(ECSWorld& world) {

	visuals_.RestoreAll(world);
	world.ForEach<UISelectableRuntimeComponent>(
		[&](Entity entity, UISelectableRuntimeComponent& runtime) {
		RestoreSelectableVisual(world, entity, runtime);
		});
	world.ForEach<CanvasRuntimeComponent>(
		[](Entity, CanvasRuntimeComponent& runtime) {
			ResetCanvasInputRuntime(runtime);
			});
	UIRuntimeService::GetInstance().ClearGameplayInputBlocks();
}
