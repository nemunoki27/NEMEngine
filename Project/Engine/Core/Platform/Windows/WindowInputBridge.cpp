#include "WindowInputBridge.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Platform/Input/InputSystem.h>

void Engine::WindowInputBridge::NotifyFocus(bool focused) {

	if (Input* input = Input::TryGetInstance()) {
		input->SetWindowFocus(focused);
	}
}

void Engine::WindowInputBridge::AppendCharacter(WPARAM character) {

	if (character >= 0x20 || character == L'\t' || character == L'\n' || character == L'\r') {
		if (Input* input = Input::TryGetInstance()) {
			input->AppendTextInputUtf16(static_cast<wchar_t>(character));
		}
	}
}

void Engine::WindowInputBridge::PushDrop(const WindowFileDrop& drop) {

	if (Input* input = Input::TryGetInstance()) {
		input->PushDroppedFiles(drop.paths, drop.position);
	}
}
