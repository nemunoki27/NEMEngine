#include "SceneViewCameraController.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Platform/Input/InputSystem.h>

// c++
#include <algorithm>
#include <cmath>

// imgui
#include <imgui.h>

using namespace Engine;
using namespace Engine::SceneViewCameraDefaults;

void Engine::SceneViewCameraController::CancelNavigation() {

	navigationSession_.Cancel();
	navigationActive_ = false;
}

SceneViewCameraInput Engine::SceneViewCameraController::ReadNavigationInput() {

	Input* input = Input::GetInstance();
	SceneViewCameraInput result;
	result.available = input->HasWindowFocus();
	result.mouseDelta = input->GetMouseMoveValue();
	result.wheel = input->GetMouseWheel();
	result.deltaTime = ImGui::GetIO().DeltaTime;
	if (input->PushMouseLeft()) { result.buttons |= SceneViewCameraInput::kLeft; }
	if (input->PushMouseRight()) { result.buttons |= SceneViewCameraInput::kRight; }
	if (input->PushMouseCenter()) { result.buttons |= SceneViewCameraInput::kMiddle; }
	if (input->TriggerMouseLeft()) { result.pressedButtons |= SceneViewCameraInput::kLeft; }
	if (input->TriggerMouseRight()) { result.pressedButtons |= SceneViewCameraInput::kRight; }
	if (input->TriggerMouseCenter()) { result.pressedButtons |= SceneViewCameraInput::kMiddle; }
	if (input->PushKey(DIK_LSHIFT) || input->PushKey(DIK_RSHIFT)) { result.modifiers |= SceneViewCameraInput::kShift; }
	if (input->PushKey(DIK_LCONTROL) || input->PushKey(DIK_RCONTROL)) { result.modifiers |= SceneViewCameraInput::kControl; }
	if (input->PushKey(DIK_LALT) || input->PushKey(DIK_RALT)) { result.modifiers |= SceneViewCameraInput::kAlt; }
	result.movement = Vector3(
		static_cast<float>(input->PushKey(DIK_D)) - static_cast<float>(input->PushKey(DIK_A)),
		static_cast<float>(input->PushKey(DIK_E)) - static_cast<float>(input->PushKey(DIK_Q)),
		static_cast<float>(input->PushKey(DIK_W)) - static_cast<float>(input->PushKey(DIK_S)));
	return result;
}

void Engine::SceneViewCameraController::Update3D(InputViewArea viewArea) {

	const SceneViewCameraInput input = ReadNavigationInput();
	SceneViewCameraNavigationCommand command;
	if (viewArea == InputViewArea::InspectorModelPreview) {
		// モデルプレビューの従来操作を共通のカメラ計算へ渡す
		command.preview = true;
		command.look = (input.buttons & SceneViewCameraInput::kRight) != 0;
		command.pan = (input.buttons & SceneViewCameraInput::kMiddle) != 0;
		command.zoomWheel = input.wheel;
		command.active = command.look || command.pan || input.wheel != 0.0f;
	} else {
		const auto controls = SceneViewCameraNavigationUtility::ResolveControls(settings_.controlStyle, settings_.customControls);
		command = navigationSession_.Resolve(controls, input);
	}
	navigationActive_ = command.active;
	if (command.active) { focusActive_ = false; }
	SceneViewCameraNavigationUtility::ApplyMotion(settings_, navigationState_, command, input);
}

void Engine::SceneViewCameraController::Update2D(InputViewArea viewArea) {

	Input* input = Input::GetInstance();
	const Vector3 previousPosition = settings_.cameraState.transform2D.pos;
	const float previousZoom = settings_.cameraState.orthographicZoom;
	const Vector2 mouseDelta = input->GetMouseMoveValueInView(viewArea);
	const float wheel = input->GetMouseWheel();
	navigationSession_.Cancel();
	navigationActive_ = input->PushMouseCenter() || wheel != 0.0f;

	// 中ドラッグで表示倍率に応じた距離を移動する
	if (input->PushMouseCenter()) {
		const float worldUnitsPerPixel = 1.0f / settings_.cameraState.orthographicZoom;
		settings_.cameraState.transform2D.pos.x -= mouseDelta.x * settings_.panSpeed2D * worldUnitsPerPixel;
		settings_.cameraState.transform2D.pos.y -= mouseDelta.y * settings_.panSpeed2D * worldUnitsPerPixel;
	}

	// カーソル位置を維持してズームする
	if (wheel != 0.0f) {
		const float nextZoom = std::clamp(previousZoom * std::exp(wheel * settings_.zoomRate2D), kMin2DZoom, kMax2DZoom);
		if (const std::optional<Vector2> mouse = input->GetMousePosInView(viewArea)) {
			settings_.cameraState.transform2D.pos.x += mouse->x / previousZoom - mouse->x / nextZoom;
			settings_.cameraState.transform2D.pos.y += mouse->y / previousZoom - mouse->y / nextZoom;
		}
		settings_.cameraState.orthographicZoom = nextZoom;
	}

	// 無効な入力では最後の正常なカメラを維持する
	if (!SceneViewCameraSettingsUtility::IsFinite(settings_.cameraState.transform2D.pos) ||
		!std::isfinite(settings_.cameraState.orthographicZoom)) {
		settings_.cameraState.transform2D.pos = previousPosition;
		settings_.cameraState.orthographicZoom = previousZoom;
	}
}
