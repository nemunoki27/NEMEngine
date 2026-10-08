#include "AnimationClipEditorUI.h"
#include "AnimationClipEditSession.h"
#include "AnimationClipEditorUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Editor/UI/ImGui/ImGuiEnum.h>

// c++
#include <algorithm>
#include <array>
#include <cmath>

using namespace Engine;
using namespace Engine::AnimationClipEditorUtility;

namespace {

	// 複数成分のCurveを編集してTrackへ戻す
	template <typename TCurve>
	CurveEditResult DrawTrackCurve(AnimationCurveTrack& track, CurveEditorState& state, const CurveEditSetting& setting) {

		TCurve curve{};
		if (track.channels.size() != curve.channels.size()) {
			return {};
		}
		for (size_t i = 0; i < curve.channels.size(); ++i) {
			curve.channels[i] = track.channels[i];
		}
		const CurveEditResult result = MyGUI::CurveEditor("AnimationClipCurveEditor", curve, state, setting);
		for (size_t i = 0; i < curve.channels.size(); ++i) {
			track.channels[i] = curve.channels[i];
		}
		return result;
	}
}

//============================================================================
//	AnimationClipEditorUI
//============================================================================

void Engine::AnimationClipEditorUI::DrawCurveEditorUI(AnimationClipEditSession& session, const EditorToolContext& context) {

	if (!session.GetHasClip()) {
		return;
	}

	session.NormalizeSelectedTrackIndex();

	if (session.GetSelectedTrackIndex() < 0) {
		ImGui::TextDisabled("Select property track.");
		return;
	}

	AnimationCurveTrack& track = session.GetClip().curveTracks[static_cast<size_t>(session.GetSelectedTrackIndex())];
	if (session.GetEditorViewTrackIndex() != session.GetSelectedTrackIndex()) {
		session.LoadSelectedTrackEditorView();
	}

	if (track.channels.empty()) {
		ImGui::TextDisabled("Visible curve track is empty.");
		return;
	}

	session.GetCurveState().visibleTimeMax = (std::max)(session.GetCurveState().visibleTimeMax, session.GetClip().duration);
	session.GetCurveState().currentTime = session.GetPreviewTime();

	const float previousTime = session.GetCurveState().currentTime;
	CurveEditSetting curveSetting{};
	curveSetting.size = ImVec2(0.0f, 360.0f);
	curveSetting.autoFit = false;
	curveSetting.showSidePanels = false;
	curveSetting.snap = true;
	curveSetting.snapInterval = 0.001f;
	CurveEditResult result{};
	// 選択Trackを値型ごとのCurveで編集する
	switch (track.binding.valueType) {
	case AnimationValueType::Float: {
		CurveFloat curve{};
		curve.channel = track.channels[0];
		result = MyGUI::CurveEditor("AnimationClipCurveEditor", curve, session.GetCurveState(), curveSetting);
		track.channels[0] = curve.channel;
		break;
	}
	case AnimationValueType::Vector3: {
		result = DrawTrackCurve<CurveVector3>(track, session.GetCurveState(), curveSetting);
		break;
	}
	case AnimationValueType::Color3: {
		result = DrawTrackCurve<CurveColor3>(track, session.GetCurveState(), curveSetting);
		break;
	}
	case AnimationValueType::Color4: {
		result = DrawTrackCurve<CurveColor4>(track, session.GetCurveState(), curveSetting);
		break;
	}
	case AnimationValueType::Quaternion: {
		// 回転軸と角度を別々のキーで編集する
		const bool wasAxisAngleTrack = IsQuaternionAxisAngleTrack(track);
		CurveQuaternion curve = BuildQuaternionEditorCurve(track);
		result = MyGUI::CurveEditor("AnimationClipCurveEditor", curve, session.GetCurveState(), curveSetting);
		if (!wasAxisAngleTrack || result.valueChanged || result.editFinished) {
			StoreQuaternionEditorCurve(curve, track);
			result.valueChanged |= !wasAxisAngleTrack;
		}
		break;
	}
	case AnimationValueType::Vector2:
	default: {
		std::vector<CurveChannelRef> channelRefs{};
		for (CurveChannel& channel : track.channels) {
			CurveChannelRef ref{};
			ref.channel = &channel;
			ref.displayName = channel.name;
			channelRefs.emplace_back(std::move(ref));
		}
		result = MyGUI::CurveEditor("AnimationClipCurveEditor", channelRefs, session.GetCurveState(), curveSetting);
		break;
	}
	}

	// 表示範囲の色変化を帯で描画する
	const bool isColorTrack =
		track.binding.valueType == AnimationValueType::Color3 || track.binding.valueType == AnimationValueType::Color4;
	if (isColorTrack && track.channels.size() >= 3) {

		const bool hasAlpha = track.binding.valueType == AnimationValueType::Color4;
		MyGUI::CurveColorGradientBar(
			track.channels, session.GetCurveState().visibleTimeMin, session.GetCurveState().visibleTimeMax, hasAlpha);
	}

	session.StoreSelectedTrackEditorView();

	if (result.valueChanged || result.editFinished) {
		// 最後のキーに合わせて再生時間を更新する
		UpdateAnimationClipAutoDuration(session.GetClip());
		session.MarkClipDirty();
	}
	if (previousTime != session.GetCurveState().currentTime || result.valueChanged) {
		// Curve上の時刻をプレビューへ反映する
		session.GetPreviewTime() = (std::clamp)(session.GetCurveState().currentTime, 0.0f, session.GetClip().duration);
		session.GetCurveState().currentTime = session.GetPreviewTime();
		session.ApplyPreviewAtCurrentTime(context, true);
	}
}

void Engine::AnimationClipEditorUI::DrawKeyInspectorUI(AnimationClipEditSession& session, const EditorToolContext& context) {

	if (session.GetSelectedTrackIndex() < 0 ||
		static_cast<int>(session.GetClip().curveTracks.size()) <= session.GetSelectedTrackIndex()) {
		return;
	}
	if (!MyGUI::CollapsingHeader("キーインスペクター")) {
		return;
	}
	if (session.GetCurveState().selectedKeys.empty()) {
		ImGui::TextDisabled("キーが選択されていません");
		return;
	}

	AnimationCurveTrack& track = session.GetClip().curveTracks[static_cast<size_t>(session.GetSelectedTrackIndex())];
	CurveKeySelection selection = session.GetCurveState().selectedKeys.front();
	if (track.channels.size() <= selection.channelIndex ||
		track.channels[selection.channelIndex].keys.size() <= selection.keyIndex) {
		return;
	}

	CurveChannel& channel = track.channels[selection.channelIndex];
	CurveKey& key = channel.keys[selection.keyIndex];

	ImGui::Text("チャンネル: %s", channel.name.c_str());
	bool changed = false;
	float time = key.time;
	if (MyGUI::DragFloat("キー時間", time, {.dragSpeed = 0.001f, .minValue = 0.0f, .maxValue = 10000.0f}).valueChanged) {
		key.time = (std::max)(0.0f, time);
		changed = true;
	}

	if (IsQuaternionAxisAngleTrack(track) && selection.channelIndex == 0u) {

		CurveQuaternionAxisKey& axisKey = GetQuaternionAxisKeyForEdit(track, selection.keyIndex);
		if (MyGUI::Checkbox("カスタム軸", axisKey.useCustomAxis)) {
			changed = true;
		}
		if (axisKey.useCustomAxis) {
			Vector3 customAxis = axisKey.customAxis;
			if (MyGUI::DragVector3("キー回転軸", customAxis, {.dragSpeed = 0.001f, .minValue = -1.0f, .maxValue = 1.0f})
					.valueChanged) {
				axisKey.customAxis = customAxis;
				changed = true;
			}
		} else {
			if (axisKey.axes.empty()) {
				axisKey.axes.emplace_back(Axis::X);
			}
			Axis axis = axisKey.axes.front();
			if (MyGUI::EnumCombo("キー回転軸", axis).valueChanged) {
				axisKey.axes = {axis};
				changed = true;
			}
		}
		key.value = GetPrimaryAxisValue(axisKey);
		key.interpolation = CurveInterpolationMode::Constant;
	} else if (IsQuaternionAxisAngleTrack(track) && selection.channelIndex == 1u) {

		if (MyGUI::DragFloat("キー角度", key.value,
				{
					.dragSpeed = 0.1f,
					.minValue = -36000.0f,
					.maxValue = 36000.0f,
				})
				.valueChanged) {
			changed = true;
		}
	} else if (CanDrawColorRgbKeyEditor(track, selection.channelIndex)) {
		if (DrawColorKeyValueEditor(track, selection.channelIndex, key.time)) {
			changed = true;
		}
	} else {
		if (MyGUI::DragFloat("キー値", key.value, {.dragSpeed = 0.001f, .minValue = -100000.0f, .maxValue = 100000.0f})
				.valueChanged) {
			changed = true;
		}
	}

	const bool quaternionTrack = track.binding.valueType == AnimationValueType::Quaternion;
	CurveInterpolationMode interpolation = key.interpolation;
	if (!quaternionTrack && interpolation == CurveInterpolationMode::Squad) {
		// 回転以外のSquadをSplineとして表示する
		interpolation = CurveInterpolationMode::Spline;
	}

	if (!(IsQuaternionAxisAngleTrack(track) && selection.channelIndex == 0u) && MyGUI::BeginPropertyRow("補間方法")) {
		if (quaternionTrack) {

			if (Engine::ImGuiUtility::EnumCombo<CurveInterpolationMode>("##Value", &interpolation)) {
				key.interpolation = interpolation;
				changed = true;
			}
		} else {

			constexpr std::array<CurveInterpolationMode, 4> kNonQuaternionInterpolations{
				CurveInterpolationMode::Constant,
				CurveInterpolationMode::Linear,
				CurveInterpolationMode::Bezier,
				CurveInterpolationMode::Spline,
			};

			if (ImGui::BeginCombo("##Value", EnumAdapter<CurveInterpolationMode>::ToString(interpolation))) {
				for (CurveInterpolationMode mode : kNonQuaternionInterpolations) {

					const bool selected = interpolation == mode;
					if (ImGui::Selectable(EnumAdapter<CurveInterpolationMode>::ToString(mode), selected)) {
						key.interpolation = mode;
						changed = true;
					}
					if (selected) {
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
		}
		MyGUI::EndPropertyRow();
	}

	if (key.interpolation == CurveInterpolationMode::Bezier) {
		Vector2 inTangent = key.inTangent;
		if (MyGUI::DragVector2(
				"入力タンジェント", inTangent, {.dragSpeed = 0.001f, .minValue = -10000.0f, .maxValue = 10000.0f})
				.valueChanged) {
			key.inTangent = inTangent;
			changed = true;
		}
		Vector2 outTangent = key.outTangent;
		if (MyGUI::DragVector2(
				"出力タンジェント", outTangent, {.dragSpeed = 0.001f, .minValue = -10000.0f, .maxValue = 10000.0f})
				.valueChanged) {
			key.outTangent = outTangent;
			changed = true;
		}
	}

	if (changed) {
		const float editedTime = key.time;
		if (IsQuaternionAxisAngleTrack(track) && selection.channelIndex == 0u) {
			SortQuaternionAxisKeys(track);
		} else {
			channel.SortKeys();
		}
		// 並べ替えたキーを再選択する
		for (uint32_t i = 0; i < channel.keys.size(); ++i) {
			if (std::abs(channel.keys[i].time - editedTime) <= 0.0005f) {
				session.GetCurveState().SelectSingle(selection.channelIndex, i);
				break;
			}
		}
		UpdateAnimationClipAutoDuration(session.GetClip());
		session.MarkClipDirty();
		session.ApplyPreviewAtCurrentTime(context, true);
	}
}

void Engine::AnimationClipEditorUI::DrawGeneratorUI(AnimationClipEditSession& session, const EditorToolContext& context) {

	if (session.GetSelectedTrackIndex() < 0 ||
		static_cast<int>(session.GetClip().curveTracks.size()) <= session.GetSelectedTrackIndex()) {
		return;
	}

	AnimationCurveTrack& track = session.GetClip().curveTracks[static_cast<size_t>(session.GetSelectedTrackIndex())];
	if (track.channels.empty()) {
		return;
	}

	if (!MyGUI::CollapsingHeader("カーブ生成")) {
		return;
	}

	// 生成条件と適用先のUIは共通実装を使う
	const std::vector<CurveBakeTarget> bakeTargets = BuildBakeTargets(track);
	if (DrawCurveGenerator(session.GetGeneratorState(), track.channels, bakeTargets)) {
		session.UpdateAutoDurationAndPreview(context);
	}
}

void Engine::AnimationClipEditorUI::DrawEventListUI(AnimationClipEditSession& session) {

	if (!session.GetHasClip()) {
		return;
	}
	if (!MyGUI::CollapsingHeader("イベント")) {
		return;
	}

	int removeIndex = -1;
	for (int i = 0; i < static_cast<int>(session.GetClip().events.size()); ++i) {

		ImGui::PushID(i);
		AnimationEvent& event = session.GetClip().events[static_cast<size_t>(i)];
		if (ImGui::TreeNodeEx("Event", ImGuiTreeNodeFlags_DefaultOpen, "イベント : %s",
				event.name.empty() ? "<名前なし>" : event.name.c_str())) {

			if (MyGUI::InputText("名前", event.name).valueChanged) {
				session.MarkClipDirty();
			}
			if (MyGUI::DragFloat("時刻", event.time, {.dragSpeed = 0.001f, .minValue = 0.0f, .maxValue = 10000.0f})
					.valueChanged) {
				event.time = std::clamp(event.time, 0.0f, session.GetClip().duration);
				session.MarkClipDirty();
			}
			if (MyGUI::DragFloat(
					"floatパラメータ", event.floatParam, {.dragSpeed = 0.001f, .minValue = -10000.0f, .maxValue = 10000.0f})
					.valueChanged) {
				session.MarkClipDirty();
			}
			if (MyGUI::DragInt("intパラメータ", event.intParam, {.dragSpeed = 1.0f, .minValue = -1000000, .maxValue = 1000000})
					.valueChanged) {
				session.MarkClipDirty();
			}
			if (MyGUI::InputText("stringパラメータ", event.stringParam).valueChanged) {
				session.MarkClipDirty();
			}
			if (ImGui::Button("イベントを削除", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
				removeIndex = i;
			}
			ImGui::TreePop();
		}
		ImGui::Separator();
		ImGui::PopID();
	}

	if (0 <= removeIndex && removeIndex < static_cast<int>(session.GetClip().events.size())) {
		session.GetClip().events.erase(session.GetClip().events.begin() + removeIndex);
		session.MarkClipDirty();
	}
	if (ImGui::Button("イベントを追加", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

		// 現在のプレビュー再生位置にイベントを追加する
		AnimationEvent event{};
		event.time = std::clamp(session.GetPreviewTime(), 0.0f, session.GetClip().duration);
		event.name = "Event";
		session.GetClip().events.emplace_back(std::move(event));
		session.MarkClipDirty();
	}
}
