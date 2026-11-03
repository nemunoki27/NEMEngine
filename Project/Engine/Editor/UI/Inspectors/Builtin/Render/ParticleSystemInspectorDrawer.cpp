#include "ParticleSystemInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/Inspectors/Common/TimeSourceFields.h>
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
	DrawField(anyItemActive, [&] {
		if (draft.useUnscaledTime) timeSourceDraft_.mode = TimeSourceMode::Unscaled;
		auto result = DrawTimeSourceField(context, world, entity, timeSourceDraft_);
		if (result.valueChanged) draft.useUnscaledTime = false;
		return result;
	});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField(
			Engine::GetInspectorLabel(Engine::InspectorFieldID::PlayOnStart), draft.playOnAwake);
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField(
			Engine::GetInspectorLabel(Engine::InspectorFieldID::PlayInEditMode), draft.playInEditMode);
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat(Engine::GetInspectorLabel(Engine::InspectorFieldID::PlaybackSpeed), draft.playbackSpeed,
			{ .dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 100.0f });
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField(
			"乱数シードを自動生成", draft.useAutoRandomSeed);
		});
	if (!draft.useAutoRandomSeed) {
		DrawField(anyItemActive, [&]() {
			ValueEditResult result{};
			if (MyGUI::BeginPropertyRow("乱数シード")) {
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
		return MyGUI::DragInt(Engine::GetInspectorLabel(Engine::InspectorFieldID::SortLayer), draft.layer);
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragInt(Engine::GetInspectorLabel(Engine::InspectorFieldID::DrawOrder), draft.order);
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField(
			"エミッター形状を描画", draft.drawEmitterShape);
		});

	// 設定の所有とUndoは通常のComponent編集へまとめる
	DrawField(anyItemActive, [&]() { return MyGUI::EnumCombo("実行方式", draft.simulationTarget); });
	const auto& settings = settingsEditor_.GetDraft();
	const bool gpu = draft.simulationTarget == ParticleSimulationTarget::GPU;
	const uint64_t capacity = (std::max)(1u, settings.emitter.maxParticles);
	const uint64_t attributeBytes = (gpu ? std::bit_ceil(capacity) * sizeof(ParticleAttributesGPU) :
		capacity * ParticleStorage::GetAttributeBytesPerParticle());
	MyGUI::TextDouble("粒子属性の目安MB", static_cast<double>(attributeBytes) / (1024.0 * 1024.0));
	if (settings.trail.enabled) {
		const auto trail = MakeParticleGPUTrailConstants(settings.trail, static_cast<uint32_t>(capacity));
		const uint64_t cpuPointCount = uint64_t(std::clamp(settings.trail.maxPoints, 2, 4096));
		const uint64_t cpuBudget = uint64_t(settings.trail.memoryBudgetMB) * 1024 * 1024;
		const uint64_t cpuTrails = (std::min)(settings.trail.mode == ParticleTrailMode::Ribbon ? 1ull :
			uint64_t(settings.trail.maxTrails), cpuBudget / (cpuPointCount * sizeof(ParticleTrailPoint)));
		const uint64_t bytes = gpu ? std::bit_ceil(uint64_t(trail.trailCapacity) * trail.pointCapacity) * sizeof(ParticleTrailPointGPU) :
			cpuTrails * cpuPointCount * sizeof(ParticleTrailPoint);
		MyGUI::TextDouble("軌跡点容量の目安MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
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
	if (!status_.empty()) MyGUI::Text("設定状態", status_);
	ImGui::Separator();
	constexpr InspectorPlaybackAction actions[]{InspectorPlaybackAction::Play, InspectorPlaybackAction::EmitOnce,
		InspectorPlaybackAction::Pause, InspectorPlaybackAction::Stop, InspectorPlaybackAction::Clear};
	switch (DrawInspectorPlaybackControls(context.IsPlaying() || draft.playInEditMode, false, actions)) {
	case InspectorPlaybackAction::Play: RequestParticleSystemPlay(world, entity); break;
	case InspectorPlaybackAction::EmitOnce: RequestParticleSystemRestart(world, entity, true); break;
	case InspectorPlaybackAction::Pause: RequestParticleSystemPause(world, entity); break;
	case InspectorPlaybackAction::Stop: RequestParticleSystemStop(world, entity); break;
	case InspectorPlaybackAction::Clear: RequestParticleSystemClear(world, entity); break;
	default: break;
	}
	const auto* runtime = TryGetParticleSystemRuntime(world, entity);
	const char* state = GetInspectorPlaybackState(IsParticleSystemPlaying(world, entity), runtime && runtime->paused);
	if (runtime && !runtime->paused && !IsParticleSystemPlaying(world, entity) && IsParticleSystemAlive(world, entity)) state = "停止処理中";
	MyGUI::Text(GetInspectorLabel(InspectorFieldID::PlaybackState), state);
	ImGui::Separator();
	const bool statisticsValid = HasParticleSystemStatistics(world, entity);
	if (statisticsValid) {
		MyGUI::TextInt("粒子数", GetParticleSystemParticleCount(world, entity));
		MyGUI::TextInt("Trail数", GetParticleSystemTrailCount(world, entity));
	} else {
		MyGUI::Text("粒子数", "読戻し待ち");
		MyGUI::Text("Trail数", "読戻し待ち");
	}
	MyGUI::TextInt("Trail容量超過", GetParticleSystemDroppedTrailCount(world, entity));
	if (runtime && runtime->instance.emitter.gpu && !runtime->instance.emitter.gpu->diagnostic.empty())
		MyGUI::Text("GPU状態", runtime->instance.emitter.gpu->diagnostic);
	if (runtime && !runtime->instance.rejectedReason.empty()) MyGUI::Text("発生状態", runtime->instance.rejectedReason);
}


void Engine::ParticleSystemInspectorDrawer::OnSyncDraftFromWorld(ECSWorld& world,
	const Entity& entity, [[maybe_unused]] const ParticleSystemComponent& component) {

	timeSourceDraft_ = GetParticleSystemTimeSource(world, entity);
	const auto settings = GetParticleSystemSettings(world, entity);
	settingsEditor_.GetDraft() = settings ? *settings : MakeDefaultParticleSystemSettings();
}

void Engine::ParticleSystemInspectorDrawer::SerializeDraft([[maybe_unused]] ECSWorld& world,
	[[maybe_unused]] const Entity& entity, const ParticleSystemComponent& component, nlohmann::json& out) const {

	to_json(out, component);
	out["settings"] = ToJson(settingsEditor_.GetDraft());
	out["timeSource"] = timeSourceDraft_;
}

void Engine::ParticleSystemInspectorDrawer::ApplyPreview(ECSWorld& world,
	const Entity& entity, const ParticleSystemComponent& component) {

	// 現在のWorldが持つ設定ハンドルを保つ
	auto& destination = world.GetComponent<ParticleSystemComponent>(entity);
	const auto handle = destination.settings;
	destination = component;
	destination.settings = handle;
	SetParticleSystemSettings(world, entity, settingsEditor_.GetDraft());
	SetParticleSystemTimeSource(world, entity, timeSourceDraft_);
	world.MarkComponentModified<ParticleSystemComponent>(entity);
}
