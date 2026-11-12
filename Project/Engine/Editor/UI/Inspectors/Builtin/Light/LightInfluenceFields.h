#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Core/World/Components/Lighting/LightInfluenceSettings.h>

namespace Engine {

	struct EditorPanelContext;

	// 全ライト共通の照射対象と影の除外を編集する
	ValueEditResult DrawLightInfluenceFields(const EditorPanelContext& context, ECSWorld& world,
		Entity owner, LightInfluenceSettings& settings);
}
