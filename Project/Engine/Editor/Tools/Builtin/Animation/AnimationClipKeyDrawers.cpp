#include "AnimationClipEditorUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

bool Engine::AnimationClipEditorUtility::DrawColorKeyValueEditor(AnimationCurveTrack& track,
	uint32_t selectedChannelIndex, float time) {

	if (!CanDrawColorRgbKeyEditor(track, selectedChannelIndex)) return false;
	// 不足するキーは現在時刻の評価値で補う
	constexpr uint32_t kRgbChannelCount = 3u;
	float values[3] = {
		track.channels[0].Evaluate(time),
		track.channels[1].Evaluate(time),
		track.channels[2].Evaluate(time),
	};
	Color3 color(values[0], values[1], values[2]);
	const ValueEditResult result = MyGUI::ColorEdit("キー色 RGB", color);
	if (!result.valueChanged) return false;
	values[0] = color.r;
	values[1] = color.g;
	values[2] = color.b;
	for (uint32_t i = 0; i < kRgbChannelCount; ++i) {

		uint32_t keyIndex = 0;
		if (FindKeyIndexAtTime(track.channels[i], time, keyIndex)) track.channels[i].keys[keyIndex].value = values[i];
		else track.channels[i].AddKey(time, values[i], CurveInterpolationMode::Linear);
	}
	return true;
}
