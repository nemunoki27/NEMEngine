#include "SceneCompositionTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>

// c++
#include <algorithm>
#include <unordered_set>
// imgui
#include <imgui.h>

//============================================================================
//	SceneCompositionTool classMethods
//============================================================================
void Engine::SceneCompositionTool::OpenEditorTool() {

	openWindow_ = true;
}

void Engine::SceneCompositionTool::DrawEditorTool(const EditorToolContext& context) {

	if (openWindow_) {
		DrawWindow(context);
	}
}

void Engine::SceneCompositionTool::DrawWindow(const EditorToolContext& context) {

	if (!ImGui::Begin("シーン構成", &openWindow_)) {
		ImGui::End();
		return;
	}

	SceneInstanceManager* scenes = context.toolContext.sceneInstances;
	AssetDatabase* assetDatabase = context.toolContext.assetDatabase;
	SceneInstance* activeScene = scenes ?
		scenes->Find(context.toolContext.activeSceneInstanceID) : nullptr;
	if (!context.CanEditScene() || !activeScene || !assetDatabase || !context.panelContext ||
		!context.panelContext->host) {
		ImGui::TextDisabled("編集可能なシーンがありません");
		ImGui::End();
		return;
	}

	ImGui::Text("シーン: %s", activeScene->header.name.c_str());
	ImGui::Separator();

	int32_t removeIndex = -1;
	int32_t moveFrom = -1;
	int32_t moveTo = -1;
	for (size_t index = 0; index < activeScene->header.subScenes.size(); ++index) {

		SubSceneSlotDesc& slot = activeScene->header.subScenes[index];
		ImGui::PushID(static_cast<int>(index));
		const std::string headerLabel =
			(slot.slotName.empty() ? std::string("SubScene") : slot.slotName) + "##Slot";
		const bool open = ImGui::CollapsingHeader(headerLabel.c_str(),
			ImGuiTreeNodeFlags_DefaultOpen);

		const float buttonSize = ImGui::GetFrameHeight();
		const float right = ImGui::GetWindowContentRegionMax().x;
		ImGui::SameLine((std::max)(0.0f, right - buttonSize * 3.0f -
			ImGui::GetStyle().ItemSpacing.x * 2.0f));
		if (ImGui::Button("^", ImVec2(buttonSize, buttonSize)) && index > 0) {
			moveFrom = static_cast<int32_t>(index);
			moveTo = moveFrom - 1;
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("上へ移動");
		}
		ImGui::SameLine();
		if (ImGui::Button("v", ImVec2(buttonSize, buttonSize)) &&
			index + 1 < activeScene->header.subScenes.size()) {
			moveFrom = static_cast<int32_t>(index);
			moveTo = moveFrom + 1;
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("下へ移動");
		}
		ImGui::SameLine();
		if (ImGui::Button("X", ImVec2(buttonSize, buttonSize))) {
			removeIndex = static_cast<int32_t>(index);
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("削除");
		}

		if (open) {

			const std::vector<SubSceneSlotDesc> previous = activeScene->header.subScenes;
			bool changed = false;
			{
				MyGUI::ScopedPropertyLabelWidth labelWidth("SubSceneSlot");
				changed |= MyGUI::InputText("名前", slot.slotName).valueChanged;
				changed |= MyGUI::AssetReferenceField(
					"シーン", slot.sceneAsset, assetDatabase, { AssetType::Scene }).valueChanged;
				changed |= MyGUI::Checkbox("有効", slot.enabled);
			}
			if (changed) {
				ApplyChanges(context, *activeScene, previous);
				ImGui::PopID();
				ImGui::End();
				return;
			}
		}
		ImGui::PopID();
	}

	if (removeIndex >= 0) {
		const std::vector<SubSceneSlotDesc> previous = activeScene->header.subScenes;
		activeScene->header.subScenes.erase(
			activeScene->header.subScenes.begin() + removeIndex);
		ApplyChanges(context, *activeScene, previous);
		ImGui::End();
		return;
	} else if (moveFrom >= 0 && moveTo >= 0) {
		const std::vector<SubSceneSlotDesc> previous = activeScene->header.subScenes;
		std::swap(activeScene->header.subScenes[moveFrom],
			activeScene->header.subScenes[moveTo]);
		ApplyChanges(context, *activeScene, previous);
		ImGui::End();
		return;
	}

	ImGui::Spacing();
	if (ImGui::Button("サブシーンを追加", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

		const std::vector<SubSceneSlotDesc> previous = activeScene->header.subScenes;
		SubSceneSlotDesc slot{};
		slot.slotID = UUID::New();
		size_t nameIndex = activeScene->header.subScenes.size() + 1;
		do {
			slot.slotName = "SubScene" + std::to_string(nameIndex++);
		} while (std::any_of(activeScene->header.subScenes.begin(),
			activeScene->header.subScenes.end(), [&slot](const SubSceneSlotDesc& existing) {
				return existing.slotName == slot.slotName;
			}));
		activeScene->header.subScenes.emplace_back(std::move(slot));
		ApplyChanges(context, *activeScene, previous);
	}

	if (!statusMessage_.empty()) {
		ImGui::Separator();
		if (statusError_) {
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", statusMessage_.c_str());
		} else {
			ImGui::TextDisabled("%s", statusMessage_.c_str());
		}
	}
	ImGui::End();
}

bool Engine::SceneCompositionTool::ApplyChanges(const EditorToolContext& context,
	SceneInstance& instance, const std::vector<SubSceneSlotDesc>& previous) {

	const UUID instanceID = instance.instanceID;
	std::unordered_set<UUID> slotIDs;
	std::unordered_set<std::string> slotNames;
	bool valid = true;
	for (const SubSceneSlotDesc& slot : instance.header.subScenes) {

		if (!slot.slotID || !slotIDs.insert(slot.slotID).second ||
			slot.slotName.empty() || !slotNames.insert(slot.slotName).second ||
			(slot.sceneAsset && slot.sceneAsset == instance.sceneAsset)) {
			valid = false;
			break;
		}
	}

	SceneInstanceManager* scenes = context.toolContext.sceneInstances;
	AssetDatabase* assetDatabase = context.toolContext.assetDatabase;
	ECSWorld* world = context.GetWorld();
	if (!valid || !scenes || !assetDatabase || !world ||
		!scenes->SynchronizeSubScenes(
			*assetDatabase, SceneSystem{}, *world, instanceID)) {

		SceneInstance* current = scenes ? scenes->Find(instanceID) : nullptr;
		if (current) {
			current->header.subScenes = previous;
		}
		if (scenes && assetDatabase && world) {
			scenes->SynchronizeSubScenes(
				*assetDatabase, SceneSystem{}, *world, instanceID);
		}
		statusMessage_ = "SubScene設定を適用できません";
		statusError_ = true;
		return false;
	}

	context.panelContext->host->RequestMarkSceneDirty();
	statusMessage_ = "SubScene設定を更新しました";
	statusError_ = false;
	return true;
}
