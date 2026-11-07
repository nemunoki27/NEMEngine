#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

namespace Engine::ShaderGraphWidgets {

	// DirectXの列挙名から型の接頭辞を除いて表示する
	template <typename T>
	Engine::ValueEditResult D3D12EnumCombo(const char* label, T& currentValue) {

		return Engine::MyGUI::EnumCombo(label, currentValue);
	}
} // Engine::ShaderGraphWidgets
