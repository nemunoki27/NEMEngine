#include "ParticlePhaseDrawer.h"

//============================================================================
//	include
//============================================================================
#include "ParticleMaterialResolver.h"
#include "ParticleTextureDrawer.h"
#include "GUI/ParticleGUIHelpers.h"
#include "ParticleEditorDescriptorRegistry.h"
#include "Modules/ParticleCustomShaderParameterModuleDrawer.h"
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleCustomShaderParameterModule.h>
#include <Engine/Core/Rendering/Particle/ParticleGPUProgram.h>
#include <Engine/Core/Rendering/Particle/ParticleSimulationValidation.h>
#include <Engine/Editor/Utility/EditorTextureHelper.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>

using namespace Engine;
using namespace Engine::ParticleMaterialResolver;
using Engine::ParticleGUI::MakeDragSetting;

ParticlePhaseDrawer::ParticlePhaseDrawer(ParticleSettingsEditSession& session, std::string& status, TextSearchFilter& search) :
	session_(session), statusMessage_(status), addModuleSearchFilter_(search) {}

bool ParticlePhaseDrawer::Draw(const EditorPanelContext& context,
	ParticleSystemSettings& group, ParticleSettingsEditState& editorState, ParticleSimulationTarget simulationTarget) {

	if (!ImGui::BeginTabItem("フェーズ")) return false;
	bool changed = false;
	if (group.phases.empty()) group.phases.emplace_back();
	int32_t& selected = editorState.selectedPhase;
	selected = std::clamp(selected, 0, static_cast<int32_t>(group.phases.size()) - 1);
	editorState.moduleCache.resize(group.phases.size());

	// 選択と順序を分けて表示する
	if (MyGUI::BeginPropertyRow("フェーズ")) {
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		if (ImGui::BeginCombo("##Value", group.phases[selected].name.c_str())) {
			for (int32_t i = 0; i < static_cast<int32_t>(group.phases.size()); ++i) {
				ImGui::PushID(i);
				if (ImGui::Selectable(group.phases[i].name.c_str(), selected == i)) selected = i;
				if (selected == i) ImGui::SetItemDefaultFocus();
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		MyGUI::EndPropertyRow();
	}
	// 余白を除いた幅を4つの操作へ均等に割り当てる
	const float spacing = ImGui::GetStyle().ItemSpacing.x;
	const float buttonWidth = (std::max)(1.0f, (ImGui::GetContentRegionAvail().x - spacing * 3.0f) / 4.0f);
	const ImVec2 buttonSize(buttonWidth, 0.0f);
	if (ImGui::Button("追加##Phase", buttonSize)) {
		ParticlePhaseSettings phase;
		phase.name = "フェーズ" + std::to_string(group.phases.size() + 1);
		group.phases.push_back(std::move(phase));
		editorState.moduleCache.emplace_back();
		selected = static_cast<int32_t>(group.phases.size()) - 1;
		changed = true;
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(group.phases.size() <= 1);
	if (ImGui::Button("削除##Phase", buttonSize)) {
		group.phases.erase(group.phases.begin() + selected);
		editorState.moduleCache.erase(editorState.moduleCache.begin() + selected);
		selected = (std::max)(0, selected - 1);
		changed = true;
	}
	ImGui::EndDisabled();
	const auto move = [&](int32_t destination) {
		Algorithm::MoveListItem(group.phases, selected, destination);
		Algorithm::MoveListItem(editorState.moduleCache, selected, destination);
		selected = destination;
		changed = true;
	};
	ImGui::SameLine();
	ImGui::BeginDisabled(selected == 0);
	if (ImGui::Button("上へ##Phase", buttonSize)) move(selected - 1);
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(selected + 1 >= static_cast<int32_t>(group.phases.size()));
	if (ImGui::Button("下へ##Phase", buttonSize)) move(selected + 1);
	ImGui::EndDisabled();

	ParticlePhaseSettings& phase = group.phases[selected];
	changed |= MyGUI::InputText("名前", phase.name).valueChanged;
	changed |= ParticleGUI::DrawParticleValueFloat("寿命", phase.lifetime, MakeDragSetting(0.001f, 600.0f));
	changed |= MyGUI::EnumCombo("寿命終了時", phase.lifeEndMode).valueChanged;
	AssetEditSetting setting{};
	changed |= MyGUI::AssetReferenceField("マテリアル", phase.material,
		context.editorContext->assetDatabase, {AssetType::Material}, setting).valueChanged;
	changed |= DrawPhaseMaterialSection(context, group, phase);
	changed |= DrawPhaseParentSection(group, phase, selected);
	changed |= DrawPhaseModules(context, group, editorState, phase, simulationTarget);
	ImGui::EndTabItem();
	return changed;
}

bool ParticlePhaseDrawer::DrawPhaseMaterialSection(const EditorPanelContext& context,
	ParticleSystemSettings& group, ParticlePhaseSettings& phase) {

	return ParticleTextureDrawer::Draw(context, phase.materialSettings, ResolvePhaseMaterialID(group, phase));
}

bool ParticlePhaseDrawer::DrawPhaseParentSection(ParticleSystemSettings& group, ParticlePhaseSettings& phase, int32_t selectedPhase) {

	bool changed = false;
	ParticlePhaseParentSettings& settings = phase.parentSettings;
	if (!MyGUI::CollapsingHeader("ペアレント設定", false)) {
		return false;
	}

	changed |= MyGUI::Checkbox("所有エンティティへ追従", settings.useEmitter);
	changed |= MyGUI::Checkbox("親の回転を無視", settings.ignoreParentRotation);
	changed |= MyGUI::Checkbox("親のスケールを無視", settings.ignoreParentScale);

	const bool previousHasParent = 0 < selectedPhase &&
		group.phases[selectedPhase - 1].parentSettings.HasParent();
	if (previousHasParent && !settings.HasParent()) {
		changed |= MyGUI::Checkbox("ワールドを保持", settings.keepWorldOnDetach);
	}

	return changed;
}

bool ParticlePhaseDrawer::DrawPhaseModules(const EditorPanelContext& context,
	ParticleSystemSettings& group, ParticleSettingsEditState& editorState,
	ParticlePhaseSettings& phase, ParticleSimulationTarget simulationTarget) {

	bool changed = false;
	const bool gpu = simulationTarget == ParticleSimulationTarget::GPU;
	auto& cache = editorState.moduleCache[editorState.selectedPhase];
	cache.resize(phase.modules.size());
	ImGui::Separator();
	if (ImGui::Button("モジュール追加", ImVec2(-FLT_MIN, 0.0f))) ImGui::OpenPopup("##AddParticleModule");
	if (ImGui::BeginPopup("##AddParticleModule")) {
		addModuleSearchFilter_.DrawInput("##ModuleSearch", {}, "検索...");
		for (const auto& descriptor : ParticleModuleRegistry::GetInstance().GetDescriptors()) {
			if (gpu && !IsParticleGPUModuleSupported(descriptor.id)) continue;
			if (!gpu && IsParticleModuleGPUOnly(descriptor.id)) continue;
			if (!addModuleSearchFilter_.Matches(ImGuiUtility::GetDisplayName(descriptor.id.c_str()))) continue;
			if (ImGui::MenuItem(ImGuiUtility::GetDisplayName(descriptor.id.c_str()))) {
				ParticleModuleEntry entry;
				entry.id = descriptor.id;
				entry.params = ParticleModuleRegistry::GetInstance().Create(descriptor.typeID)->ToJson();
				phase.modules.push_back(std::move(entry));
				cache.emplace_back();
				changed = true;
			}
		}
		ImGui::EndPopup();
	}
	ImGui::SeparatorText("モジュール一覧");
	ImGui::Indent();
	int32_t remove = -1;
	int32_t from = -1;
	int32_t to = -1;
	for (int32_t i = 0; i < static_cast<int32_t>(phase.modules.size()); ++i) {
		auto& entry = phase.modules[i];
		// CPU用の設定は保持し、GPUでは対応するModuleだけを表示する
		if (gpu && !IsParticleGPUModuleSupported(entry.id)) continue;
		ImGui::PushID(ToString(entry.instanceID).c_str());
		const bool open = MyGUI::CollapsingHeader(ImGuiUtility::GetDisplayName(entry.id.c_str()));
		if (ImGui::BeginPopupContextItem()) {
			if (ImGui::MenuItem("上へ", nullptr, false, i > 0)) { from = i; to = i - 1; }
			if (ImGui::MenuItem("下へ", nullptr, false, i + 1 < static_cast<int32_t>(phase.modules.size()))) { from = i; to = i + 1; }
			if (ImGui::MenuItem("削除")) remove = i;
			ImGui::EndPopup();
		}
		if (open) {
			ImGui::Indent();
			MyGUI::ScopedPropertyLabelWidth labelWidth(entry.id.c_str());
			changed |= MyGUI::Checkbox("モジュールを有効にする", entry.enabled);
			ImGui::BeginDisabled(!entry.enabled);
			if (!gpu && IsParticleModuleGPUOnly(entry.id)) {
				ImGui::TextWrapped("このモジュールにはGPU実行が必要です");
			} else if (auto* module = session_.ResolveModuleCache(cache[i], entry)) {
				if (auto* drawer = dynamic_cast<ParticleCustomShaderParameterModuleDrawer*>(cache[i].drawer.get())) {
					if (const auto material = LoadMaterialAsset(context, ResolvePhaseMaterialID(group, phase))) {
						if (const auto* reflection = FindParticleMaterialReflection(context, *material))
							drawer->SetReflectedParameters(CollectMaterialParameters(*reflection));
					}
				}
				if (ParticleEditorDescriptorRegistry::GetInstance().DrawModule(
					cache[i].typeID, *module, cache[i].drawer.get(), context)) {
					entry.params = module->ToJson();
					changed = true;
				}
			}
			ImGui::EndDisabled();
			ImGui::Unindent();
		}
		ImGui::PopID();
	}
	ImGui::Unindent();
	// 表示中の参照を使い終えてから構造を変える
	if (remove >= 0) {
		phase.modules.erase(phase.modules.begin() + remove);
		cache.erase(cache.begin() + remove);
		changed = true;
	} else if (from >= 0) {
		Algorithm::MoveListItem(phase.modules, from, to);
		Algorithm::MoveListItem(cache, from, to);
		changed = true;
	}
	return changed;
}
