#pragma once

//============================================================================
//	include
//============================================================================
#include "TimelineAsset.h"

namespace Engine {

	// EditorとRuntimeで同じ初期設定を使用する
	nlohmann::json CreateDefaultTimelineClipData(TimelineTrackKind kind);
	std::string_view GetTimelineTrackRequiredComponent(TimelineTrackKind kind);
}
