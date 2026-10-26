#include "ParticleFeedbackDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Common/SceneEntityReferenceFields.h>
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>

using namespace Engine;

bool ParticleFeedbackDrawer::Draw(const EditorPanelContext& context, ECSWorld& world,
	Entity owner, ParticleFeedbackSettings& settings) {

	if (!ImGui::BeginTabItem("GPUイベント")) return false;
	bool changed = false;
	int32_t capacity = static_cast<int32_t>(settings.eventCapacity);
	if (MyGUI::DragInt("イベント上限", capacity, {.minValue = 1, .maxValue = 65536}).valueChanged) {
		settings.eventCapacity = static_cast<uint32_t>(capacity);
		changed = true;
	}
	ImGui::TextDisabled("別Systemの粒子は次回更新に発生します");
	for (size_t index = 0; index < settings.events.size();) {
		auto& event = settings.events[index];
		ImGui::PushID(static_cast<int>(index));
		if (MyGUI::CollapsingHeader("発生経路")) {
			changed |= MyGUI::Checkbox("有効", event.enabled);
			if (MyGUI::BeginPropertyRow("発生契機")) {
				const char* kinds[]{"衝突", "消滅", "経過時間", "フェーズ終了"};
				int kind = static_cast<int>(event.kind);
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
				if (ImGui::Combo("##EventKind", &kind, kinds, 4)) {
					event.kind = static_cast<ParticleEventKind>(kind);
					changed = true;
				}
				MyGUI::EndPropertyRow();
			}
			if (event.kind == ParticleEventKind::PhaseTransition) ImGui::TextDisabled("選択フェーズの終了時に発生します");
			changed |= SceneEntityReferenceFields::Draw("発生先System", world, owner, event.target).valueChanged;
			int32_t phase = event.phase == UINT32_MAX ? -1 : static_cast<int32_t>(event.phase);
			if (MyGUI::DragInt("フェーズ番号", phase, {.minValue = -1, .maxValue = 255}).valueChanged) {
				event.phase = phase < 0 ? UINT32_MAX : static_cast<uint32_t>(phase);
				changed = true;
			}
			int32_t count = static_cast<int32_t>(event.count);
			if (MyGUI::DragInt("発生数", count, {.minValue = 1, .maxValue = 256}).valueChanged) {
				event.count = static_cast<uint32_t>(count);
				changed = true;
			}
			if (event.kind == ParticleEventKind::Time) changed |= MyGUI::DragFloat("経過秒", event.time, {.minValue = 0.0f}).valueChanged;
			changed |= MyGUI::DragFloat("発生確率", event.probability, {.minValue = 0.0f, .maxValue = 1.0f}).valueChanged;
			changed |= MyGUI::Checkbox("位置を継承", event.inheritPosition);
			changed |= MyGUI::Checkbox("法線を継承", event.inheritNormal);
			changed |= MyGUI::Checkbox("速度を継承", event.inheritVelocity);
			changed |= MyGUI::Checkbox("色を継承", event.inheritColor);
			changed |= MyGUI::Checkbox("Seedを継承", event.inheritSeed);
			if (ImGui::Button("発生経路を削除")) {
				settings.events.erase(settings.events.begin() + index);
				changed = true;
				ImGui::PopID();
				continue;
			}
		}
		ImGui::PopID();
		++index;
	}
	if (settings.events.size() < ParticleFeedbackSettings::kMaxRoutes && ImGui::Button("発生経路を追加")) {
		settings.events.emplace_back();
		changed = true;
	}
	if (MyGUI::CollapsingHeader("実行時の塗装")) {
		changed |= MyGUI::Checkbox("塗装する", settings.paint.enabled);
		changed |= MyGUI::DragFloat("半径", settings.paint.radius, {.minValue = 0.001f}).valueChanged;
		changed |= MyGUI::DragFloat("輪郭の硬さ", settings.paint.hardness, {.minValue = 0.0f, .maxValue = 1.0f}).valueChanged;
		changed |= MyGUI::ColorEdit("塗装色", settings.paint.color).valueChanged;
		ImGui::TextDisabled("受け取り面にParticlePaintReceiverを追加してください");
	}
	if (MyGUI::CollapsingHeader("衝突デカール")) {
		changed |= MyGUI::Checkbox("デカールを残す", settings.decal.enabled);
		changed |= MyGUI::AssetReferenceField("マテリアル", settings.decal.material,
			context.editorContext->assetDatabase, {AssetType::Material}).valueChanged;
		changed |= MyGUI::DragVector3("大きさと奥行き", settings.decal.size, {.minValue = 0.001f}).valueChanged;
		changed |= MyGUI::ColorEdit("乗算色", settings.decal.color).valueChanged;
		changed |= MyGUI::DragFloat("保持秒数", settings.decal.lifetime, {.minValue = 0.001f}).valueChanged;
		changed |= MyGUI::DragFloat("消滅フェード秒数", settings.decal.fadeDuration, {.minValue = 0.0f}).valueChanged;
		changed |= InspectorDrawerCommon::DrawLayerMaskField(context, "受け取り先レイヤー",
			settings.decal.receiverRenderingLayerMask).valueChanged;
		int32_t count = static_cast<int32_t>(settings.decal.capacity);
		if (MyGUI::DragInt("デカール上限", count, {.minValue = 1, .maxValue = 16384}).valueChanged) {
			settings.decal.capacity = static_cast<uint32_t>(count);
			changed = true;
		}
	}
	if (changed) SanitizeParticleFeedbackSettings(settings);
	ImGui::EndTabItem();
	return changed;
}
