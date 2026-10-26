#include "ParticleSystemInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Editor/UI/Inspectors/Particle/ParticleEmitterDrawer.h>
#include <Engine/Editor/UI/Inspectors/Particle/ParticlePhaseDrawer.h>
#include <Engine/Editor/UI/Inspectors/Particle/ParticleFeedbackDrawer.h>
#include <Engine/Core/Rendering/Particle/ParticleGPUState.h>
#include <Engine/Core/Rendering/Particle/ParticleGPUTrailTypes.h>

// c++
#include <algorithm>
#include <string>
#include <bit>

#include <imgui.h>

//============================================================================
//	ParticleSystemInspectorDrawer classMethods
//============================================================================
void Engine::ParticleSystemInspectorDrawer::DrawFields(
	const EditorPanelContext& context, ECSWorld& world,
	const Entity& entity, bool& anyItemActive) {

	ParticleSystemComponent& draft = GetDraft();
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawLayerMaskField(context, "表示レイヤー", draft.renderingLayerMask);
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("表示", draft.visible);
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
	const float width = (ImGui::GetContentRegionAvail().x - spacing * 2.0f) / 3.0f;
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
	const float stopWidth = (ImGui::GetContentRegionAvail().x - spacing) / 2.0f;
	if (ImGui::Button("停止", ImVec2(stopWidth, 0.0f))) {
		RequestParticleSystemStop(world, entity);
	}
	ImGui::SameLine();
	if (ImGui::Button("消去", ImVec2(stopWidth, 0.0f))) {
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
	ImGui::Separator();
	const bool statisticsValid = HasParticleSystemStatistics(world, entity);
	MyGUI::Text("粒子数", statisticsValid ? std::to_string(GetParticleSystemParticleCount(world, entity)) : "読戻し待ち");
	MyGUI::Text("Trail数", statisticsValid ? std::to_string(GetParticleSystemTrailCount(world, entity)) : "読戻し待ち");
	MyGUI::Text("Trail容量超過", std::to_string(GetParticleSystemDroppedTrailCount(world, entity)));
	if (runtime && runtime->instance.emitter.gpu && !runtime->instance.emitter.gpu->diagnostic.empty())
		ImGui::TextWrapped("%s", runtime->instance.emitter.gpu->diagnostic.c_str());
	if (runtime && !runtime->instance.rejectedReason.empty()) ImGui::TextWrapped("%s", runtime->instance.rejectedReason.c_str());

	// 設定の所有とUndoは通常のComponent編集へまとめる
	DrawField(anyItemActive, [&]() { return MyGUI::EnumCombo("実行方式", draft.simulationTarget); });
	const auto& settings = settingsEditor_.GetDraft();
	const bool gpu = draft.simulationTarget == ParticleSimulationTarget::GPU;
	const uint64_t capacity = (std::max)(1u, settings.emitter.maxParticles);
	const uint64_t attributeBytes = (gpu ? std::bit_ceil(capacity) * sizeof(ParticleAttributesGPU) :
		capacity * ParticleStorage::GetAttributeBytesPerParticle());
	MyGUI::Text("粒子属性の目安MB", std::to_string(attributeBytes / (1024.0 * 1024.0)));
	if (settings.trail.enabled) {
		const auto trail = MakeParticleGPUTrailConstants(settings.trail, static_cast<uint32_t>(capacity));
		const uint64_t cpuPointCount = uint64_t(std::clamp(settings.trail.maxPoints, 2, 4096));
		const uint64_t cpuBudget = uint64_t(settings.trail.memoryBudgetMB) * 1024 * 1024;
		const uint64_t cpuTrails = (std::min)(settings.trail.mode == ParticleTrailMode::Ribbon ? 1ull :
			uint64_t(settings.trail.maxTrails), cpuBudget / (cpuPointCount * sizeof(ParticleTrailPoint)));
		const uint64_t bytes = gpu ? std::bit_ceil(uint64_t(trail.trailCapacity) * trail.pointCapacity) * sizeof(ParticleTrailPointGPU) :
			cpuTrails * cpuPointCount * sizeof(ParticleTrailPoint);
		MyGUI::Text("軌跡点容量の目安MB", std::to_string(bytes / (1024.0 * 1024.0)));
	}
	ImGui::Separator();
	bool changed = false;
	if (ImGui::BeginTabBar("##ParticleSystemSettings")) {
		ParticleEmitterDrawer emitter(settingsEditor_, status_);
		ParticlePhaseDrawer phases(settingsEditor_, status_, moduleSearch_);
		changed |= emitter.Draw(context, settingsEditor_.GetDraft(), draft.simulationTarget);
		changed |= phases.Draw(context, settingsEditor_.GetDraft(), settingsEditor_.GetEditorState(), draft.simulationTarget);
		if (gpu) changed |= ParticleFeedbackDrawer::Draw(context, world, entity, settingsEditor_.GetDraft().feedback);
		ImGui::EndTabBar();
	}
	const bool active = ImGui::IsAnyItemActive();
	PushEditResult({.valueChanged = changed, .anyItemActive = active, .editFinished = changed && !active}, anyItemActive);
	if (!status_.empty()) ImGui::TextWrapped("%s", status_.c_str());

}


void Engine::ParticleSystemInspectorDrawer::OnSyncDraftFromWorld(ECSWorld& world,
	const Entity& entity, [[maybe_unused]] const ParticleSystemComponent& component) {

	const auto settings = GetParticleSystemSettings(world, entity);
	settingsEditor_.GetDraft() = settings ? *settings : MakeDefaultParticleSystemSettings();
}

void Engine::ParticleSystemInspectorDrawer::SerializeDraft([[maybe_unused]] ECSWorld& world,
	[[maybe_unused]] const Entity& entity, const ParticleSystemComponent& component, nlohmann::json& out) const {

	to_json(out, component);
	out["settings"] = ToJson(settingsEditor_.GetDraft());
}

void Engine::ParticleSystemInspectorDrawer::ApplyPreview(ECSWorld& world,
	const Entity& entity, const ParticleSystemComponent& component) {

	// 現在のWorldが持つ設定ハンドルを保つ
	auto& destination = world.GetComponent<ParticleSystemComponent>(entity);
	const auto handle = destination.settings;
	destination = component;
	destination.settings = handle;
	SetParticleSystemSettings(world, entity, settingsEditor_.GetDraft());
	world.MarkComponentModified<ParticleSystemComponent>(entity);
}
