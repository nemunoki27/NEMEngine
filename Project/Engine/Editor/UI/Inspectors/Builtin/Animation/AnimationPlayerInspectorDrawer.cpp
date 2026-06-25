#include "AnimationPlayerInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>

// imgui
#include <imgui.h>

// c++
#include <string>
#include <vector>

//============================================================================
//	AnimationPlayerInspectorDrawer helpers
//============================================================================
namespace {

	// wrapModeの表示名、シリアライズはenum名のままで表示だけ日本語にする
	const char* WrapModeLabel(Engine::AnimationWrapMode mode) {

		switch (mode) {
		case Engine::AnimationWrapMode::UseClip:      return "クリップ設定に従う";
		case Engine::AnimationWrapMode::Once:         return "一度だけ";
		case Engine::AnimationWrapMode::Loop:         return "ループ";
		case Engine::AnimationWrapMode::PingPong:     return "往復";
		case Engine::AnimationWrapMode::ClampForever: return "終端で停止";
		}
		return "";
	}
}

//============================================================================
//	AnimationPlayerInspectorDrawer classMethods
//============================================================================
void Engine::AnimationPlayerInspectorDrawer::DrawFields(const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();

	//============================================================================
	//	基本設定
	//============================================================================
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled);
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("開始時に再生", draft.playOnStart);
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("編集中に再生", draft.playInEditMode);
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("全体速度", draft.globalSpeed,
			{ .dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 100.0f });
		});

	// 開始アニメーションは候補があれば名前から選び、無ければ手入力にする
	if (!draft.states.empty()) {

		std::vector<std::string> names;
		names.reserve(draft.states.size());
		for (const AnimationState& state : draft.states) {
			names.push_back(state.name);
		}
		DrawField(anyItemActive, [&]() {
			return MyGUI::StringCombo("開始アニメーション", draft.defaultState, names, "<なし>");
			});
	} else {
		DrawField(anyItemActive, [&]() {
			return MyGUI::InputText("開始アニメーション", draft.defaultState);
			});
	}

	ImGui::Separator();

	//============================================================================
	//	アニメーション一覧
	//============================================================================
	ImGui::Indent();
	if (MyGUI::CollapsingHeader("アニメーション一覧")) {

		int32_t removeIndex = -1;
		for (uint32_t i = 0; i < static_cast<uint32_t>(draft.states.size()); ++i) {

			ImGui::PushID(static_cast<int32_t>(i));
			AnimationState& state = draft.states[i];
			if (ImGui::TreeNodeEx("Anim", ImGuiTreeNodeFlags_DefaultOpen, "アニメーション : %s",
				state.name.empty() ? "<名前なし>" : state.name.c_str())) {

				DrawField(anyItemActive, [&]() {
					return MyGUI::InputText("名前", state.name);
					});
				DrawField(anyItemActive, [&]() {
					return MyGUI::AssetReferenceField("クリップ", state.clip,
						context.editorContext->assetDatabase, { AssetType::AnimationClip });
					});
				DrawField(anyItemActive, [&]() {
					return MyGUI::DragFloat("再生速度", state.speed,
						{ .dragSpeed = 0.01f, .minValue = -100.0f, .maxValue = 100.0f });
					});
				DrawField(anyItemActive, [&]() {
					// 表示は日本語にするが値はenumのまま保持する
					ValueEditResult result{};
					if (!MyGUI::BeginPropertyRow("ループ形式")) {
						return result;
					}
					ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
					if (ImGui::BeginCombo("##WrapMode", WrapModeLabel(state.wrapMode))) {

						for (const AnimationWrapMode mode : { AnimationWrapMode::UseClip, AnimationWrapMode::Once,
							AnimationWrapMode::Loop, AnimationWrapMode::PingPong, AnimationWrapMode::ClampForever }) {

							const bool selected = state.wrapMode == mode;
							if (ImGui::Selectable(WrapModeLabel(mode), selected)) {
								state.wrapMode = mode;
								result.valueChanged = true;
								result.editFinished = true;
							}
							if (selected) {
								ImGui::SetItemDefaultFocus();
							}
						}
						ImGui::EndCombo();
					}
					result.anyItemActive = ImGui::IsItemActive();
					MyGUI::EndPropertyRow();
					return result;
					});
				if (ImGui::Button("アニメーションを削除", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
					removeIndex = static_cast<int32_t>(i);
				}
				ImGui::TreePop();
			}
			ImGui::Separator();
			ImGui::PopID();
		}

		if (0 <= removeIndex && removeIndex < static_cast<int32_t>(draft.states.size())) {
			draft.states.erase(draft.states.begin() + removeIndex);
			RequestCommit();
		}
		if (ImGui::Button("アニメーションを追加", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			draft.states.push_back(AnimationState{});
			RequestCommit();
		}
	}
	ImGui::Unindent();

	ImGui::Separator();

	//============================================================================
	//	デバッグ表示
	//============================================================================
	ImGui::Text("再生中 : %s", draft.runtimeCurrent.c_str());
	ImGui::Text("再生時間 : %.3f", draft.runtimeTime);
	ImGui::Text("フェード : %.3f", draft.runtimeFade);
}
