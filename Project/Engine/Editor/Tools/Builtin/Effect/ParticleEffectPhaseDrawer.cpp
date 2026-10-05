#include "ParticleEffectPhaseDrawer.h"

//============================================================================
//	include
//============================================================================
#include "ParticleEffectMaterialResolver.h"
#include "ParticleEffectTextureDrawer.h"
#include "GUI/ParticleGUIHelpers.h"
#include "ParticleEditorDescriptorRegistry.h"
#include "Modules/ParticleCustomShaderParameterModuleDrawer.h"
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleCustomShaderParameterModule.h>
#include <Engine/Editor/Utility/EditorTextureHelper.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>

using namespace Engine;
using namespace Engine::ParticleEffectMaterialResolver;
using Engine::ParticleGUI::MakeDragSetting;

namespace {
	constexpr const char* kModuleReorderPayloadType = "PARTICLE_MODULE_REORDER";
	constexpr const char* kPhaseReorderPayloadType = "PARTICLE_PHASE_REORDER";
}

ParticleEffectPhaseDrawer::ParticleEffectPhaseDrawer(ParticleEffectEditSession& session, std::string& status, TextSearchFilter& search) :
	session_(session), statusMessage_(status), addModuleSearchFilter_(search) {}

bool ParticleEffectPhaseDrawer::Draw(const EditorToolContext& context,
	ParticleEffectGroup& group, ParticleGroupEditState& editorState) {

	bool changed = false;
	int32_t& selectedPhase = editorState.selectedPhase;
	auto& moduleCache = editorState.moduleCache;
	auto& selectedModules = editorState.selectedModules;

	//============================================================================
	//	フェーズ編集
	//============================================================================
	if (ImGui::BeginTabItem("フェーズ")) {

		// フェーズは必ず1つ以上持つ
		if (group.phases.empty()) {

			group.phases.emplace_back();
			changed = true;
		}
		// 編集用インスタンスの数をフェーズ数へ合わせる
		if (moduleCache.size() != group.phases.size()) {
			moduleCache.resize(group.phases.size());
		}
		if (selectedModules.size() != group.phases.size()) {
			selectedModules.resize(group.phases.size(), 0);
		}
		selectedPhase = std::clamp(selectedPhase, 0, static_cast<int32_t>(group.phases.size()) - 1);

		//========================================================================================================================================================
		// 左のフェーズリスト、選択と追加と削除と並べ替え
		ImGui::BeginChild("PhaseList", ImVec2(90.0f, 0.0f), true);
		if (ImGui::Button("追加", ImVec2(-FLT_MIN, 0.0f))) {

			ParticleEffectPhase phase{};
			phase.name = "Phase " + std::to_string(group.phases.size() + 1);
			group.phases.emplace_back(std::move(phase));
			moduleCache.emplace_back();
			selectedModules.emplace_back(0);
			selectedPhase = static_cast<int32_t>(group.phases.size()) - 1;
			changed = true;
		}
		ImGui::Separator();
		int32_t removePhaseIndex = -1;
		for (int32_t i = 0; i < static_cast<int32_t>(group.phases.size()); ++i) {

			ImGui::PushID(i);
			const std::string label = std::to_string(i + 1) + ": " + group.phases[i].name;
			if (ImGui::Selectable(label.c_str(), i == selectedPhase)) {
				selectedPhase = i;
			}
			// ドラッグ&ドロップで並べ替える
			if (ImGui::BeginDragDropSource()) {

				ImGui::SetDragDropPayload(kPhaseReorderPayloadType, &i, sizeof(i));
				ImGui::TextUnformatted(label.c_str());
				ImGui::EndDragDropSource();
			}
			if (ImGui::BeginDragDropTarget()) {

				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPhaseReorderPayloadType)) {

					const int32_t from = *static_cast<const int32_t*>(payload->Data);
					if (from != i) {

						Algorithm::MoveListItem(group.phases, from, i);
						Algorithm::MoveListItem(moduleCache, from, i);
						Algorithm::MoveListItem(selectedModules, from, i);
						// 選択中のフェーズを追従させる
						if (from == selectedPhase) {
							selectedPhase = i;
						} else if (from < selectedPhase && selectedPhase <= i) {
							--selectedPhase;
						} else if (i <= selectedPhase && selectedPhase < from) {
							++selectedPhase;
						}
						changed = true;
					}
				}
				ImGui::EndDragDropTarget();
			}
			// 右クリックで削除、最後の1つは消せない
			if (ImGui::BeginPopupContextItem()) {

				if (ImGui::MenuItem("削除", nullptr, false, 1 < group.phases.size())) {
					removePhaseIndex = i;
				}
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}
		if (0 <= removePhaseIndex) {

			group.phases.erase(group.phases.begin() + removePhaseIndex);
			moduleCache.erase(moduleCache.begin() + removePhaseIndex);
			selectedModules.erase(selectedModules.begin() + removePhaseIndex);
			selectedPhase = std::clamp(selectedPhase, 0, static_cast<int32_t>(group.phases.size()) - 1);
			changed = true;
		}
		ImGui::EndChild();

		//========================================================================================================================================================
		// 右の選択フェーズ編集
		ImGui::SameLine();
		ImGui::BeginChild("PhaseEdit", ImVec2(0.0f, 0.0f));

		ParticleEffectPhase& phase = group.phases[selectedPhase];
		changed |= MyGUI::InputText("名前", phase.name).valueChanged;
		changed |= ParticleGUI::DrawParticleValueFloat("寿命", phase.lifetime, MakeDragSetting(0.001f, 600.0f));
		changed |= MyGUI::EnumCombo("寿命終了時", phase.lifeEndMode).valueChanged;
		{
			// 未設定ならエフェクト共通のマテリアルを引き継ぐ
			AssetEditSetting setting{};
			AssetID material = phase.material;
			if (MyGUI::AssetReferenceField("マテリアル", material,
				context.toolContext.assetDatabase, { AssetType::Material }, setting).valueChanged) {

				std::string message{};
				if (ValidateParticleMaterialSelection(context, material, message)) {
					phase.material = material;
					changed = true;
					statusMessage_.clear();
				} else {
					statusMessage_ = "Particleマテリアルを設定できません: " + message;
				}
			}
		}

		changed |= DrawPhaseMaterialSection(context, group, phase);
		changed |= DrawPhaseParentSection(group, phase, selectedPhase);

		ImGui::SeparatorText("モジュール");
		changed |= DrawPhaseModules(context, group, editorState, phase);
		ImGui::EndChild();

		ImGui::EndTabItem();
	}
	return changed;
}

bool ParticleEffectPhaseDrawer::DrawPhaseMaterialSection(const EditorToolContext& context,
	ParticleEffectGroup& group, ParticleEffectPhase& phase) {

	if (!MyGUI::CollapsingHeader("テクスチャ設定", false)) { return false; }
	return ParticleEffectTextureDrawer::Draw(context, phase.materialSettings, ResolvePhaseMaterialID(session_.GetDraft(), group, phase));
}

bool ParticleEffectPhaseDrawer::DrawPhaseParentSection(ParticleEffectGroup& group, ParticleEffectPhase& phase, int32_t selectedPhase) {

	bool changed = false;
	ParticlePhaseParentSettings& settings = phase.parentSettings;
	if (!MyGUI::CollapsingHeader("ペアレント設定", false)) {
		return false;
	}

	changed |= MyGUI::Checkbox("所有Entityへ追従", settings.useEmitter);
	changed |= MyGUI::Checkbox("親の回転を無視", settings.ignoreParentRotation);
	changed |= MyGUI::Checkbox("親のスケールを無視", settings.ignoreParentScale);

	const bool previousHasParent = 0 < selectedPhase &&
		group.phases[selectedPhase - 1].parentSettings.HasParent();
	if (previousHasParent && !settings.HasParent()) {
		changed |= MyGUI::Checkbox("ワールドを保持", settings.keepWorldOnDetach);
	}

	return changed;
}

bool ParticleEffectPhaseDrawer::DrawPhaseModules(const EditorToolContext& context,
	ParticleEffectGroup& group, ParticleGroupEditState& editorState, ParticleEffectPhase& phase) {

	bool changed = false;
	std::vector<ParticleModuleEditCacheEntry>& cache = editorState.moduleCache[editorState.selectedPhase];
	if (cache.size() != phase.modules.size()) {
		cache.resize(phase.modules.size());
	}
	int32_t& selectedModule = editorState.selectedModules[editorState.selectedPhase];
	selectedModule = phase.modules.empty() ? -1 :
		std::clamp(selectedModule, 0, static_cast<int32_t>(phase.modules.size()) - 1);

	ImGui::BeginChild("ModuleList", ImVec2(190.0f, 0.0f), true);
	const auto& moduleDescriptors = ParticleModuleRegistry::GetInstance().GetDescriptors();
	if (ImGui::Button("モジュール追加", ImVec2(-FLT_MIN, 0.0f))) {
		ImGui::OpenPopup("##AddParticleModulePopup");
	}
	if (ImGui::BeginPopup("##AddParticleModulePopup")) {

		ImTextureID searchIcon{};
		if (context.panelContext && context.panelContext->graphicsCore) {
			searchIcon = EditorTextureHelper::GetSearchIcon(
				context.panelContext->graphicsCore->GetTextureUploadService());
		}
		addModuleSearchFilter_.DrawInput("##AddParticleModuleSearch", searchIcon, "検索...");
		ImGui::Separator();

		bool hasAny = false;
		for (const ParticleModuleRegistry::Descriptor& descriptor : moduleDescriptors) {

			if (!addModuleSearchFilter_.Matches(descriptor.id)) {
				continue;
			}
			hasAny = true;
			if (ImGui::MenuItem(descriptor.id.c_str())) {

				ParticleEffectModuleEntry entry{};
				entry.id = descriptor.id;
				if (auto module = ParticleModuleRegistry::GetInstance().Create(descriptor.typeID)) {
					entry.params = module->ToJson();
				}
				phase.modules.emplace_back(std::move(entry));
				cache.emplace_back();
				selectedModule = static_cast<int32_t>(phase.modules.size()) - 1;
				changed = true;
				ImGui::CloseCurrentPopup();
			}
		}
		if (!hasAny) {
			ImGui::TextDisabled("追加できるモジュールがありません");
		}
		ImGui::EndPopup();
	}
	ImGui::Separator();
	int32_t removeIndex = -1;
	for (int32_t i = 0; i < static_cast<int32_t>(phase.modules.size()); ++i) {

		ParticleEffectModuleEntry& entry = phase.modules[i];
		ImGui::PushID(i);
		if (ImGui::Selectable(entry.id.c_str(), i == selectedModule)) {
			selectedModule = i;
		}
		if (ImGui::BeginDragDropSource()) {

			ImGui::SetDragDropPayload(kModuleReorderPayloadType, &i, sizeof(i));
			ImGui::TextUnformatted(entry.id.c_str());
			ImGui::EndDragDropSource();
		}
		if (ImGui::BeginDragDropTarget()) {

			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kModuleReorderPayloadType)) {

				const int32_t from = *static_cast<const int32_t*>(payload->Data);
				if (from != i) {

					Algorithm::MoveListItem(phase.modules, from, i);
					Algorithm::MoveListItem(cache, from, i);
					if (selectedModule == from) {
						selectedModule = i;
					} else if (from < selectedModule && selectedModule <= i) {
						--selectedModule;
					} else if (i <= selectedModule && selectedModule < from) {
						++selectedModule;
					}
					changed = true;
				}
			}
			ImGui::EndDragDropTarget();
		}
		if (ImGui::BeginPopupContextItem()) {

			if (ImGui::MenuItem("削除")) {
				removeIndex = i;
			}
			ImGui::EndPopup();
		}
		ImGui::PopID();
	}
	ImGui::EndChild();

	ImGui::SameLine();
	ImGui::BeginChild("ModuleEdit", ImVec2(0.0f, 0.0f), true);
	if (0 <= selectedModule && selectedModule < static_cast<int32_t>(phase.modules.size())) {

		ParticleEffectModuleEntry& entry = phase.modules[selectedModule];

		// モジュール名表示
		std::string moduleName = "モジュール名: " + entry.id;
		ImGui::TextUnformatted(moduleName.c_str());
		ImGui::Separator();
		// キャッシュされたモジュールリストの中から選択IDで引く
		if (IParticleModule* module = session_.ResolveModuleCache(cache[selectedModule], entry)) {
			MyGUI::ScopedPropertyLabelWidth labelWidth(entry.id.c_str());
			if (dynamic_cast<ParticleCustomShaderParameterModule*>(module)) {

				std::vector<ShaderConstantBufferVariable> parameters{};
				const AssetID materialID = entry.id == "TrailCustomShaderParameter" ?
					ResolveTrailMaterialID(session_.GetDraft(), group) : ResolvePhaseMaterialID(session_.GetDraft(), group, phase);
				if (const std::optional<MaterialAsset> material = LoadMaterialAsset(context, materialID)) {
					if (const ShaderReflectionInfo* reflection = FindParticleMaterialReflection(context, *material)) {
						parameters = CollectMaterialParameters(*reflection);
					}
				}
				if (auto* drawer = dynamic_cast<ParticleCustomShaderParameterModuleDrawer*>(cache[selectedModule].drawer.get())) {
					drawer->SetReflectedParameters(parameters);
				}
			}
			if (ParticleEditorDescriptorRegistry::GetInstance().DrawModule(
				cache[selectedModule].typeID, *module, cache[selectedModule].drawer.get())) {

				entry.params = module->ToJson();
				changed = true;
			}
		} else {
			ImGui::TextDisabled("未登録のモジュールです");
		}
	} else {
		ImGui::TextDisabled("モジュールを選択してください");
	}
	ImGui::EndChild();

	if (0 <= removeIndex) {

		phase.modules.erase(phase.modules.begin() + removeIndex);
		cache.erase(cache.begin() + removeIndex);
		if (phase.modules.empty()) {
			selectedModule = -1;
		} else if (selectedModule >= static_cast<int32_t>(phase.modules.size())) {
			selectedModule = static_cast<int32_t>(phase.modules.size()) - 1;
		}
		changed = true;
	}
	return changed;
}
