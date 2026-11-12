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
	// 時間源と再生開始の項目を同じ順序で表示する
	ValueEditResult DrawPlaybackStartFields(const EditorPanelContext& context, ECSWorld& world,
		Entity owner, TimeSourceSettings& settings, bool& playInEditMode, bool* playOnStart = nullptr);
}
