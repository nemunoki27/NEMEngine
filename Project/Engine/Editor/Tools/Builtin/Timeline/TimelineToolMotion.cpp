#include "TimelineTool.h"
#include "TimelineClipFields.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Motion/MotionAnimationEvaluation.h>
#include <Engine/Core/World/Components/Animation/MotionAnimationComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <algorithm>

using namespace Engine;

void TimelineTool::DrawMotionClipData(const EditorToolContext& context, TimelineClip& clip) {

	MotionAnimationSettings settings;
	if (clip.data.contains("settings")) settings = clip.data.at("settings").get<MotionAnimationSettings>();
	bool changed = false;
	const auto edit = [&](const ValueEditResult& result) { changed |= result.valueChanged; };
	const char* modes[]{"開始姿勢からの移動", "対象へ接近", "区間ごとの移動", "Splineの距離移動"};
	int mode = static_cast<int>(settings.mode);
	if (TimelineEditorFields::Select("移動方式", mode, modes, 4).valueChanged) {
		settings.mode = static_cast<MotionAnimationMode>(mode); changed = true;
	}
	const char* planes[]{"3D", "地面 XZ・高さを維持", "2D XY"};
	int plane = static_cast<int>(settings.plane);
	if (TimelineEditorFields::Select("移動平面", plane, planes, 3).valueChanged) {
		settings.plane = static_cast<MotionAnimationPlane>(plane); changed = true;
	}
	if (settings.mode == MotionAnimationMode::Relative) edit(MyGUI::DragVector3("開始姿勢からの距離", settings.offset));
	if (settings.mode == MotionAnimationMode::ApproachTarget ||
		(settings.applyRotation && settings.rotation == MotionAnimationRotation::LookAtTarget)) {
		TimelineClipFields::Binding("対象", clip.data, "targetBinding", session_.GetAsset());
		edit(MyGUI::CheckboxField("対象の移動を追跡", settings.trackTarget));
	}
	if (settings.mode == MotionAnimationMode::ApproachTarget)
		edit(MyGUI::DragFloat("停止距離", settings.stoppingDistance, {.minValue = 0.0f, .maxValue = 1000000.0f}));
	if (settings.mode == MotionAnimationMode::Spline) {
		std::vector<UUID> ids{{}};
		std::vector<std::string> labels{"未設定"};
		const std::string current = clip.data.value("splineBinding", "");
		int selected = 0;
		for (const auto& role : session_.GetAsset().bindings) {
			TimelineTrack query;
			query.binding = role.id;
			const Entity target = ResolveTrackTarget(context, query);
			if (context.GetWorld() && context.GetWorld()->IsAlive(target) && !context.GetWorld()->HasComponent(target, "Spline")) continue;
			ids.push_back(role.id);
			labels.push_back(role.name);
			if (current == ToString(role.id) || current == role.name) selected = static_cast<int>(ids.size() - 1);
		}
		std::vector<const char*> names;
		for (const auto& name : labels) names.push_back(name.c_str());
		if (TimelineEditorFields::Select("Spline", selected, names.data(), static_cast<int>(names.size())).valueChanged)
			clip.data["splineBinding"] = ids[selected] ? ToString(ids[selected]) : std::string{};
		edit(MyGUI::DragFloat("開始距離", settings.splineStart, {.minValue = 0.0f, .maxValue = 1000000.0f}));
		edit(MyGUI::DragFloat("終了距離", settings.splineEnd, {.minValue = 0.0f, .maxValue = 1000000.0f}));
	}
	if (settings.mode == MotionAnimationMode::Segments) {
		if (MyGUI::CollapsingHeader("移動の区間")) {
			ImGui::Indent();
			for (size_t i = 0; i < settings.segments.size(); ++i) {
				ImGui::PushID(static_cast<int>(i));
				auto& segment = settings.segments[i];
				if (MyGUI::CollapsingHeader(("区間 " + std::to_string(i + 1)).c_str(), false)) {
					ImGui::Indent();
					edit(MyGUI::DragVector3("開始姿勢からの終点", segment.offset));
					edit(MyGUI::DragFloat("時間の比率", segment.duration, {.minValue = 0.0001f, .maxValue = 1000000.0f}));
					edit(MyGUI::EnumCombo("イージング", segment.easing));
					const bool remove = settings.segments.size() > 1 && ImGui::Button("区間を削除", ImVec2(-1, 0));
					ImGui::Unindent();
					if (remove) {
						settings.segments.erase(settings.segments.begin() + i);
						changed = true;
						ImGui::PopID();
						break;
					}
				}
				ImGui::PopID();
			}
			if (settings.segments.size() < 1024 && ImGui::Button("区間を追加", ImVec2(-1, 0))) {
				settings.segments.push_back({settings.segments.back().offset + Vector3(0, 0, 1)});
				changed = true;
			}
			ImGui::Unindent();
		}
	}
	edit(MyGUI::CheckboxField("位置を適用", settings.applyPosition));
	edit(MyGUI::CheckboxField("回転を適用", settings.applyRotation));
	if (settings.applyRotation) {
		const char* rotations[]{"開始姿勢からの回転", "対象を向く", "進行方向を向く"};
		int rotation = static_cast<int>(settings.rotation);
		if (TimelineEditorFields::Select("回転方式", rotation, rotations, 3).valueChanged) {
			settings.rotation = static_cast<MotionAnimationRotation>(rotation); changed = true;
		}
		if (settings.rotation == MotionAnimationRotation::Relative) {
			const char* rotationModes[]{"軸と角度", "オイラー角"};
			int rotationEdit = static_cast<int>(settings.rotationEdit);
			if (TimelineEditorFields::Select("回転編集", rotationEdit, rotationModes, 2).valueChanged) {
				SetMotionAnimationRotationEdit(settings, static_cast<SimpleAnimationRotationMode>(rotationEdit));
				changed = true;
			}
			if (settings.rotationEdit == SimpleAnimationRotationMode::Euler) edit(MyGUI::DragVector3("角度", settings.euler));
			else {
				edit(MyGUI::DragVector3("回転軸", settings.axis));
				edit(MyGUI::DragFloat("角度", settings.angle));
			}
		}
	}
	edit(MyGUI::CheckboxField("デバッグ描画", settings.drawDebug));
	if (changed) {
		settings.target = {};
		settings.spline = {};
		clip.data["settings"] = settings;
	}
}
