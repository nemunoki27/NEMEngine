#include "MotionAnimationInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Motion/MotionAnimationEvaluation.h>
#include <Engine/Core/World/Components/Transform/SplineComponent.h>
#include <Engine/Editor/UI/Common/SceneEntityReferenceFields.h>
#include <Engine/Editor/UI/Inspectors/Common/TimeSourceFields.h>
#include <Engine/Editor/UI/Inspectors/Common/SimpleAnimationFields.h>
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>

// c++
#include <algorithm>

using namespace Engine;

namespace {

	// MyGUIのプロパティ行で列挙値を選択する
	template <typename T, size_t N>
	ValueEditResult Select(const char* label, T& value, const char* const (&names)[N]) {

		ValueEditResult edit;
		if (!MyGUI::BeginPropertyRow(label)) return edit;
		ImGui::PushID(label);
		int selected = static_cast<int>(value);
		edit.valueChanged = ImGui::Combo("##Value", &selected, names, static_cast<int>(N));
		edit.editFinished = edit.valueChanged;
		edit.anyItemActive = ImGui::IsItemActive();
		if (edit.valueChanged) value = static_cast<T>(selected);
		ImGui::PopID();
		MyGUI::EndPropertyRow();
		return edit;
	}
}

void MotionAnimationInspectorDrawer::OnSyncDraftFromWorld(ECSWorld& world, const Entity& entity,
	[[maybe_unused]] const MotionAnimationComponent& component) {

	const auto* record = TryGetMotionAnimation(world, entity);
	settings_ = record ? record->settings : MotionAnimationSettings{};
	targetRuntimeID_ = record && record->runtime.hasTargetOverride && world.IsAlive(record->runtime.targetOverride) ?
		world.GetUUID(record->runtime.targetOverride) : UUID{};
	splineRuntimeID_ = record && record->runtime.hasSplineOverride && world.IsAlive(record->runtime.splineOverride) ?
		world.GetUUID(record->runtime.splineOverride) : UUID{};
}

void MotionAnimationInspectorDrawer::SerializeDraft([[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] const MotionAnimationComponent& component, nlohmann::json& out) const {

	out = settings_;
}

void MotionAnimationInspectorDrawer::ApplyPreview(ECSWorld& world, const Entity& entity,
	[[maybe_unused]] const MotionAnimationComponent& component) {

	SetMotionAnimationSettings(world, entity, settings_);
	if (auto* record = TryGetMotionAnimation(world, entity)) {
		auto& runtime = record->runtime;
		const Entity target = world.FindByUUID(targetRuntimeID_);
		const Entity spline = world.FindByUUID(splineRuntimeID_);
		if (runtime.targetOverride != target || runtime.hasTargetOverride != static_cast<bool>(targetRuntimeID_))
			runtime.resourcesDirty = true;
		if (runtime.splineOverride != spline || runtime.hasSplineOverride != static_cast<bool>(splineRuntimeID_)) {
			runtime.splineSamples.clear();
			runtime.resourcesDirty = true;
		}
		runtime.hasTargetOverride = static_cast<bool>(targetRuntimeID_);
		runtime.targetOverride = target;
		runtime.hasSplineOverride = static_cast<bool>(splineRuntimeID_);
		runtime.splineOverride = spline;
	}
}

void MotionAnimationInspectorDrawer::DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& active) {

	auto& settings = settings_;
	const auto check = [&](const char* label, bool& value) {
		DrawField(active, [&] { return InspectorDrawerCommon::DrawCheckboxField(label, value); });
	};
	const auto number = [&](const char* label, float& value, float minimum = 0.0f) {
		DrawField(active, [&] { return MyGUI::DragFloat(label, value, {.dragSpeed = 0.01f, .minValue = minimum, .maxValue = 1000000.0f}); });
	};
	check(GetInspectorLabel(InspectorFieldID::Enabled), settings.enabled);
	DrawField(active, [&] {
		return DrawPlaybackStartFields(context, world, entity, settings.timeSource, settings.playInEditMode, &settings.playOnStart);
	});
	const char* modes[]{"開始姿勢からの移動", "対象へ接近", "区間ごとの移動", "Splineの距離移動"};
	DrawField(active, [&] { return Select("移動方式", settings.mode, modes); });
	const char* planes[]{"3D", "地面 XZ・高さを維持", "2D XY"};
	DrawField(active, [&] { return Select("移動平面", settings.plane, planes); });
	if (settings.mode == MotionAnimationMode::Relative) DrawField(active, [&] { return MyGUI::DragVector3("開始姿勢からの距離", settings.offset); });
	if (settings.mode == MotionAnimationMode::ApproachTarget ||
		(settings.applyRotation && settings.rotation == MotionAnimationRotation::LookAtTarget)) {
		DrawField(active, [&] { return SceneEntityReferenceFields::Draw("対象", world, entity, settings.target, {}, &targetRuntimeID_); });
		check("対象の移動を追跡", settings.trackTarget);
	}
	if (settings.mode == MotionAnimationMode::ApproachTarget) number("停止距離", settings.stoppingDistance);
	if (settings.mode == MotionAnimationMode::Spline) {
		EntityEditSetting referenceSetting;
		referenceSetting.acceptEntity = [](ECSWorld& source, Entity target) {
			return source.TryGetComponentForBinding<SplineComponent>(target) != nullptr;
		};
		referenceSetting.rejectReason = "Splineコンポーネントが必要です";
		DrawField(active, [&] {
			return SceneEntityReferenceFields::Draw("Spline", world, entity, settings.spline, referenceSetting, &splineRuntimeID_);
		});
		number("開始距離", settings.splineStart);
		number("終了距離", settings.splineEnd);
	}
	if (settings.mode == MotionAnimationMode::Segments) {
		for (size_t i = 0; i < settings.segments.size(); ++i) {
			ImGui::PushID(static_cast<int>(i));
			if (ImGui::TreeNode("Segment", "区間 %zu", i + 1)) {
				auto& segment = settings.segments[i];
				DrawField(active, [&] { return MyGUI::DragVector3("開始姿勢からの終点", segment.offset); });
				number("所要時間", segment.duration, 0.0001f);
				DrawField(active, [&] { return MyGUI::EnumCombo("イージング", segment.easing); });
				if (settings.segments.size() > 1 && ImGui::Button("区間を削除")) {
					settings.segments.erase(settings.segments.begin() + i); PushEditResult({true, false, true}, active);
					ImGui::TreePop();
					ImGui::PopID();
					break;
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		if (settings.segments.size() < 1024 && ImGui::Button("区間を追加")) {
			settings.segments.push_back({settings.segments.back().offset + Vector3(0, 0, 1)}); PushEditResult({true, false, true}, active);
		}
	} else number("所要時間", settings.duration, 0.0001f);
	DrawField(active, [&] { return MyGUI::EnumCombo("イージング", settings.easing); });
	check("位置を適用", settings.applyPosition);
	check("回転を適用", settings.applyRotation);
	if (settings.applyRotation) {
		const char* rotations[]{"開始姿勢からの回転", "対象を向く", "進行方向を向く"};
		DrawField(active, [&] { return Select("回転方式", settings.rotation, rotations); });
		if (settings.rotation == MotionAnimationRotation::Relative) {
			const char* editModes[]{"軸と角度", "オイラー角"};
			DrawField(active, [&] {
				auto mode = settings.rotationEdit;
				const auto edit = Select("回転編集", mode, editModes);
				if (edit.valueChanged) SetMotionAnimationRotationEdit(settings, mode);
				return edit;
			});
			if (settings.rotationEdit == SimpleAnimationRotationMode::Euler)
				DrawField(active, [&] { return MyGUI::DragVector3("角度", settings.euler); });
			else {
				DrawField(active, [&] { return MyGUI::DragVector3("回転軸", settings.axis); });
				number("角度", settings.angle, -1000000.0f);
			}
		}
	}
	number(GetInspectorLabel(InspectorFieldID::PlaybackSpeed), settings.speed);
	number(GetInspectorLabel(InspectorFieldID::StartDelay), settings.delay);
	DrawField(active, [&] { return MyGUI::EnumCombo(GetInspectorLabel(InspectorFieldID::PlaybackMode), settings.repeat); });
	if (settings.repeat != SimpleAnimationRepeat::Once) {
		int cycles = static_cast<int>((std::min)(settings.cycleCount, 1000000u));
		DrawField(active, [&] {
			auto edit = MyGUI::DragInt("回数・0は無制限", cycles);
			if (edit.valueChanged) settings.cycleCount = static_cast<uint32_t>((std::max)(0, cycles));
			return edit;
		});
	}
	check("自動適用", settings.autoApply);
	check("デバッグ描画", settings.drawDebug);
	ImGui::Separator();
	auto* record = TryGetMotionAnimation(world, entity);
	if (!record) return;
	auto& runtime = record->runtime;
	const bool previewDisabled = !context.IsPlaying() && !settings.playInEditMode;
	const auto action = DrawInspectorPlaybackControls(!previewDisabled);
	if (action != InspectorPlaybackAction::None) runtime.request = static_cast<int32_t>(action);
	ImGui::BeginDisabled(previewDisabled);
	float time = static_cast<float>(runtime.time);
	if (MyGUI::DragFloat("再生時刻", time, {.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 1000000.0f}).valueChanged) {
		runtime.seekTime = time;
		runtime.request = 6;
	}
	ImGui::EndDisabled();
	if (previewDisabled) MyGUI::Text("操作", "編集中も再生を有効にしてください");
	SimpleAnimationFields::DrawCurrentValue("現在位置", runtime.position);
	SimpleAnimationFields::DrawCurrentValue("現在回転", Quaternion::ToEulerDegrees(runtime.rotation));
	MyGUI::Text(GetInspectorLabel(InspectorFieldID::PlaybackState),
		GetInspectorPlaybackState(runtime.playing, runtime.paused, runtime.finished));
	if (!runtime.diagnostic.empty()) MyGUI::Text("適用状態", runtime.diagnostic);
}
