#include "CurveEditorState.h"

//============================================================================
//	include
//============================================================================
#include <algorithm>

//============================================================================
//	CurveEditorState classMethods
//============================================================================

bool Engine::CurveEditorState::IsChannelVisible(uint32_t channelIndex) const {

	if (64 <= channelIndex) {
		return false;
	}
	if ((activeChannelMask & (1ull << channelIndex)) == 0) {
		return false;
	}
	if (channelIndex < channelVisible.size()) {
		return channelVisible[channelIndex];
	}
	return true;
}

void Engine::CurveEditorState::SetChannelVisible(uint32_t channelIndex, bool visible) {

	// 未登録のチャンネルでも直接指定できるように表示配列を拡張する
	EnsureChannelCount(channelIndex + 1);
	channelVisible[channelIndex] = visible;

	// 高速な一括判定用のbit maskも同期する
	if (visible) {
		activeChannelMask |= 1ull << channelIndex;
	} else {
		activeChannelMask &= ~(1ull << channelIndex);
	}
}

void Engine::CurveEditorState::EnsureChannelCount(uint32_t channelCount) {

	if (channelVisible.size() < channelCount) {
		channelVisible.resize(channelCount, true);
	}
}

void Engine::CurveEditorState::ClearSelection() {

	selectedKeys.clear();
}

bool Engine::CurveEditorState::IsSelected(uint32_t channelIndex, uint32_t keyIndex) const {

	return std::find(selectedKeys.begin(), selectedKeys.end(), CurveKeySelection{ channelIndex, keyIndex }) != selectedKeys.end();
}

void Engine::CurveEditorState::SelectSingle(uint32_t channelIndex, uint32_t keyIndex) {

	selectedKeys.clear();
	selectedKeys.push_back({ channelIndex, keyIndex });
}

void Engine::CurveEditorState::ToggleSelection(uint32_t channelIndex, uint32_t keyIndex) {

	const CurveKeySelection selection{ channelIndex, keyIndex };
	auto it = std::find(selectedKeys.begin(), selectedKeys.end(), selection);
	if (it != selectedKeys.end()) {
		selectedKeys.erase(it);
		return;
	}
	selectedKeys.push_back(selection);
}

void Engine::CurveEditorState::RemoveInvalidSelections(uint32_t channelCount, const uint32_t* keyCounts) {

	// チャンネル数やキー数が変わったあと、範囲外になった選択を消す
	selectedKeys.erase(std::remove_if(selectedKeys.begin(), selectedKeys.end(),
		[&](const CurveKeySelection& selection) {
			return channelCount <= selection.channelIndex || keyCounts[selection.channelIndex] <= selection.keyIndex;
		}), selectedKeys.end());
}
