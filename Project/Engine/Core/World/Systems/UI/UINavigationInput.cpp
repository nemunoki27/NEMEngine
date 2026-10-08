#include "UINavigationInput.h"

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

namespace Engine::UINavigationInput {

	bool IsBindingTriggered(Engine::Input& input, std::span<const Engine::CanvasInputBinding> bindings,
		Engine::CanvasInputAction action, Engine::CanvasInputDevice device, uint32_t playerIndex) {

		for (const Engine::CanvasInputBinding& binding : bindings) {
			if (binding.action != action || binding.device != device) {
				continue;
			}
			const bool triggered = device == Engine::CanvasInputDevice::Keyboard ?
				(input.IsPlayerKeyboardMouseEnabled(playerIndex) &&
					input.TriggerKey(static_cast<BYTE>(binding.code))) :
				input.GamepadButtonDownByIndex(input.GetPlayerGamepadIndex(playerIndex), binding.code);
			if (triggered) {
				return true;
			}
		}
		return false;
	}

	bool IsBindingHeld(Engine::Input& input, std::span<const Engine::CanvasInputBinding> bindings,
		Engine::CanvasInputAction action, Engine::CanvasInputDevice device, uint32_t playerIndex) {

		for (const Engine::CanvasInputBinding& binding : bindings) {
			if (binding.action != action || binding.device != device) {
				continue;
			}
			const bool held = device == Engine::CanvasInputDevice::Keyboard ?
				(input.IsPlayerKeyboardMouseEnabled(playerIndex) &&
					input.PushKey(static_cast<BYTE>(binding.code))) :
				input.GamepadButtonByIndex(input.GetPlayerGamepadIndex(playerIndex), binding.code);
			if (held) {
				return true;
			}
		}
		return false;
	}

	Engine::Vector2 ReadTriggeredNavigationDirection(Engine::Input& input, const Engine::CanvasComponent& canvas,
		std::span<const Engine::CanvasInputBinding> bindings) {

		if ((canvas.keyboardInputEnabled &&
			IsBindingTriggered(input, bindings,
				Engine::CanvasInputAction::Up, Engine::CanvasInputDevice::Keyboard, canvas.playerIndex)) ||
			(canvas.gamepadInputEnabled &&
				IsBindingTriggered(input, bindings,
					Engine::CanvasInputAction::Up, Engine::CanvasInputDevice::Gamepad, canvas.playerIndex))) {
			return Engine::Vector2(0.0f, -1.0f);
		}
		if ((canvas.keyboardInputEnabled &&
			IsBindingTriggered(input, bindings,
				Engine::CanvasInputAction::Down, Engine::CanvasInputDevice::Keyboard, canvas.playerIndex)) ||
			(canvas.gamepadInputEnabled &&
				IsBindingTriggered(input, bindings,
					Engine::CanvasInputAction::Down, Engine::CanvasInputDevice::Gamepad, canvas.playerIndex))) {
			return Engine::Vector2(0.0f, 1.0f);
		}
		if ((canvas.keyboardInputEnabled &&
			IsBindingTriggered(input, bindings,
				Engine::CanvasInputAction::Left, Engine::CanvasInputDevice::Keyboard, canvas.playerIndex)) ||
			(canvas.gamepadInputEnabled &&
				IsBindingTriggered(input, bindings,
					Engine::CanvasInputAction::Left, Engine::CanvasInputDevice::Gamepad, canvas.playerIndex))) {
			return Engine::Vector2(-1.0f, 0.0f);
		}
		if ((canvas.keyboardInputEnabled &&
			IsBindingTriggered(input, bindings,
				Engine::CanvasInputAction::Right, Engine::CanvasInputDevice::Keyboard, canvas.playerIndex)) ||
			(canvas.gamepadInputEnabled &&
				IsBindingTriggered(input, bindings,
					Engine::CanvasInputAction::Right, Engine::CanvasInputDevice::Gamepad, canvas.playerIndex))) {
			return Engine::Vector2(1.0f, 0.0f);
		}
		return {};
	}

	Engine::Vector2 ReadHeldNavigationDirection(Engine::Input& input, const Engine::CanvasComponent& canvas,
		std::span<const Engine::CanvasInputBinding> bindings) {

		if ((canvas.keyboardInputEnabled &&
			IsBindingHeld(input, bindings,
				Engine::CanvasInputAction::Up, Engine::CanvasInputDevice::Keyboard, canvas.playerIndex)) ||
			(canvas.gamepadInputEnabled &&
				IsBindingHeld(input, bindings,
					Engine::CanvasInputAction::Up, Engine::CanvasInputDevice::Gamepad, canvas.playerIndex))) {
			return Engine::Vector2(0.0f, -1.0f);
		}
		if ((canvas.keyboardInputEnabled &&
			IsBindingHeld(input, bindings,
				Engine::CanvasInputAction::Down, Engine::CanvasInputDevice::Keyboard, canvas.playerIndex)) ||
			(canvas.gamepadInputEnabled &&
				IsBindingHeld(input, bindings,
					Engine::CanvasInputAction::Down, Engine::CanvasInputDevice::Gamepad, canvas.playerIndex))) {
			return Engine::Vector2(0.0f, 1.0f);
		}
		if ((canvas.keyboardInputEnabled &&
			IsBindingHeld(input, bindings,
				Engine::CanvasInputAction::Left, Engine::CanvasInputDevice::Keyboard, canvas.playerIndex)) ||
			(canvas.gamepadInputEnabled &&
				IsBindingHeld(input, bindings,
					Engine::CanvasInputAction::Left, Engine::CanvasInputDevice::Gamepad, canvas.playerIndex))) {
			return Engine::Vector2(-1.0f, 0.0f);
		}
		if ((canvas.keyboardInputEnabled &&
			IsBindingHeld(input, bindings,
				Engine::CanvasInputAction::Right, Engine::CanvasInputDevice::Keyboard, canvas.playerIndex)) ||
			(canvas.gamepadInputEnabled &&
				IsBindingHeld(input, bindings,
					Engine::CanvasInputAction::Right, Engine::CanvasInputDevice::Gamepad, canvas.playerIndex))) {
			return Engine::Vector2(1.0f, 0.0f);
		}

		const int32_t gamepadIndex = input.GetPlayerGamepadIndex(canvas.playerIndex);
		const Engine::Vector2 stick = canvas.gamepadInputEnabled &&
			canvas.gamepadLeftStickEnabled ? Engine::Vector2(
				input.GamepadAxisByIndex(gamepadIndex, 0),
				input.GamepadAxisByIndex(gamepadIndex, 1)) : Engine::Vector2{};
		if (canvas.stickThreshold <= std::abs(stick.x) ||
			canvas.stickThreshold <= std::abs(stick.y)) {
			return std::abs(stick.x) > std::abs(stick.y) ?
				Engine::Vector2(stick.x < 0.0f ? -1.0f : 1.0f, 0.0f) :
					Engine::Vector2(0.0f, stick.y < 0.0f ? 1.0f : -1.0f);
		}
		return {};
	}

	bool IsSubmitTriggered(Engine::Input& input, const Engine::CanvasComponent& canvas,
		std::span<const Engine::CanvasInputBinding> bindings) {

		return (canvas.keyboardInputEnabled &&
			IsBindingTriggered(input, bindings,
				Engine::CanvasInputAction::Submit,
				Engine::CanvasInputDevice::Keyboard, canvas.playerIndex)) ||
			(canvas.gamepadInputEnabled &&
				IsBindingTriggered(input, bindings,
					Engine::CanvasInputAction::Submit,
					Engine::CanvasInputDevice::Gamepad, canvas.playerIndex));
	}

	void ResetCanvasInputRuntime(Engine::CanvasRuntimeComponent& runtime) {

		runtime = {};
	}
}
