#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Simple/SimpleAnimationEvaluation.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

namespace Engine::SimpleAnimationFields {

	ValueEditResult DrawValue(const char* label, SimpleAnimationValue& value);
	void DrawCurrentValue(const char* label, const SimpleAnimationValue& value);
	ValueEditResult DrawSettings(SimpleAnimationSettings& settings, bool allowTypeChange);
}
