#include "SceneCompositionTool.h"
#include "SceneCompositionOperations.h"

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

	std::vector<SubSceneSlotDesc> draft = activeScene->header.subScenes;
	int32_t removeIndex = -1;
	int32_t moveFrom = -1;
	int32_t moveTo = -1;
	for (size_t index = 0; index < draft.size(); ++index) {

		SubSceneSlotDesc& slot = draft[index];
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
			index + 1 < draft.size()) {
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
			bool changed = false;
			{
				MyGUI::ScopedPropertyLabelWidth labelWidth("SubSceneSlot");
				changed |= MyGUI::InputText("名前", slot.slotName).valueChanged;
				changed |= MyGUI::AssetReferenceField(
					"シーン", slot.sceneAsset, assetDatabase, { AssetType::Scene }).valueChanged;
				changed |= MyGUI::Checkbox("有効", slot.enabled);
			}
			if (changed) {
				SceneCompositionOperations::ApplyChanges(context, *activeScene, draft, statusMessage_, statusError_);
				ImGui::PopID();
				ImGui::End();
				return;
			}
		}
		ImGui::PopID();
	}

	if (removeIndex >= 0) {
		draft.erase(
			draft.begin() + removeIndex);
		SceneCompositionOperations::ApplyChanges(context, *activeScene, draft, statusMessage_, statusError_);
		ImGui::End();
		return;
	} else if (moveFrom >= 0 && moveTo >= 0) {
		std::swap(draft[moveFrom],
			draft[moveTo]);
		SceneCompositionOperations::ApplyChanges(context, *activeScene, draft, statusMessage_, statusError_);
		ImGui::End();
		return;
	}

	ImGui::Spacing();
	if (ImGui::Button("サブシーンを追加", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
		SubSceneSlotDesc slot{};
		slot.slotID = UUID::New();
		size_t nameIndex = draft.size() + 1;
		do {
			slot.slotName = "SubScene" + std::to_string(nameIndex++);
		} while (std::any_of(draft.begin(),
			draft.end(), [&slot](const SubSceneSlotDesc& existing) {
				return existing.slotName == slot.slotName;
			}));
		draft.emplace_back(std::move(slot));
		SceneCompositionOperations::ApplyChanges(context, *activeScene, draft, statusMessage_, statusError_);
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
