#pragma once

//============================================================================
//	include
//============================================================================
#include <imgui.h>

namespace Engine::TimelineAppearance {

	enum class Color {
		Background, Ruler, Track, SelectedTrack, Clip, ActiveClip, SelectedClip,
		Grid, MinorGrid, Divider, Text, MutedText, Playhead, Selection, Blend,
		Accent, Range, SelectionFill, Zoom, Count
	};

	// タイムライン内の配色を一か所で管理する
	ImU32 GetColor(Color color);
	void PushStyle();
	void PopStyle();
}
