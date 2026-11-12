#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Timeline/TimelineAsset.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

// c++
#include <string_view>

namespace Engine::TimelineEditorFields {

	// 既定の割り当て名は参照名を変えず日本語で表示する
	std::string_view BindingLabel(std::string_view name);

	// 種類ごとの表示名と色を返す
	const char* TrackLabel(TimelineTrackKind kind);
	ImU32 TrackColor(TimelineTrackKind kind);
	// プロパティ行で候補の添字を編集する
	ValueEditResult Select(const char* label, int& selected, const char* const* names, int count);
	// 秒数を表示単位へ変換して編集する
	ValueEditResult Time(const char* label, double& seconds, double frameRate, bool frames, double minimum = 0.0);
	// 割り当てとパラメーターを永続識別子で選択する
	ValueEditResult Binding(const char* label, UUID& selected, const TimelineAsset& asset, bool allowNone = true);
	ValueEditResult Parameter(const char* label, UUID& selected, const TimelineAsset& asset);
	// 対応する型のMyGUI編集を呼び出す
	ValueEditResult ParameterValue(const char* label, TimelineParameterValue& value, bool chooseType = false);
	// シーンの参照型を持つトラックを判定する
	const char* RequiredComponent(TimelineTrackKind kind);
}
