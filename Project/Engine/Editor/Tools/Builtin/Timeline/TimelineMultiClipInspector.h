#pragma once

//============================================================================
//	include
//============================================================================
#include "TimelineEditOperations.h"
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

namespace Engine {

	// 複数選択したクリップの共通設定を編集する
	ValueEditResult DrawTimelineMultiClipInspector(TimelineAsset& asset,
		std::span<const TimelineClipSelection> selection, TimelineEditMode mode, bool autoBlend, bool frames);
}
