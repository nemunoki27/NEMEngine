#include "MeshAssetInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>

// c++
#include <algorithm>

void Engine::MeshAssetInspectorDrawer::Draw(
	const EditorPanelContext& context, const AssetMeta& meta) {

	SyncSelection(meta);
	ImGui::SeparatorText("LODインポート");
	MyGUI::ScopedPropertyLabelWidth labelWidth("MeshImportSettings");
	MyGUI::Checkbox("自動LODを生成",
		draftSettings_.generateAutomaticLODs);
	for (size_t index = 0;
		index < draftSettings_.manualLODMeshes.size(); ++index) {

		std::string label = "LOD" + std::to_string(index + 1);
		MyGUI::AssetReferenceField(
			label.c_str(), draftSettings_.manualLODMeshes[index],
			context.editorContext ?
				context.editorContext->assetDatabase : nullptr,
			{ AssetType::Mesh });
	}
	ImGui::BeginDisabled(!draftSettings_.generateAutomaticLODs);
	for (size_t index = 0;
		index < draftSettings_.lodTargetErrors.size(); ++index) {

		std::string label = "LOD" + std::to_string(index + 1) + " 誤差";
		MyGUI::DragFloat(label.c_str(),
			draftSettings_.lodTargetErrors[index],
			{ .dragSpeed = 0.001f,.minValue = 0.0f,.maxValue = 1.0f });
	}
	ImGui::EndDisabled();
	MyGUI::EnumCombo("切り替え",
		draftSettings_.lodTransition);

	const bool dirty = HasPendingChanges();
	const float spacing = ImGui::GetStyle().ItemSpacing.x;
	const float width =
		(ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
	ImGui::BeginDisabled(!dirty);
	if (ImGui::Button("適用", ImVec2(width, 0.0f))) {
		ApplySettings(context, meta);
	}
	ImGui::SameLine();
	if (ImGui::Button("元に戻す", ImVec2(width, 0.0f))) {
		DiscardPendingChanges();
	}
	ImGui::EndDisabled();
	if (!statusMessage_.empty()) {
		ImGui::TextDisabled("%s", statusMessage_.c_str());
	}
}

bool Engine::MeshAssetInspectorDrawer::ApplyPendingChanges(
	const EditorPanelContext& context) {

	AssetDatabase* database = context.editorContext ?
		context.editorContext->assetDatabase : nullptr;
	const AssetMeta* meta = database ?
		database->Find(selectedAsset_) : nullptr;
	return meta && ApplySettings(context, *meta);
}

void Engine::MeshAssetInspectorDrawer::DiscardPendingChanges() {

	draftSettings_ = savedSettings_;
	statusMessage_.clear();
}

void Engine::MeshAssetInspectorDrawer::SyncSelection(
	const AssetMeta& meta) {

	if (selectedAsset_ == meta.guid) {
		return;
	}
	selectedAsset_ = meta.guid;
	savedSettings_ = ParseMeshImportSettings(meta.importerSettings);
	draftSettings_ = savedSettings_;
	statusMessage_.clear();
}

bool Engine::MeshAssetInspectorDrawer::ApplySettings(
	const EditorPanelContext& context, const AssetMeta& meta) {

	AssetDatabase* database = context.editorContext ?
		context.editorContext->assetDatabase : nullptr;
	for (size_t index = 0;
		index < draftSettings_.manualLODMeshes.size(); ++index) {

		const AssetID lod = draftSettings_.manualLODMeshes[index];
		if (lod == meta.guid || (lod && std::find(
			draftSettings_.manualLODMeshes.begin(),
			draftSettings_.manualLODMeshes.begin() + index,
			lod) != draftSettings_.manualLODMeshes.begin() + index)) {

			statusMessage_ =
				"手動LODに同じMeshは指定できません";
			return false;
		}
	}
	for (size_t index = 1;
		index < draftSettings_.lodTargetErrors.size(); ++index) {

		draftSettings_.lodTargetErrors[index] = (std::max)(
			draftSettings_.lodTargetErrors[index],
			draftSettings_.lodTargetErrors[index - 1]);
	}
	if (!database || !database->UpdateImporterSettings(
		meta.guid, ToJson(draftSettings_), kMeshImporterVersion)) {

		statusMessage_ = "適用に失敗しました";
		return false;
	}
	if (context.renderPipeline) {
		context.renderPipeline->ReloadMesh(meta.guid);
	}
	savedSettings_ = draftSettings_;
	statusMessage_ = "適用しました";
	return true;
}
