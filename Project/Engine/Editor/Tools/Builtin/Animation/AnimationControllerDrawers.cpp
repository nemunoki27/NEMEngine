#include "AnimationControllerTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

// c++
#include <algorithm>

namespace {

	// 同じ一覧で重ならない名前を作る
	template<typename T>
	std::string MakeName(const std::vector<T>& values, const char* prefix) {

		for (size_t index = 1;; ++index) {

			std::string name = prefix + std::to_string(index);
			if (std::none_of(values.begin(), values.end(), [&](const auto& value) { return value.name == name; })) return name;
		}
	}
}

//============================================================================
//	AnimationControllerTool classMethods
//============================================================================
void Engine::AnimationControllerTool::DrawStates(AssetDatabase& database) {

	if (!ImGui::CollapsingHeader("状態", ImGuiTreeNodeFlags_DefaultOpen)) return;
	auto& draft = session_.GetDraft();
	if (ImGui::Button("状態を追加")) {

		AnimationGroup group;
		group.name = MakeName(draft.states, "State");
		if (draft.states.empty()) draft.defaultState = group.name;
		draft.states.push_back(std::move(group));
		session_.MarkModified();
	}
	for (size_t index = 0; index < draft.states.size(); ++index) {

		ImGui::PushID(static_cast<int>(index));
		auto& group = draft.states[index];
		bool changed = false;
		const std::string previous = group.name;
		if (MyGUI::InputText("状態名", group.name).valueChanged) {

			// 状態名の変更を開始状態と遷移へ反映する
			if (draft.defaultState == previous) draft.defaultState = group.name;
			for (auto& transition : draft.transitions) {
				if (!transition.from.empty() && transition.from == previous) transition.from = group.name;
				if (transition.to == previous) transition.to = group.name;
			}
			changed = true;
		}
		if (ImGui::RadioButton("開始状態", draft.defaultState == group.name)) {

			draft.defaultState = group.name;
			changed = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("状態を削除")) {

			const std::string removed = group.name;
			draft.states.erase(draft.states.begin() + index);
			std::erase_if(draft.transitions, [&](const auto& transition) { return transition.from == removed || transition.to == removed; });
			if (draft.defaultState == removed) draft.defaultState = draft.states.empty() ? std::string() : draft.states.front().name;
			session_.MarkModified();
			ImGui::PopID();
			break;
		}
		if (ImGui::Button("Clipを追加")) {

			AnimationState state;
			state.name = MakeName(group.states, "Clip");
			group.states.push_back(std::move(state));
			changed = true;
		}
		for (size_t clipIndex = 0; clipIndex < group.states.size(); ++clipIndex) {

			ImGui::PushID(static_cast<int>(clipIndex));
			auto& state = group.states[clipIndex];
			changed |= MyGUI::InputText("Clip名", state.name).valueChanged;
			changed |= MyGUI::AssetReferenceField("Clip", state.clip, &database, { AssetType::AnimationClip }, {}).valueChanged;
			changed |= ImGui::DragFloat("速度", &state.speed, 0.01f);
			changed |= ImGui::SliderFloat("Weight", &state.weight, 0.0f, 1.0f);
			changed |= ImGui::InputInt("Priority", &state.priority);
			changed |= ImGui::Checkbox("Additive", &state.additive);
			changed |= ImGuiUtility::EnumCombo("Wrap", &state.wrapMode);
			changed |= ImGui::Checkbox("向き相対", &state.relativeTransform);
			changed |= ImGui::DragFloat("開始遅延", &state.startDelay, 0.01f, 0.0f, 3600.0f);
			changed |= ImGui::DragFloat("間隔", &state.interval, 0.01f, 0.0f, 3600.0f);
			changed |= ImGui::InputInt("ループ回数", &state.loopCount);
			changed |= ImGui::InputInt("往復回数", &state.pingPongCount);
			changed |= ImGui::Checkbox("ループ繋ぎ補間", &state.loopBridge.enabled);
			if (state.loopBridge.enabled) {

				changed |= ImGui::DragFloat("繋ぎ時間", &state.loopBridge.duration, 0.01f, 0.0f, 3600.0f);
			}
			if (ImGui::Button("Clipを削除")) {

				group.states.erase(group.states.begin() + clipIndex);
				changed = true;
				ImGui::PopID();
				break;
			}
			ImGui::Separator();
			ImGui::PopID();
		}
		if (changed) session_.MarkModified();
		ImGui::Separator();
		ImGui::PopID();
	}
}

void Engine::AnimationControllerTool::DrawParameters() {

	if (!ImGui::CollapsingHeader("Parameter")) return;
	auto& draft = session_.GetDraft();
	if (ImGui::Button("Parameterを追加")) {

		AnimationControllerParameter parameter;
		parameter.name = MakeName(draft.parameters, "Parameter");
		draft.parameters.push_back(std::move(parameter));
		session_.MarkModified();
	}
	for (size_t index = 0; index < draft.parameters.size(); ++index) {

		ImGui::PushID(static_cast<int>(index));
		auto& parameter = draft.parameters[index];
		const std::string previous = parameter.name;
		bool changed = MyGUI::InputText("名前", parameter.name).valueChanged;
		if (changed) {

			for (auto& transition : draft.transitions) {
				for (auto& condition : transition.conditions) {
					if (condition.parameter == previous) condition.parameter = parameter.name;
				}
			}
		}
		if (ImGuiUtility::EnumCombo("型", &parameter.type)) {

			// 型変更で旧型の初期値を持ち越さない
			parameter.defaultValue = parameter.type == AnimationControllerParameterType::Float ? AnimationControllerParameterValue(0.0f) :
				parameter.type == AnimationControllerParameterType::Integer ? AnimationControllerParameterValue(int32_t(0)) : AnimationControllerParameterValue(false);
			changed = true;
		}
		if (auto* floatValue = std::get_if<float>(&parameter.defaultValue)) changed |= ImGui::DragFloat("初期値", floatValue, 0.01f);
		else if (auto* integerValue = std::get_if<int32_t>(&parameter.defaultValue)) changed |= ImGui::InputInt("初期値", integerValue);
		else if (parameter.type != AnimationControllerParameterType::Trigger) changed |= ImGui::Checkbox("初期値", &std::get<bool>(parameter.defaultValue));
		if (ImGui::Button("Parameterを削除")) {

			draft.parameters.erase(draft.parameters.begin() + index);
			// 条件は保持し、参照欠損を保存時に知らせる
			session_.MarkModified();
			ImGui::PopID();
			break;
		}
		if (changed) session_.MarkModified();
		ImGui::Separator();
		ImGui::PopID();
	}
}

void Engine::AnimationControllerTool::DrawTransitions() {

	if (!ImGui::CollapsingHeader("遷移")) return;
	auto& draft = session_.GetDraft();
	if (ImGui::Button("遷移を追加")) {

		AnimationControllerTransition transition;
		transition.to = draft.defaultState;
		draft.transitions.push_back(std::move(transition));
		session_.MarkModified();
	}
	for (size_t index = 0; index < draft.transitions.size(); ++index) {

		ImGui::PushID(static_cast<int>(index));
		auto& transition = draft.transitions[index];
		bool changed = MyGUI::InputText("遷移元（空欄はAny State）", transition.from).valueChanged;
		changed |= MyGUI::InputText("遷移先", transition.to).valueChanged;
		changed |= ImGui::DragFloat("時間（秒）", &transition.duration, 0.01f, 0.0f, 3600.0f);
		changed |= ImGui::Checkbox("Exit Time", &transition.hasExitTime);
		if (transition.hasExitTime) changed |= ImGui::DragFloat("終了時刻（正規化）", &transition.exitTime, 0.01f, 0.0f, 1000.0f);
		changed |= ImGui::Checkbox("同じ状態へ遷移", &transition.canTransitionToSelf);
		if (ImGui::Button("条件を追加")) {

			transition.conditions.emplace_back();
			changed = true;
		}
		for (size_t conditionIndex = 0; conditionIndex < transition.conditions.size(); ++conditionIndex) {

			ImGui::PushID(static_cast<int>(conditionIndex));
			auto& condition = transition.conditions[conditionIndex];
			changed |= MyGUI::InputText("Parameter", condition.parameter).valueChanged;
			changed |= ImGuiUtility::EnumCombo("比較", &condition.mode);
			if (condition.mode != AnimationControllerConditionMode::If && condition.mode != AnimationControllerConditionMode::IfNot) {
				changed |= ImGui::DragFloat("値", &condition.threshold, 0.01f);
			}
			if (ImGui::Button("条件を削除")) {

				transition.conditions.erase(transition.conditions.begin() + conditionIndex);
				changed = true;
				ImGui::PopID();
				break;
			}
			ImGui::PopID();
		}
		if (ImGui::Button("遷移を削除")) {

			draft.transitions.erase(draft.transitions.begin() + index);
			session_.MarkModified();
			ImGui::PopID();
			break;
		}
		if (changed) session_.MarkModified();
		ImGui::Separator();
		ImGui::PopID();
	}
}
