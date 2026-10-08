#include "ParticleSystemInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>

// c++
#include <algorithm>
#include <string>

#include <imgui.h>

//============================================================================
//	ParticleSystemInspectorDrawer classMethods
//============================================================================
void Engine::ParticleSystemInspectorDrawer::DrawFields(
	const EditorPanelContext& context, ECSWorld& world,
	const Entity& entity, bool& anyItemActive) {

	ParticleSystemComponent& draft = GetDraft();
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawLayerMaskField(context, "Rendering Layer", draft.renderingLayerMask);
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("表示", draft.visible);
		});
	DrawField(anyItemActive, [&]() {
		AssetEditSetting setting{};
		setting.defaultAssetID = BuiltinAssets::Effects::DefaultParticle;
		return MyGUI::AssetReferenceField("エフェクト", draft.effect,
			context.editorContext->assetDatabase,
			{ AssetType::ParticleEffect }, setting);
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled);
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField(
			"開始時に再生", draft.playOnAwake);
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField(
			"編集中も再生", draft.playInEditMode);
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField(
			"非スケール時間", draft.useUnscaledTime);
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("再生速度", draft.playbackSpeed,
			{ .dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 100.0f });
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField(
			"Seedを自動生成", draft.useAutoRandomSeed);
		});
	if (!draft.useAutoRandomSeed) {
		DrawField(anyItemActive, [&]() {
			ValueEditResult result{};
			if (MyGUI::BeginPropertyRow("Seed")) {
				result.valueChanged = ImGui::InputScalar("##RandomSeed", ImGuiDataType_U32, &draft.randomSeed);
				result.anyItemActive = ImGui::IsItemActive();
				result.editFinished = ImGui::IsItemDeactivatedAfterEdit();
				MyGUI::EndPropertyRow();
			}
			return result;
			});
	}
	DrawField(anyItemActive, [&]() {
		return MyGUI::EnumCombo("シミュレーション空間", draft.simulationSpace);
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::EnumCombo("終了時の処理", draft.stopAction);
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragInt("レイヤー", draft.layer);
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragInt("描画順", draft.order);
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField(
			"エミッター形状を描画", draft.drawEmitterShape);
		});

	const float spacing = ImGui::GetStyle().ItemSpacing.x;
	const float width = (ImGui::GetContentRegionAvail().x - spacing * 4.0f) / 5.0f;
	if (ImGui::Button("再生", ImVec2(width, 0.0f))) {
		RequestParticleSystemPlay(world, entity);
	}
	ImGui::SameLine();
	if (ImGui::Button("単発再生", ImVec2(width, 0.0f))) {
		RequestParticleSystemRestart(world, entity, true);
	}
	ImGui::SameLine();
	if (ImGui::Button("一時停止", ImVec2(width, 0.0f))) {
		RequestParticleSystemPause(world, entity);
	}
	ImGui::SameLine();
	if (ImGui::Button("停止", ImVec2(width, 0.0f))) {
		RequestParticleSystemStop(world, entity);
	}
	ImGui::SameLine();
	if (ImGui::Button("消去", ImVec2(width, 0.0f))) {
		RequestParticleSystemClear(world, entity);
	}

	const ParticleSystemRuntimeData* runtime =
		TryGetParticleSystemRuntime(world, entity);
	if (!runtime) {
		ImGui::TextDisabled("実行状態なし");
	} else if (runtime->paused) {
		ImGui::Text("一時停止中");
	} else if (IsParticleSystemPlaying(world, entity)) {
		ImGui::Text("再生中");
	} else if (IsParticleSystemAlive(world, entity)) {
		ImGui::Text("停止処理中");
	} else {
		ImGui::Text("停止中");
	}
}
