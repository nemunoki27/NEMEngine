#include "EffectEmitterInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>

// imgui
#include <imgui.h>

// c++
#include <algorithm>

//============================================================================
//	EffectEmitterInspectorDrawer internal
//============================================================================
namespace {

	const char* ModeLabel(Engine::EffectEmitterMode mode) {

		switch (mode) {
		case Engine::EffectEmitterMode::Once:       return "一度だけ";
		case Engine::EffectEmitterMode::Continuous: return "連続発生";
		case Engine::EffectEmitterMode::Count:      return "回数指定";
		}
		return "";
	}

	Engine::ValueEditResult DrawModeField(Engine::EffectEmitterMode& mode) {

		Engine::ValueEditResult result{};
		if (!Engine::MyGUI::BeginPropertyRow("発生方法")) {
			return result;
		}
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		if (ImGui::BeginCombo("##Mode", ModeLabel(mode))) {

			for (const Engine::EffectEmitterMode value : { Engine::EffectEmitterMode::Once,
				Engine::EffectEmitterMode::Continuous, Engine::EffectEmitterMode::Count }) {

				const bool selected = mode == value;
				if (ImGui::Selectable(ModeLabel(value), selected)) {
					mode = value;
					result.valueChanged = true;
					result.editFinished = true;
				}
				if (selected) { ImGui::SetItemDefaultFocus(); }
			}
			ImGui::EndCombo();
		}
		result.anyItemActive = ImGui::IsItemActive();
		Engine::MyGUI::EndPropertyRow();
		return result;
	}
}

//============================================================================
//	EffectEmitterInspectorDrawer classMethods
//============================================================================
void Engine::EffectEmitterInspectorDrawer::DrawFields(const EditorPanelContext& context,
	ECSWorld& world, const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();

	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled);
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("開始時に再生", draft.playOnStart);
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("編集中も再生", draft.playInEditMode);
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("エミッター形状を描画", draft.drawEmitterShape);
		});

	if (!draft.groups.empty()) {

		std::vector<std::string> names{};
		names.reserve(draft.groups.size());
		for (const EffectEmitterGroup& group : draft.groups) { names.emplace_back(group.name); }
		DrawField(anyItemActive, [&]() {
			return MyGUI::StringCombo("開始グループ", draft.defaultGroup, names, "<なし>");
			});
	} else {
		DrawField(anyItemActive, [&]() { return MyGUI::InputText("開始グループ", draft.defaultGroup); });
	}
	DrawField(anyItemActive, [&]() { return MyGUI::DragInt("レイヤー", draft.layer); });
	DrawField(anyItemActive, [&]() { return MyGUI::DragInt("描画順", draft.order); });
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("表示", draft.visible);
		});

	ImGui::Indent();
	if (MyGUI::CollapsingHeader("エフェクトグループ")) {

		int32_t removeGroupIndex = -1;
		for (int32_t groupIndex = 0; groupIndex < static_cast<int32_t>(draft.groups.size()); ++groupIndex) {

			ImGui::PushID(groupIndex);
			EffectEmitterGroup& group = draft.groups[groupIndex];
			if (ImGui::TreeNodeEx("Group", ImGuiTreeNodeFlags_DefaultOpen, "グループ : %s",
				group.name.empty() ? "<名前なし>" : group.name.c_str())) {

				DrawField(anyItemActive, [&]() { return MyGUI::InputText("グループ名", group.name); });

				int32_t removeStateIndex = -1;
				ImGui::Indent();
				for (int32_t stateIndex = 0; stateIndex < static_cast<int32_t>(group.states.size()); ++stateIndex) {

					ImGui::PushID(stateIndex);
					EffectEmitterState& state = group.states[stateIndex];
					if (ImGui::TreeNodeEx("State", ImGuiTreeNodeFlags_DefaultOpen, "エフェクト : %s",
						state.name.empty() ? "<名前なし>" : state.name.c_str())) {

						DrawField(anyItemActive, [&]() {
							return InspectorDrawerCommon::DrawCheckboxField("有効", state.enabled);
							});
						DrawField(anyItemActive, [&]() { return MyGUI::InputText("名前", state.name); });
						DrawField(anyItemActive, [&]() {
							AssetEditSetting setting{};
							setting.defaultAssetID = BuiltinAssets::Effects::DefaultParticle;
							return MyGUI::AssetReferenceField("エフェクト", state.effect,
								context.editorContext->assetDatabase, { AssetType::ParticleEffect }, setting);
							});
						DrawField(anyItemActive, [&]() { return DrawModeField(state.mode); });
						DrawField(anyItemActive, [&]() {
							return MyGUI::DragFloat("開始遅延", state.delay,
								{ .dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 3600.0f });
							});
						if (state.mode == EffectEmitterMode::Count) {

							DrawField(anyItemActive, [&]() {
								return MyGUI::DragInt("発生回数", state.count,
									{ .dragSpeed = 1.0f, .minValue = 1, .maxValue = 100000 });
								});
							DrawField(anyItemActive, [&]() {
								return MyGUI::DragFloat("発生間隔", state.interval,
									{ .dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 3600.0f });
								});
						} else if (state.mode == EffectEmitterMode::Continuous) {
							DrawField(anyItemActive, [&]() {
								return MyGUI::DragFloat("発生時間", state.duration,
									{ .dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 3600.0f });
								});
						}
						DrawField(anyItemActive, [&]() { return MyGUI::DragVector3("ローカル座標", state.localPosition); });
						Vector3 localEuler = Quaternion::ToEulerAngles(state.localRotation);
						DrawField(anyItemActive, [&]() {
							auto result = MyGUI::DragVector3("ローカル回転", localEuler);
							if (result.valueChanged) {
								state.localRotation = Quaternion::Normalize(Quaternion::EulerToQuaternion(localEuler));
							}
							return result;
							});
						MyGUI::TextQuaternion("Quaternion", state.localRotation);
						ImGui::Separator();
						DrawField(anyItemActive, [&]() { return MyGUI::DragVector3("ローカルスケール", state.localScale); });

						if (ImGui::Button("エフェクトを削除", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
							removeStateIndex = stateIndex;
						}
						ImGui::TreePop();
					}
					ImGui::Separator();
					ImGui::PopID();
				}
				if (0 <= removeStateIndex) {
					group.states.erase(group.states.begin() + removeStateIndex);
					RequestCommit();
				}
				if (ImGui::Button("エフェクトを追加", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
					group.states.emplace_back();
					RequestCommit();
				}
				ImGui::Unindent();

				if (world.IsAlive(entity) && world.HasComponent<EffectEmitterComponent>(entity)) {

					EffectEmitterComponent& live = world.GetComponent<EffectEmitterComponent>(entity);
					if (ImGui::Button("Emit")) { live.Emit(group.name); }
					ImGui::SameLine();
					if (ImGui::Button("Stop")) { live.Stop(group.name); }
					ImGui::SameLine();
					if (ImGui::Button("Clear")) { live.Clear(group.name); }
				}
				if (ImGui::Button("グループを削除", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
					removeGroupIndex = groupIndex;
				}
				ImGui::TreePop();
			}
			ImGui::Separator();
			ImGui::PopID();
		}
		if (0 <= removeGroupIndex) {
			draft.groups.erase(draft.groups.begin() + removeGroupIndex);
			RequestCommit();
		}
		if (ImGui::Button("エフェクトグループを追加", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			EffectEmitterGroup group{};
			group.name = "Group " + std::to_string(draft.groups.size() + 1);
			draft.groups.emplace_back(std::move(group));
			RequestCommit();
		}
	}
	ImGui::Unindent();

	if (world.IsAlive(entity) && world.HasComponent<EffectEmitterComponent>(entity)) {

		const EffectEmitterComponent& live = world.GetComponent<EffectEmitterComponent>(entity);
		ImGui::Separator();
		ImGui::Text("再生数 : %zu", live.runtimePlaybacks.size());
		for (const EffectEmitterPlaybackRuntime& playback : live.runtimePlaybacks) {
			ImGui::Text("  %llu : %s%s", static_cast<unsigned long long>(playback.id),
				playback.groupName.c_str(), playback.stopped ? " (停止中)" : "");
		}
	}
}

void Engine::EffectEmitterInspectorDrawer::ApplyPreview(ECSWorld& world,
	const Entity& entity, const EffectEmitterComponent& previewComponent) {

	if (!world.IsAlive(entity) || !world.HasComponent<EffectEmitterComponent>(entity)) {
		return;
	}
	EffectEmitterComponent& live = world.GetComponent<EffectEmitterComponent>(entity);
	EffectEmitterComponent applied = previewComponent;
	applied.runtimeStarted = live.runtimeStarted;
	applied.runtimeNextPlaybackID = live.runtimeNextPlaybackID;
	applied.runtimeNextEffectInstanceID = live.runtimeNextEffectInstanceID;
	applied.runtimePlaybacks = std::move(live.runtimePlaybacks);
	applied.runtimeCommands = std::move(live.runtimeCommands);
	live = std::move(applied);
}
