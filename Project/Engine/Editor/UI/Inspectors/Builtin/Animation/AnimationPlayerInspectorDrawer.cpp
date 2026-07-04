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
		}
		return "";
	}

	// 回数の編集行、0のときは数値ではなく「無限」と表示する
	Engine::ValueEditResult DrawRepeatCountField(const char* label, int32_t& count) {

		Engine::ValueEditResult result{};
		if (!Engine::MyGUI::BeginPropertyRow(label)) {
			return result;
		}
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		// formatに%dが無いとImGuiはその文字列をそのまま表示するので、0のときだけ「無限」にする
		const char* format = count <= 0 ? "無限" : "%d";
		if (ImGui::DragInt("##RepeatCount", &count, 1.0f, 0, 9999, format)) {
			count = (std::max)(count, 0);
			result.valueChanged = true;
		}
		result.anyItemActive = ImGui::IsItemActive();
		result.editFinished = ImGui::IsItemDeactivatedAfterEdit();
		Engine::MyGUI::EndPropertyRow();
		return result;
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

	// 開始グループは候補があれば名前から選び、無ければ手入力にする、Playはこのグループ名で行う
	if (!draft.groups.empty()) {

		std::vector<std::string> names;
		names.reserve(draft.groups.size());
		for (const AnimationGroup& group : draft.groups) {
			names.push_back(group.name);
		}
		DrawField(anyItemActive, [&]() {
			return MyGUI::StringCombo("開始グループ", draft.defaultGroup, names, "<なし>");
			});
	} else {
		DrawField(anyItemActive, [&]() {
			return MyGUI::InputText("開始グループ", draft.defaultGroup);
			});
	}

	ImGui::Separator();

	// グループ内クリップ1つ分の設定を描く
	auto drawStateBody = [&](AnimationState& state) {

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
			return InspectorDrawerCommon::DrawCheckboxField("向き相対", state.relativeTransform);
			});
		DrawField(anyItemActive, [&]() {
			// グループPlay時にこの秒数だけ待ってから再生を始める
			return MyGUI::DragFloat("開始遅延", state.startDelay,
				{ .dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 100.0f });
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
					AnimationWrapMode::Loop, AnimationWrapMode::PingPong }) {

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

		// ループ時のみ繋ぎ補間とループ回数を出す
		if (state.wrapMode == AnimationWrapMode::Loop) {

			DrawField(anyItemActive, [&]() {
				return InspectorDrawerCommon::DrawCheckboxField("ループの繋ぎ補間", state.loopBridge.enabled);
				});
			if (state.loopBridge.enabled) {

				DrawField(anyItemActive, [&]() {
					return MyGUI::DragFloat("補間時間", state.loopBridge.duration,
						{ .dragSpeed = 0.001f, .minValue = 0.001f, .maxValue = 10.0f });
					});
				DrawField(anyItemActive, [&]() {
					return MyGUI::EnumCombo<CurveInterpolationMode>("補間方法", state.loopBridge.interpolation, {});
					});
			}
			DrawField(anyItemActive, [&]() {
				return DrawRepeatCountField("ループ回数", state.loopCount);
				});
			DrawField(anyItemActive, [&]() {
				// 繋ぎ補間の後、次の再生までの待機時間
				return MyGUI::DragFloat("インターバル", state.interval,
					{ .dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 100.0f });
				});
		}
		// 往復時のみ往復回数を出す
		else if (state.wrapMode == AnimationWrapMode::PingPong) {

			DrawField(anyItemActive, [&]() {
				return DrawRepeatCountField("往復回数", state.pingPongCount);
				});
			DrawField(anyItemActive, [&]() {
				// 1往復ごとに、次の往復までの待機時間
				return MyGUI::DragFloat("インターバル", state.interval,
					{ .dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 100.0f });
				});
		}
		};

	//============================================================================
	//	アニメーショングループ一覧
	//============================================================================
	ImGui::Indent();
	if (MyGUI::CollapsingHeader("アニメーショングループ")) {

		int32_t removeGroupIndex = -1;
		for (uint32_t gi = 0; gi < static_cast<uint32_t>(draft.groups.size()); ++gi) {

			ImGui::PushID(static_cast<int32_t>(gi));
			AnimationGroup& group = draft.groups[gi];
			if (ImGui::TreeNodeEx("Group", ImGuiTreeNodeFlags_DefaultOpen, "グループ : %s",
				group.name.empty() ? "<名前なし>" : group.name.c_str())) {

				DrawField(anyItemActive, [&]() {
					return MyGUI::InputText("グループ名", group.name);
					});

				// グループ内のクリップ一覧
				ImGui::Indent();
				int32_t removeClipIndex = -1;
				for (uint32_t ci = 0; ci < static_cast<uint32_t>(group.states.size()); ++ci) {

					ImGui::PushID(static_cast<int32_t>(ci));
					AnimationState& state = group.states[ci];
					if (ImGui::TreeNodeEx("Anim", ImGuiTreeNodeFlags_DefaultOpen, "アニメーション : %s",
						state.name.empty() ? "<名前なし>" : state.name.c_str())) {

						drawStateBody(state);
						if (ImGui::Button("アニメーションを削除", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
							removeClipIndex = static_cast<int32_t>(ci);
						}
						ImGui::TreePop();
					}
					ImGui::Separator();
					ImGui::PopID();
				}
				if (0 <= removeClipIndex && removeClipIndex < static_cast<int32_t>(group.states.size())) {
					group.states.erase(group.states.begin() + removeClipIndex);
					RequestCommit();
				}
				if (ImGui::Button("アニメーションを追加", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
					group.states.push_back(AnimationState{});
					RequestCommit();
				}
				ImGui::Unindent();

				if (ImGui::Button("グループを削除", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
					removeGroupIndex = static_cast<int32_t>(gi);
				}
				ImGui::TreePop();
			}
			ImGui::Separator();
			ImGui::PopID();
		}

		if (0 <= removeGroupIndex && removeGroupIndex < static_cast<int32_t>(draft.groups.size())) {
			draft.groups.erase(draft.groups.begin() + removeGroupIndex);
			RequestCommit();
		}
		if (ImGui::Button("アニメーショングループを追加", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			draft.groups.push_back(AnimationGroup{});
			RequestCommit();
		}
	}
	ImGui::Unindent();

	ImGui::Separator();

	//============================================================================
	//	デバッグ表示
	//============================================================================
	ImGui::Text("再生中グループ : %s", draft.runtimeCurrent.c_str());
	// 現在グループのクリップごとの状態を出す
	for (const AnimationClipRuntime& clipRt : draft.runtimeCurrentClips) {

		const char* status = clipRt.started ? (clipRt.playing ? "" : "  (終了)") : "  (遅延中)";
		ImGui::Text("  %s : 時間 %.3f  回数 %d%s",
			clipRt.stateName.c_str(), clipRt.time, clipRt.repeatCount, status);
	}
}
