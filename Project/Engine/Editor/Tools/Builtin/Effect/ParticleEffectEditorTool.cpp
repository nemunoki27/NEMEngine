#include "ParticleEffectEditorTool.h"

//============================================================================
//	include
//============================================================================
#include "ParticleEffectGroupDrawer.h"
#include "ParticleEffectPhaseDrawer.h"
#include "ParticleEffectPreviewOperations.h"
#include "GUI/ParticleGUIHelpers.h"
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

using namespace Engine;
using namespace Engine::ParticleEffectPreviewOperations;
using Engine::ParticleGUI::MakeDragSetting;

namespace {
	constexpr const char* kGroupReorderPayloadType = "PARTICLE_GROUP_REORDER";
}

void ParticleEffectEditorTool::OpenEditorTool() {

	openWindow_ = true;
}

void ParticleEffectEditorTool::OpenAsset(AssetID assetID) {

	pendingAsset_ = assetID;
	openWindow_ = true;
}

void ParticleEffectEditorTool::DrawEditorTool(const EditorToolContext& context) {

	if (pendingAsset_ && !session_.IsDirty() && !pendingConfirmation_) {
		session_.LoadEffect(context, *pendingAsset_, statusMessage_);
		pendingAsset_.reset();
	}
	if (!openWindow_) {
		return;
	}
	DrawWindow(context);
}

void ParticleEffectEditorTool::DrawWindow(const EditorToolContext& context) {

	const bool visible = ImGui::Begin("ParticleEffect", &openWindow_);
	if (!openWindow_ && session_.IsDirty()) {
		openWindow_ = true;
		pendingClose_ = true;
	}
	DrawPendingEdits(context);
	if (!visible) {

		session_.FinishEditing();
		ImGui::End();
		return;
	}
	ImGui::SetWindowFontScale(0.8f);
	DrawHistory(context);

	DrawAssetSection(context);
	if (!statusMessage_.empty()) {
		ImGui::TextWrapped("%s", statusMessage_.c_str());
	}

	if (session_.IsLoaded()) {

		// 変更検知フラグ
		bool changed = false;
		if (session_.GetDraft().groups.empty()) {

			session_.GetDraft().groups.emplace_back();
			session_.GetSelectedGroupID() = session_.GetDraft().groups.front().id;
			changed = true;
		}
		changed |= DrawGroupEmissionSection(context);
		ImGui::BeginChild("ParticleEffectGroupList", ImVec2(160.0f, 0.0f), true);
		changed |= DrawGroupList();
		ImGui::EndChild();
		ImGui::SameLine();
		ImGui::BeginChild("ParticleEffectGroupEdit", ImVec2(0.0f, 0.0f));
		if (ParticleEffectGroup* group = session_.GetSelectedGroup()) {

			ParticleGroupEditState& editorState = session_.GetGroupEditorState(group->id);
			if (ImGui::BeginTabBar("ParticleEffectEditorToolTabBar")) {

				// GroupとPhaseの表示を各Drawerへ渡す
				ParticleEffectGroupDrawer groupDrawer(session_, statusMessage_);
				ParticleEffectPhaseDrawer phaseDrawer(session_, statusMessage_, addModuleSearchFilter_);
				changed |= groupDrawer.Draw(context, *group);
				changed |= phaseDrawer.Draw(context, *group, editorState);
				ImGui::EndTabBar();
			}
		}
		ImGui::EndChild();

		// ドラッグ中の変更をまとめ、確定後に1件のUndoへ積む
		session_.UpdateEditing(changed, ImGui::IsAnyItemActive());
	}

	ImGui::SetWindowFontScale(1.0f);
	ImGui::End();
}

bool ParticleEffectEditorTool::DrawGroupEmissionSection(const EditorToolContext& context) {

	bool changed = false;
	if (MyGUI::CollapsingHeader("グループ発生設定", false)) {
		MyGUI::ScopedPropertyLabelWidth labelWidth("GroupEmissionSettings");

		bool playing = false;
		bool oneShot = false;
		float currentInterval = 0.0f;
		bool foundSystem = false;
		if (ECSWorld* world = context.GetWorld()) {
			world->ForEach<ParticleSystemComponent>(
				[&](const Entity& entity, const ParticleSystemComponent& component) {

					const ParticleEffectInstanceRuntime* effect = ResolveEffectInstance(
						*world, entity, component, session_.GetEditingID());
					if (!effect) {
						return;
					}
					if (!foundSystem) {
						currentInterval = effect->runtimeGroupEmitTimer;
						foundSystem = true;
					}
					playing |= !effect->emissionStopped;
					oneShot |= effect->oneShot;
				});
		}
		if (MyGUI::BeginPropertyRow("再生状態")) {

			ImGui::TextUnformatted(oneShot ? "単発再生中" : (playing ? "再生中" : "停止中"));
			MyGUI::EndPropertyRow();
		}

		if (MyGUI::BeginPropertyRow("発生方法")) {

			const char* labels[] = { "個別発生", "同時発生" };
			int32_t current = static_cast<int32_t>(session_.GetDraft().groupEmission.mode);
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
			if (ImGui::Combo("##Value", &current, labels, IM_ARRAYSIZE(labels))) {

				session_.GetDraft().groupEmission.mode = static_cast<ParticleEffectGroupEmissionMode>(current);
				changed = true;
			}
			MyGUI::EndPropertyRow();
		}
		if (session_.GetDraft().groupEmission.mode == ParticleEffectGroupEmissionMode::Simultaneous) {

			changed |= MyGUI::Checkbox("全グループの終了を待つ", session_.GetDraft().groupEmission.waitForCompletion);
			changed |= MyGUI::DragFloat("同時発生間隔", session_.GetDraft().groupEmission.interval,
				MakeDragSetting(0.0f, 60.0f)).valueChanged;
			if (MyGUI::BeginPropertyRow("現在の発生間隔")) {

				ImGui::Text("%.3f / %.3f", currentInterval, session_.GetDraft().groupEmission.interval);
				MyGUI::EndPropertyRow();
			}
		}
	}
	if (ImGui::Button("再生")) {
		RestartParticleSystems(context, session_.GetEditingID(), statusMessage_, false);
	}
	ImGui::SameLine();
	if (ImGui::Button("単発再生")) {
		RestartParticleSystems(context, session_.GetEditingID(), statusMessage_, true);
	}
	ImGui::SameLine();
	if (ImGui::Button("停止")) {
		StopParticleSystems(context, session_.GetEditingID(), statusMessage_);
	}
	ImGui::Separator();
	return changed;
}

bool ParticleEffectEditorTool::DrawGroupList() {

	bool changed = false;
	if (ImGui::Button("追加", ImVec2(-FLT_MIN, 0.0f))) {

		ParticleEffectGroup group{};
		group.name = "Group " + std::to_string(session_.GetDraft().groups.size() + 1);
		ParticleEffectPhase phase{};
		phase.name = "Phase 1";
		phase.modules = {
			{ "SizeOverLifetime", nlohmann::json::object() },
			{ "ColorOverLifetime", nlohmann::json::object() },
		};
		group.phases.emplace_back(std::move(phase));
		session_.GetDraft().groups.emplace_back(std::move(group));
		session_.GetSelectedGroupID() = session_.GetDraft().groups.back().id;
		changed = true;
	}
	ParticleEffectGroup* selected = session_.GetSelectedGroup();
	ImGui::BeginDisabled(!selected);
	if (ImGui::Button("複製", ImVec2(-FLT_MIN, 0.0f)) && selected) {

		ParticleEffectGroup copy = *selected;
		copy.id = UUID::New();
		for (ParticleEffectPhase& phase : copy.phases) {
			phase.id = UUID::New();
			for (ParticleEffectModuleEntry& module : phase.modules) {
				module.instanceID = UUID::New();
			}
		}
		copy.name += " Copy";
		session_.GetDraft().groups.emplace_back(std::move(copy));
		session_.GetSelectedGroupID() = session_.GetDraft().groups.back().id;
		changed = true;
	}
	ImGui::EndDisabled();
	ImGui::Separator();

	int32_t removeIndex = -1;
	for (int32_t i = 0; i < static_cast<int32_t>(session_.GetDraft().groups.size()); ++i) {

		ParticleEffectGroup& group = session_.GetDraft().groups[i];
		const std::string groupID = ToString(group.id);
		ImGui::PushID(groupID.c_str());
		if (MyGUI::SmallCheckbox("##Enabled", group.enabled)) { changed = true; }
		ImGui::SameLine();
		const std::string label = std::to_string(i + 1) + ": " + group.name;
		if (ImGui::Selectable(label.c_str(), group.id == session_.GetSelectedGroupID())) {
			session_.GetSelectedGroupID() = group.id;
		}
		if (ImGui::BeginDragDropSource()) {

			ImGui::SetDragDropPayload(kGroupReorderPayloadType, &i, sizeof(i));
			ImGui::TextUnformatted(label.c_str());
			ImGui::EndDragDropSource();
		}
		if (ImGui::BeginDragDropTarget()) {

			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kGroupReorderPayloadType)) {

				const int32_t from = *static_cast<const int32_t*>(payload->Data);
				if (from != i) {
					Algorithm::MoveListItem(session_.GetDraft().groups, from, i);
					changed = true;
				}
			}
			ImGui::EndDragDropTarget();
		}
		if (ImGui::BeginPopupContextItem()) {

			if (ImGui::MenuItem("削除", nullptr, false, 1 < session_.GetDraft().groups.size())) {
				removeIndex = i;
			}
			ImGui::EndPopup();
		}
		ImGui::PopID();
	}
	if (0 <= removeIndex) {

		const UUID removedID = session_.GetDraft().groups[removeIndex].id;
		session_.GetDraft().groups.erase(session_.GetDraft().groups.begin() + removeIndex);
		session_.RemoveGroupState(removedID);
		if (removedID == session_.GetSelectedGroupID()) {
			session_.GetSelectedGroupID() = session_.GetDraft().groups[(std::min)(removeIndex,
				static_cast<int32_t>(session_.GetDraft().groups.size()) - 1)].id;
		}
		changed = true;
	}
	return changed;
}

void ParticleEffectEditorTool::DrawAssetSection(const EditorToolContext& context) {

	AssetDatabase* assetDatabase = context.toolContext.assetDatabase;

	// 編集対象のエフェクトを選択する
	AssetID selected = session_.GetEditingID();
	AssetEditSetting setting{};
	setting.defaultAssetID = BuiltinAssets::Effects::DefaultParticle;
	if (MyGUI::AssetReferenceField("エフェクト", selected,
		assetDatabase, { AssetType::ParticleEffect }, setting).valueChanged) {

		if (selected != session_.GetEditingID()) pendingAsset_ = selected;
	}

	// 新規作成、GameAssets/Effects配下へ作成する
	ImGui::Separator();
	MyGUI::InputText("GameAssets/Effects/", createNameBuffer_);
	const bool canCreate = !createNameBuffer_.empty();
	ImGui::BeginDisabled(!canCreate);
	if (ImGui::Button("新規作成")) {
		if (session_.IsDirty()) {
			pendingCreate_ = createNameBuffer_;
		} else {
			session_.CreateEffect(context, statusMessage_, createNameBuffer_);
		}
	}
	ImGui::EndDisabled();

	// 保存、編集は保存前でも即シーンへ反映される
	ImGui::SameLine();
	ImGui::BeginDisabled(!session_.IsLoaded());
	if (ImGui::Button("保存")) {
		session_.SaveEffect(context, statusMessage_);
	}
	ImGui::EndDisabled();
	ImGui::Separator();
}
