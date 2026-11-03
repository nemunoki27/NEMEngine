#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Time/LocalTime.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>

namespace Engine {
	// すべての時間受信コンポーネントで共通の時間源を編集する
	ValueEditResult DrawTimeSourceField(const EditorPanelContext& context, ECSWorld& world,
		Entity owner, TimeSourceSettings& settings);
}
