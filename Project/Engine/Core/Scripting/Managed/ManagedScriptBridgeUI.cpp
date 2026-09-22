#include "ManagedScriptRuntime.h"
#include "ManagedScriptUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/UI/CanvasComponent.h>
#include <Engine/Core/World/Components/UI/UISelectableComponent.h>
#include <Engine/Core/World/Components/UI/UIProgressComponent.h>
#include <Engine/Core/World/Components/UI/UIImageButtonComponent.h>
#include <Engine/Core/World/Components/UI/UITextButtonComponent.h>
#include <Engine/Core/World/UI/UIRuntimeService.h>

// c++
#include <algorithm>
#include <span>
#include <vector>

namespace Engine {

	namespace {

		bool IsCanvasBindingCategoryValid(int32_t action, int32_t device) {

			return 0 <= action &&
				action <= static_cast<int32_t>(CanvasInputAction::Submit) &&
				0 <= device &&
				device <= static_cast<int32_t>(CanvasInputDevice::Gamepad);
		}
	}

	int32_t ManagedScriptRuntime::GetUISelectableRuntimeStateCallback(
		ManagedNativeEntity entity,
		ManagedUISelectableRuntimeState* outState) {

		if (!outState) {
			return 0;
		}
		*outState = {};

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		const UISelectableRuntimeComponent* runtime =
			world && world->IsAlive(resolved) ?
			world->TryGetComponent<UISelectableRuntimeComponent>(resolved) :
			nullptr;
		if (!runtime) {
			return 0;
		}

		outState->state = static_cast<int32_t>(runtime->state);
		outState->normalThisFrame = runtime->normalThisFrame ? 1 : 0;
		outState->selectedThisFrame = runtime->selectedThisFrame ? 1 : 0;
		outState->submittedThisFrame =
			runtime->submittedThisFrame ? 1 : 0;
		outState->disabledThisFrame =
			runtime->disabledThisFrame ? 1 : 0;
		return 1;
	}

	int32_t ManagedScriptRuntime::GetUIProgressRuntimeStateCallback(
		ManagedNativeEntity entity,
		ManagedUIProgressRuntimeState* outState) {

		if (!outState) {
			return 0;
		}
		*outState = {};

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		const UIProgressRuntimeData* runtime =
			world && world->IsAlive(resolved) ?
			TryGetUIProgressRuntime(*world, resolved) : nullptr;
		if (!runtime) {
			return 0;
		}

		// 外部Runtimeストレージから固定長スナップショットだけを渡す
		outState->displayedValue = runtime->displayedValue;
		outState->delayedValue = runtime->delayedValue;
		outState->initialized = runtime->initialized ? 1 : 0;
		return 1;
	}

	int32_t ManagedScriptRuntime::GetCanvasInputLockedCallback(
		ManagedNativeEntity entity) {

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		const CanvasRuntimeComponent* runtime =
			world && world->IsAlive(resolved) ?
			world->TryGetComponent<CanvasRuntimeComponent>(resolved) :
			nullptr;
		return runtime && runtime->inputLocked ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::GetUIButtonClickedCallback(
		ManagedNativeEntity entity, int32_t buttonType) {

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world || !world->IsAlive(resolved)) {
			return 0;
		}
		if (buttonType == 0) {
			const UIImageButtonRuntimeComponent* runtime =
				world->TryGetComponent<UIImageButtonRuntimeComponent>(resolved);
			return runtime && runtime->clickedThisFrame ? 1 : 0;
		}
		if (buttonType == 1) {
			const UITextButtonRuntimeComponent* runtime =
				world->TryGetComponent<UITextButtonRuntimeComponent>(resolved);
			return runtime && runtime->clickedThisFrame ? 1 : 0;
		}
		return 0;
	}

	int32_t ManagedScriptRuntime::CanvasCopyInputBindingsCallback(
		ManagedNativeEntity entity, int32_t action, int32_t device,
		int32_t* bindings, int32_t capacity) {

		ECSWorld* world = ResolveWorld(entity);
		if (!world) {
			return 0;
		}
		const Entity resolved = ResolveEntity(entity);
		if (!world->IsAlive(resolved) ||
			!world->HasComponent<CanvasComponent>(resolved) ||
			!IsCanvasBindingCategoryValid(action, device)) {
			return 0;
		}

		const CanvasInputAction targetAction =
			static_cast<CanvasInputAction>(action);
		const CanvasInputDevice targetDevice =
			static_cast<CanvasInputDevice>(device);
		const std::span<const CanvasInputBinding> stored =
			GetCanvasInputBindings(*world, resolved);
		int32_t count = 0;
		for (const CanvasInputBinding& binding : stored) {
			if (binding.action == targetAction &&
				binding.device == targetDevice) {
				if (bindings && count < capacity) {
					bindings[count] = binding.code;
				}
				++count;
			}
		}
		return count;
	}

	void ManagedScriptRuntime::CanvasSetInputBindingsCallback(
		ManagedNativeEntity entity, int32_t action, int32_t device,
		const int32_t* bindings, int32_t count) {

		ECSWorld* world = ResolveWorld(entity);
		if (!world || !IsCanvasBindingCategoryValid(action, device) ||
			count < 0) {
			return;
		}
		const Entity resolved = ResolveEntity(entity);
		if (!world->IsAlive(resolved) ||
			!world->HasComponent<CanvasComponent>(resolved)) {
			return;
		}

		const CanvasInputAction targetAction =
			static_cast<CanvasInputAction>(action);
		const CanvasInputDevice targetDevice =
			static_cast<CanvasInputDevice>(device);
		const int32_t minCode =
			targetDevice == CanvasInputDevice::Keyboard ? 1 : 0;
		const int32_t maxCode =
			targetDevice == CanvasInputDevice::Keyboard ?
			255 : static_cast<int32_t>(GamePadButtons::Counts) - 1;

		std::vector<CanvasInputBinding> replaced;
		const std::span<const CanvasInputBinding> stored =
			GetCanvasInputBindings(*world, resolved);
		replaced.reserve(stored.size() + static_cast<size_t>(count));
		for (const CanvasInputBinding& binding : stored) {
			if (binding.action != targetAction ||
				binding.device != targetDevice) {
				replaced.emplace_back(binding);
			}
		}
		for (int32_t i = 0; bindings && i < count; ++i) {

			const int32_t code = bindings[i];
			if (code < minCode || maxCode < code) {
				continue;
			}
			const bool duplicated = std::any_of(
				replaced.begin(), replaced.end(),
				[&](const CanvasInputBinding& binding) {
					return binding.code == code &&
						binding.action == targetAction &&
						binding.device == targetDevice;
				});
			if (!duplicated) {
				replaced.emplace_back(CanvasInputBinding{
					static_cast<uint16_t>(code), targetAction, targetDevice
					});
			}
		}
		SetCanvasInputBindings(*world, resolved, replaced);
	}

	int32_t ManagedScriptRuntime::CanvasGetNavigationTableSizeCallback(
		ManagedNativeEntity entity, int32_t* outRows, int32_t* outColumns) {

		if (!outRows || !outColumns) {
			return static_cast<int32_t>(CanvasNavigationTableResult::InvalidSize);
		}
		*outRows = 0;
		*outColumns = 0;

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world || !world->IsAlive(resolved) ||
			!world->HasComponent<CanvasComponent>(resolved)) {
			return static_cast<int32_t>(CanvasNavigationTableResult::InvalidCanvas);
		}
		const auto& canvas = world->GetComponent<CanvasComponent>(resolved);
		*outRows = canvas.navigationRows;
		*outColumns = canvas.navigationColumns;
		return static_cast<int32_t>(CanvasNavigationTableResult::Success);
	}

	int32_t ManagedScriptRuntime::CanvasResizeNavigationTableCallback(
		ManagedNativeEntity entity, int32_t rows, int32_t columns) {

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			return static_cast<int32_t>(CanvasNavigationTableResult::InvalidCanvas);
		}
		return static_cast<int32_t>(
			ResizeCanvasNavigationTable(*world, resolved, rows, columns));
	}

	int32_t ManagedScriptRuntime::CanvasGetNavigationCellCallback(
		ManagedNativeEntity entity, int32_t row, int32_t column,
		ManagedNativeEntity* outTarget) {

		if (!outTarget) {
			return static_cast<int32_t>(CanvasNavigationTableResult::InvalidTarget);
		}
		*outTarget = MakeNullNativeEntity();

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			return static_cast<int32_t>(CanvasNavigationTableResult::InvalidCanvas);
		}

		Entity target = Entity::Null();
		const CanvasNavigationTableResult result =
			GetCanvasNavigationCell(*world, resolved, row, column, target);
		if (result == CanvasNavigationTableResult::Success && world->IsAlive(target)) {
			*outTarget = MakeNativeEntity(*world, target);
		}
		return static_cast<int32_t>(result);
	}

	int32_t ManagedScriptRuntime::CanvasSetNavigationCellCallback(
		ManagedNativeEntity entity, int32_t row, int32_t column,
		ManagedNativeEntity target) {

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			return static_cast<int32_t>(CanvasNavigationTableResult::InvalidCanvas);
		}

		Entity resolvedTarget = Entity::Null();
		if (target.IsValid()) {
			ECSWorld* targetWorld = ResolveWorld(target);
			resolvedTarget = ResolveEntity(target);
			if (targetWorld != world || !world->IsAlive(resolvedTarget)) {
				return static_cast<int32_t>(CanvasNavigationTableResult::InvalidTarget);
			}
		}
		return static_cast<int32_t>(
			SetCanvasNavigationCell(*world, resolved, row, column, resolvedTarget));
	}

	int32_t ManagedScriptRuntime::CanvasScreenToLocalPointCallback(
		ManagedNativeEntity entity, ManagedVector2 screenPosition,
		ManagedVector2* outLocalPosition) {

		if (!outLocalPosition) {
			return 0;
		}

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world || !world->IsAlive(resolved) ||
			!world->HasComponent<CanvasComponent>(resolved)) {
			return 0;
		}

		Vector2 localPosition{};
		if (!UIRuntimeService::GetInstance().TryScreenToLocalPoint(
			*world, resolved, Vector2(screenPosition.x, screenPosition.y), localPosition)) {
			return 0;
		}
		*outLocalPosition = ToManagedVector2(localPosition);
		return 1;
	}
}
