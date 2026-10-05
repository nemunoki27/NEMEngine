#include "ImGuiHelpers.h"
#include "ImGuiCurveEditorInternal.h"

//============================================================================
//	include
//============================================================================

// c++
#include <algorithm>
#include <array>
#include <vector>

#include <imgui_internal.h>

//============================================================================
//	MyGUI Curve classMethods
//============================================================================
namespace {

	using namespace Engine::CurveEditorUtility;

	// 共通の編集状態で描画と操作を順に実行する
	Engine::CurveEditResult DrawCurveEditorInternal(const char* id, std::span<Engine::CurveChannel> channels,
		Engine::CurveEditorState& state, const Engine::CurveEditSetting& inputSetting,
		std::vector<Engine::CurveQuaternionAxisKey>* quaternionAxisKeys) {

		Engine::CurveEditResult result{};
		const Engine::CurveEditSetting& setting = inputSetting;

		ImGui::PushID(id);

		// 初回表示時だけ設定値からstateへスナップ設定を取り込む
		if (state.snapInterval <= 0.0f) {
			state.snapEnabled = setting.snap;
			state.snapInterval = setting.snapInterval;
		}
		// 固定時間範囲のときはキー時刻の上限も揃える
		state.maxKeyTime = setting.fixedTimeRange ? setting.fixedTimeMax : 0.0f;
		// 未初期化の表示範囲を0から1へ揃える
		if ((state.visibleValueMin == 0.0f && state.visibleValueMax == 0.0f) ||
			(state.visibleValueMin == -1.0f && state.visibleValueMax == 1.0f)) {
			state.visibleValueMin = 0.0f;
			state.visibleValueMax = 1.0f;
		}
		// チャンネル数に合わせて表示状態配列を用意する
		state.EnsureChannelCount(static_cast<uint32_t>(channels.size()));
		std::array<uint32_t, 64> keyCounts{};
		for (uint32_t i = 0; i < channels.size() && i < keyCounts.size(); ++i) {
			keyCounts[i] = static_cast<uint32_t>(channels[i].keys.size());
		}
		// 削除済みキーを指す選択をここで破棄する
		state.RemoveInvalidSelections(static_cast<uint32_t>(channels.size()), keyCounts.data());

		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const ImVec2 editorSize = setting.autoFit ? avail : setting.size;

		ImGui::BeginChild("##CurveEditorRoot", editorSize, true, ImGuiWindowFlags_NoScrollbar);
		const float previousFontScale = ImGui::GetCurrentWindow()->FontWindowScale;
		ImGui::SetWindowFontScale(kCurveEditorFontScale);

		// ツールバーはGraph本体より先に描画する
		if (setting.showToolbar) {
			DrawToolbar(channels, state, result);
			ImGui::Separator();
		}

		const float mainAreaHeight = (std::max)(1.0f, ImGui::GetContentRegionAvail().y);
		const float centerHeight = (std::max)(1.0f, mainAreaHeight);
		// 右パネル分を差し引いた幅をGraphに使う
		const float sideWidth = setting.showSidePanels ? kCurveInspectorWidth + ImGui::GetStyle().ItemSpacing.x : 0.0f;
		ImGui::BeginGroup();

		const ImVec2 totalCanvasSize((std::max)(160.0f, ImGui::GetContentRegionAvail().x - sideWidth), centerHeight);

		const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
		const ImVec2 canvasMax(canvasMin.x + totalCanvasSize.x, canvasMin.y + totalCanvasSize.y);
		const ImRect canvasRect(canvasMin, canvasMax);

		// ルーラー付きレイアウト
		const ImRect topRulerRect(ImVec2(canvasRect.Min.x + kCurveLeftRulerWidth, canvasRect.Min.y),
			ImVec2(canvasRect.Max.x, canvasRect.Min.y + kCurveTopRulerHeight));

		const ImRect leftRulerRect(ImVec2(canvasRect.Min.x, canvasRect.Min.y + kCurveTopRulerHeight),
			ImVec2(canvasRect.Min.x + kCurveLeftRulerWidth, canvasRect.Max.y));

		const ImRect graphRect(
			ImVec2(canvasRect.Min.x + kCurveLeftRulerWidth, canvasRect.Min.y + kCurveTopRulerHeight), canvasRect.Max);
		const ImRect topCornerRect(ImVec2(canvasRect.Min.x, canvasRect.Min.y),
			ImVec2(canvasRect.Min.x + kCurveLeftRulerWidth, canvasRect.Min.y + kCurveTopRulerHeight));
		const ImRect bottomCornerRect(ImVec2(canvasRect.Min.x, canvasRect.Max.y - kCurveTopRulerHeight),
			ImVec2(canvasRect.Min.x + kCurveLeftRulerWidth, canvasRect.Max.y));

		if (state.frameSelectionRequest) {
			FitView(graphRect, channels, state);
			state.frameSelectionRequest = false;
		}
		ApplyFixedTimeRange(graphRect, setting, state);

		// Graphの入力をImGuiで受け取る
		ImGui::SetCursorScreenPos(graphRect.Min);
		ImGui::InvisibleButton("##CurveGraphInput", graphRect.GetSize(),
			ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);

		const bool graphActive = ImGui::IsItemActive();
		result.anyItemActive |= graphActive;

		// 描画
		DrawGrid(graphRect, state);
		DrawTimeRuler(topRulerRect, state);
		DrawValueRuler(leftRulerRect, state);

		if (IsColorCurveSet(channels)) {
			DrawColorRGBCurveSamples(graphRect, channels, state);
		}
		for (uint32_t channelIndex = 0; channelIndex < static_cast<uint32_t>(channels.size()); ++channelIndex) {
			if (!state.IsChannelVisible(channelIndex)) {
				continue;
			}
			if (IsQuaternionCurveSet(channels) && channelIndex == 0) {
				continue;
			}
			if (IsColorCurveSet(channels) && channelIndex < 3) {
				continue;
			}
			DrawCurveSamples(graphRect, channels[channelIndex], state);
		}

		DrawKeys(graphRect, channels, state, ToAxisKeySpan(quaternionAxisKeys));
		DrawCurrentTimeLine(graphRect, state);
		bool graphOverlayUIBlocking = false;
		const bool valueChangedBeforeCornerEdit = result.valueChanged;
		DrawGraphRangeEditors(
			topCornerRect, bottomCornerRect, state, result.valueChanged, result.editFinished, graphOverlayUIBlocking);
		if (!valueChangedBeforeCornerEdit && result.valueChanged) {
			UpdateVerticalZoomFromRange(graphRect, state);
		}

		// 矩形選択中の半透明領域を描画する
		if (state.marqueeActive) {
			ImVec2 minPos = state.marqueeMin;
			ImVec2 maxPos = state.marqueeMax;
			NormalizeRect(minPos, maxPos);
			ImGui::GetWindowDrawList()->AddRectFilled(minPos, maxPos, IM_COL32(80, 140, 255, 42));
			ImGui::GetWindowDrawList()->AddRect(minPos, maxPos, IM_COL32(100, 165, 255, 180));
		}

		if (!graphOverlayUIBlocking) {
			HandleGraphInput(graphRect, channels, state, result, quaternionAxisKeys);
		}
		RefreshGridStepsOnly(graphRect, state);

		ImGui::EndGroup();

		// 右側の選択キー詳細
		if (setting.showSidePanels) {
			ImGui::SameLine();
			DrawInspector(channels, state, result, mainAreaHeight, quaternionAxisKeys);
		}

		ImGui::SetWindowFontScale(previousFontScale);
		ImGui::EndChild();
		ImGui::PopID();
		return result;
	}
}

Engine::CurveEditResult Engine::MyGUI::CurveEditor(
	const char* id, std::span<CurveChannel> channels, CurveEditorState& state, const CurveEditSetting& setting) {

	return DrawCurveEditorInternal(id, channels, state, setting, nullptr);
}

Engine::CurveEditResult Engine::MyGUI::CurveEditor(
	const char* id, std::span<CurveChannelRef> channels, CurveEditorState& state, const CurveEditSetting& setting) {

	std::vector<CurveChannel> editChannels{};
	std::vector<uint32_t> sourceIndices{};
	editChannels.reserve(channels.size());
	sourceIndices.reserve(channels.size());

	for (uint32_t i = 0; i < channels.size(); ++i) {
		if (!channels[i].channel) {
			continue;
		}

		// CurveEditor本体は連続したCurveChannel配列を前提にしている
		// Trackをまたぐ編集では、ここで一度作業用配列へ写し、描画後に元のTrackへ戻す
		CurveChannel editChannel = *channels[i].channel;
		if (!channels[i].displayName.empty()) {
			editChannel.name = channels[i].displayName;
		}
		editChannels.emplace_back(std::move(editChannel));
		sourceIndices.emplace_back(i);
	}

	CurveEditResult result = DrawCurveEditorInternal(id, editChannels, state, setting, nullptr);

	// 元のチャンネル名を保って編集値を戻す
	for (uint32_t i = 0; i < editChannels.size(); ++i) {
		CurveChannel* source = channels[sourceIndices[i]].channel;
		if (!source) {
			continue;
		}
		const std::string originalName = source->name;
		*source = std::move(editChannels[i]);
		source->name = originalName;
	}
	return result;
}

Engine::CurveEditResult Engine::MyGUI::CurveEditor(
	const char* id, CurveFloat& curve, CurveEditorState& state, const CurveEditSetting& setting) {

	return CurveEditor(id, GetCurveChannels(curve), state, setting);
}

Engine::CurveEditResult Engine::MyGUI::CurveEditor(
	const char* id, CurveVector3& curve, CurveEditorState& state, const CurveEditSetting& setting) {

	return CurveEditor(id, GetCurveChannels(curve), state, setting);
}

Engine::CurveEditResult Engine::MyGUI::CurveEditor(
	const char* id, CurveColor3& curve, CurveEditorState& state, const CurveEditSetting& setting) {

	return CurveEditor(id, GetCurveChannels(curve), state, setting);
}

Engine::CurveEditResult Engine::MyGUI::CurveEditor(
	const char* id, CurveColor4& curve, CurveEditorState& state, const CurveEditSetting& setting) {

	return CurveEditor(id, GetCurveChannels(curve), state, setting);
}

Engine::CurveEditResult Engine::MyGUI::CurveEditor(
	const char* id, CurveQuaternion& curve, CurveEditorState& state, const CurveEditSetting& setting) {

	curve.EnsureAxisKeyCount();
	return DrawCurveEditorInternal(id, GetCurveChannels(curve), state, setting, &curve.axisKeys);
}
