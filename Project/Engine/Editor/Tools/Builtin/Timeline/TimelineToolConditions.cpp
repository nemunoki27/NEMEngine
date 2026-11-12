#include "TimelineTool.h"
#include "TimelineEditorFields.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Editor/UI/Inspectors/Common/TimelinePlayerFields.h>

// c++
#include <algorithm>

using namespace Engine;

void TimelineTool::DrawConditions(TimelineTrack& track) {

	if (!MyGUI::CollapsingHeader("条件")) return;
	ImGui::Indent();
	MyGUI::CheckboxField("すべての条件を満たす", track.conditionsAll);
	ImGui::BeginDisabled(track.fallback);
	for (size_t i = 0; i < track.conditions.size(); ++i) {
		auto& condition = track.conditions[i];
		ImGui::PushID(static_cast<int>(i));
		const char* kinds[]{"パラメーター", "対象との距離", "対象が存在する"};
		int kind = static_cast<int>(condition.kind);
		if (TimelineEditorFields::Select("条件", kind, kinds, 3).valueChanged) {
			condition.kind = static_cast<TimelineConditionKind>(kind);
			condition.value = true;
		}
		if (condition.kind == TimelineConditionKind::Parameter) {
			if (TimelineEditorFields::Parameter("パラメーター", condition.parameter, session_.GetAsset()).valueChanged) {
				for (const auto& parameter : session_.GetAsset().parameters)
					if (parameter.id == condition.parameter) condition.value = parameter.value;
			}
			TimelineEditorFields::ParameterValue("比較する値", condition.value);
		} else {
			TimelineEditorFields::Binding("対象", condition.firstBinding, session_.GetAsset());
			if (condition.kind == TimelineConditionKind::Distance) {
				TimelineEditorFields::Binding("相手", condition.secondBinding, session_.GetAsset());
				const char* planes[]{"3D", "XZ平面", "XY平面"};
				int plane = static_cast<int>(condition.plane);
				if (TimelineEditorFields::Select("計算する平面", plane, planes, 3).valueChanged)
					condition.plane = static_cast<TimelineDistancePlane>(plane);
				MyGUI::DragFloat("距離", condition.distance, {.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 1000000.0f});
			} else TimelineEditorFields::ParameterValue("存在している", condition.value);
		}
		const char* comparisons[]{"==", "!=", "<", "<=", ">", ">="};
		int comparison = static_cast<int>(condition.comparison);
		const int count = condition.kind == TimelineConditionKind::BindingExists ||
			(condition.kind == TimelineConditionKind::Parameter && condition.value.index() != 1 && condition.value.index() != 2) ? 2 : 6;
		comparison = (std::min)(comparison, count - 1);
		if (TimelineEditorFields::Select("比較", comparison, comparisons, count).valueChanged)
			condition.comparison = static_cast<TimelineComparison>(comparison);
		if (ImGui::Button("条件を削除", ImVec2(-1, 0))) {
			track.conditions.erase(track.conditions.begin() + i);
			ImGui::PopID();
			break;
		}
		ImGui::Separator();
		ImGui::PopID();
	}
	if (ImGui::Button("条件を追加", ImVec2(-1, 0))) track.conditions.push_back({});
	ImGui::EndDisabled();
	ImGui::Unindent();
}

void TimelineTool::DrawBindings(const EditorToolContext& context) {

	if (!MyGUI::CollapsingHeader("割り当て先")) return;
	ECSWorld* world = context.GetWorld();
	if (world && world->IsAlive(session_.GetOwner(context))) {
		DrawTimelineBindingFields(*world, session_.GetOwner(context), session_.GetPreviewSettings(), session_.GetAsset());
		ImGui::Separator();
	} else MyGUI::Text("対象", "再生対象を設定するとオブジェクトを割り当てできます");
	ImGui::Indent();
	auto& asset = session_.GetAsset();
	for (size_t i = 0; i < asset.bindings.size(); ++i) {
		auto& binding = asset.bindings[i];
		ImGui::PushID(ToString(binding.id).c_str());
		std::string label(TimelineEditorFields::BindingLabel(binding.name));
		if (MyGUI::CollapsingHeader(label.c_str(), false)) {
			ImGui::Indent();
			if (MyGUI::InputText("名前", label).valueChanged) binding.name = label;
			MyGUI::InputText("必要コンポーネント", binding.component);
			const bool used = std::any_of(asset.tracks.begin(), asset.tracks.end(), [&](const auto& track) {
				if (track.binding == binding.id) return true;
				for (const auto& clip : track.clips) {
					const std::string data = clip.data.dump();
					if (data.find("\"" + ToString(binding.id) + "\"") != std::string::npos ||
					data.find("\"" + binding.name + "\"") != std::string::npos) return true;
				}
				return std::any_of(track.conditions.begin(), track.conditions.end(), [&](const auto& condition) {
					return condition.firstBinding == binding.id || condition.secondBinding == binding.id;
				});
			});
			ImGui::BeginDisabled(used);
			const bool remove = ImGui::Button("割り当て先を削除", ImVec2(-1, 0));
			ImGui::EndDisabled();
			if (used) MyGUI::Text("使用中", "先にトラックと条件の割り当てを変更してください");
			ImGui::Unindent();
			if (remove) {
				std::erase_if(session_.GetPreviewSettings().bindings, [&](const auto& value) { return value.slot == binding.id; });
				asset.bindings.erase(asset.bindings.begin() + i);
				ImGui::PopID();
				break;
			}
		}
		ImGui::PopID();
	}
	if (ImGui::Button("割り当て先を追加", ImVec2(-1, 0))) {
		std::string name;
		for (size_t index = 1;; ++index) {
			name = "Target" + std::to_string(index);
			if (std::none_of(asset.bindings.begin(), asset.bindings.end(), [&](const auto& value) { return value.name == name; })) break;
		}
		asset.bindings.push_back({UUID::New(), name, ""});
	}
	ImGui::Unindent();
}

void TimelineTool::DrawParameters() {

	if (!MyGUI::CollapsingHeader("パラメーター")) return;
	auto& asset = session_.GetAsset();
	ImGui::Indent();
	for (size_t i = 0; i < asset.parameters.size(); ++i) {
		auto& parameter = asset.parameters[i];
		ImGui::PushID(ToString(parameter.id).c_str());
		if (MyGUI::CollapsingHeader(parameter.name.c_str(), false)) {
			ImGui::Indent();
			MyGUI::InputText("名前", parameter.name);
			const size_t previousType = parameter.value.index();
			TimelineEditorFields::ParameterValue("既定値", parameter.value, true);
			if (parameter.value.index() != previousType) {
				for (auto& track : asset.tracks) for (auto& condition : track.conditions)
					if (condition.kind == TimelineConditionKind::Parameter && condition.parameter == parameter.id) condition.value = parameter.value;
				for (auto& value : session_.GetPreviewSettings().parameters) if (value.parameter == parameter.id) value.value = parameter.value;
			}
			const bool remove = ImGui::Button("パラメーターを削除", ImVec2(-1, 0));
			ImGui::Unindent();
			if (remove) {
				for (auto& track : asset.tracks) std::erase_if(track.conditions,
					[&](const auto& condition) { return condition.kind == TimelineConditionKind::Parameter && condition.parameter == parameter.id; });
				std::erase_if(session_.GetPreviewSettings().parameters, [&](const auto& value) { return value.parameter == parameter.id; });
				asset.parameters.erase(asset.parameters.begin() + i);
				ImGui::PopID();
				break;
			}
		}
		ImGui::PopID();
	}
	if (ImGui::Button("パラメーターを追加", ImVec2(-1, 0))) {
		std::string name;
		for (size_t index = 1;; ++index) {
			name = "Parameter" + std::to_string(index);
			if (std::none_of(asset.parameters.begin(), asset.parameters.end(), [&](const auto& value) { return value.name == name; })) break;
		}
		asset.parameters.push_back({UUID::New(), name, 0.0f});
	}
	if (!asset.parameters.empty()) {
		ImGui::SeparatorText("プレビュー用の上書き");
		DrawTimelineParameterOverrides(session_.GetPreviewSettings(), asset);
	}
	ImGui::Unindent();
}
