#pragma once

//============================================================================
//	include
//============================================================================
#include "UIInputTypes.h"

namespace Engine::UINavigationInput {

	bool IsBindingTriggered(Input& input, std::span<const CanvasInputBinding> bindings,
		CanvasInputAction action, CanvasInputDevice device);

	bool IsBindingHeld(Input& input, std::span<const CanvasInputBinding> bindings,
		CanvasInputAction action, CanvasInputDevice device);

	Vector2 ReadTriggeredNavigationDirection(Input& input, const CanvasComponent& canvas,
		std::span<const CanvasInputBinding> bindings);

	Vector2 ReadHeldNavigationDirection(Input& input, const CanvasComponent& canvas,
		std::span<const CanvasInputBinding> bindings);

	bool IsSubmitTriggered(Input& input, const CanvasComponent& canvas, std::span<const CanvasInputBinding> bindings);

	void ResetCanvasInputRuntime(CanvasRuntimeComponent& runtime);
}
