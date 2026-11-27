#include "PaintMaterialInspectorDrawer.h"

#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/Database/AssetDocumentPublication.h>
#include <Engine/Core/Assets/Database/AssetDocumentRecovery.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>

void Engine::PaintMaterialInspectorDrawer::Draw(const EditorPanelContext& context, const AssetMeta& meta) {

	auto* database = context.editorContext->assetDatabase;
	if (selected_ != meta.guid) {
		selected_ = meta.guid;
		loaded_ = LoadPaintMaterialAsset(database->ResolveFullPath(selected_), saved_);
		draft_ = saved_;
		status_ = loaded_ ? "" : "ペイントマテリアルを読み込めません";
	}
	if (loaded_) preview_.Draw(context, selected_, draft_);
	ImGui::BeginDisabled(!loaded_);
	ImGui::SeparatorText("ペイントマテリアル");
	const auto channel = [&](const char* name, uint32_t bit) {
		bool enabled = (draft_.channels & bit) != 0;
		if (MyGUI::Checkbox(name, enabled)) draft_.channels = enabled ? draft_.channels | bit : draft_.channels & ~bit;
	};
	const auto texture = [&](const char* name, AssetID& id) {
		MyGUI::AssetReferenceField(name, id, database, {AssetType::Texture});
	};
	channel("Base Colorを使用", 1);
	MyGUI::ColorEdit("Base Color", draft_.baseColor);
	texture("Base Color Texture", draft_.baseColorTexture);
	channel("Normalを使用", 2);
	MyGUI::DragFloat("Normal Strength", draft_.normalStrength, {.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 8.0f});
	texture("Normal Texture", draft_.normalTexture);
	channel("Roughnessを使用", 4);
	MyGUI::DragFloat("Roughness", draft_.roughness, {.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 1.0f});
	texture("Roughness Texture", draft_.roughnessTexture);
	channel("Metallicを使用", 8);
	MyGUI::DragFloat("Metallic", draft_.metallic, {.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 1.0f});
	texture("Metallic Texture", draft_.metallicTexture);
	channel("AOを使用", 16);
	MyGUI::DragFloat("AO", draft_.occlusion, {.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 1.0f});
	texture("AO Texture", draft_.occlusionTexture);
	MyGUI::DragFloat("Height", draft_.height, {.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 1.0f});
	texture("Height Texture", draft_.heightTexture);
	MyGUI::DragFloat("タイリング", draft_.tiling, {.dragSpeed = 0.01f, .minValue = 0.001f, .maxValue = 10000.0f});
	ImGui::BeginDisabled(!HasPendingChanges());
	if (ImGui::Button("保存", ImVec2(-1.0f, 0.0f))) ApplyPendingChanges(context);
	ImGui::EndDisabled();
	ImGui::EndDisabled();
	if (ImGui::Button("再読み込み", ImVec2(-1.0f, 0.0f))) selected_ = {};
	if (!status_.empty()) MyGUI::Text("状態", status_);
}

bool Engine::PaintMaterialInspectorDrawer::ApplyPendingChanges(const EditorPanelContext& context) {

	auto& database = *context.editorContext->assetDatabase;
	const auto* meta = database.Find(selected_);
	if (!loaded_ || !meta) return false;
	AssetDocumentChange change;
	if (!AssetDocumentPublication::Prepare(database, meta->assetPath, meta->type, change, status_)) return false;
	PaintMaterialAsset current;
	if (!LoadPaintMaterialAsset(change.filePath, current) || current != saved_) {
		status_ = "素材が変更されています 再読み込みしてください";
		return false;
	}
	change.document = ToJson(draft_);
	change.canonicalize = true;
	const auto scope = AssetDocumentRecovery::MakeScope(AssetDocumentSaveKind::Material);
	if (!AssetDocumentPublication::Commit(database, {&change, 1}, scope, status_)) return false;

	saved_ = draft_;
	status_ = "保存しました";
	return true;
}
