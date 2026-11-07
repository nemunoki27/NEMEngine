#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/RendererRenderSettings.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

namespace Engine {

	// Rendererの描画状態を共通の順序で編集する
	ValueEditResult DrawRendererRenderSettingsFields(RendererRenderSettings& settings, bool* enableZPrepass = nullptr);
} // Engine
