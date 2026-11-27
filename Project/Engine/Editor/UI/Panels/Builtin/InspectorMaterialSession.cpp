#include "InspectorMaterialSession.h"

//============================================================================
//	include
//============================================================================
#include "MaterialAssetFields.h"
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Editor/Commands/Assets/EditMaterialAssetCommand.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAssetReferences.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphParameterDefaults.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>

// c++
#include <filesystem>

namespace {

	using namespace Engine;

	void ResolveMaterialPipelineReferences(AssetDatabase& database, nlohmann::json& data) {

		if (!data.contains("passes") || !data["passes"].is_array()) {
			return;
		}
		for (auto& pass : data["passes"]) {

			const auto found = pass.find("pipeline");
			if (found == pass.end() || !found->is_string()) {
				continue;
			}
			const auto& text = found->get_ref<const std::string&>();
			if (text.empty() || TryParseAssetGUID32Hex(text)) {
				continue;
			}
			const auto path = database.ResolveAssetPath(text);
			std::error_code error;
			if (std::filesystem::is_regular_file(path, error)) {
				const AssetID id = database.ImportOrGet(text, AssetType::RenderPipeline);
				if (id) *found = ToString(id);
			}
		}
	}

	// 編集していない旧形式の項目と追加の情報は元の表現を維持する
	nlohmann::json EditedDocument(const nlohmann::json& document, const nlohmann::json& before,
		const nlohmann::json& after) {

		nlohmann::json result = document;
		for (const auto& [key, value] : after.items()) {
			const auto found = before.find(key);
			if (found == before.end() || *found != value) result[key] = value;
		}
		for (const auto& [key, value] : before.items()) {
			if (!after.contains(key)) result.erase(key);
		}
		return result;
	}
}

//============================================================================
//	InspectorMaterialSession classMethods
//============================================================================
void Engine::InspectorMaterialSession::DrawMaterialAssetInspector(const EditorPanelContext& context, const AssetMeta& meta) {

	if (!LoadMaterialDraft(context, meta)) {
		ImGui::TextWrapped("マテリアルを読み込めません %s", meta.assetPath.c_str());
		return;
	}

	ImGui::BeginDisabled(readOnly_);
	ValueEditResult edit = MyGUI::InputText("マテリアル名", materialDraft_.name);
	ImGui::EndDisabled();

	if (materialDraft_.shaderGraph && graphFields_.Refresh(*context.editorContext->assetDatabase, materialDraft_.shaderGraph)) {
		preview_.Invalidate();
	}
	if (!materialDraft_.shaderGraph || graphFields_.IsLoaded()) preview_.Draw(context, materialDraft_);
	if (readOnly_) ImGui::TextWrapped("共通エンジンのマテリアルです GameAssetsにコピーすると編集できます");
	if (materialDraft_.shaderGraph) {
		edit.Merge(graphFields_.Draw(context, materialDraft_, readOnly_));
	} else {
		ImGui::BeginDisabled(readOnly_);
		edit.Merge(MaterialAssetFields::Draw(context, materialDraft_));
		ImGui::EndDisabled();
	}

	ImGui::BeginDisabled(readOnly_);
	edit.Merge(MaterialAssetFields::DrawPaintSettings(context, materialDraft_));
	ImGui::EndDisabled();
	if (readOnly_) return;
	if (edit.valueChanged) {
		pendingEdit_ = true;
		preview_.Invalidate();
		if (context.renderPipeline && !edit.editFinished) {
			auto& library = context.renderPipeline->GetRenderAssetLibrary();
			if (materialDraft_.shaderGraph) {
				// Graphが組み立てたPassと暗黙の定数を保って描画値を更新する
				if (const auto* runtime = library.LoadMaterial(editingMaterialAsset_)) {
					MaterialAsset live = *runtime;
					for (const auto& record : materialDraft_.parameters.GetRecords()) {
						live.parameters.Set(record.id, record.namedValue.first, record.semantic, record.namedValue.second);
					}
					library.RegisterDerivedMaterial(std::move(live));
				}
			} else {
				library.RegisterDerivedMaterial(materialDraft_);
			}
		}
	}
	if (edit.editFinished) SaveMaterialDraft(context);
	if (!diagnostic_.empty()) {
		ImGui::TextWrapped("%s", diagnostic_.c_str());
		if (ImGui::Button("再試行")) SaveMaterialDraft(context);
		ImGui::SameLine();
		if (ImGui::Button("読み込み直す")) EndEdit(context);
	}
}

bool Engine::InspectorMaterialSession::LoadMaterialDraft(const EditorPanelContext& context, const AssetMeta& meta) {

	auto& database = *context.editorContext->assetDatabase;
	const auto lifetime = database.GetCacheLifetime().lock();
	const auto revision = database.GetContentRevision(meta.guid);
	if (editingMaterialAsset_ == meta.guid && databaseLifetime_.lock() == lifetime && contentRevision_ == revision) {
		return true;
	}
	EndEdit(context);
	const AssetID id = meta.guid;
	const auto path = database.ResolveFullPath(id);
	if (!JsonAdapter::TryLoad(path, savedDocument_)) return false;

	auto document = savedDocument_;
	ResolveMaterialPipelineReferences(database, document);
	if (!FromJson(document, materialDraft_)) return false;
	materialDraft_.guid = id;
	if (materialDraft_.name.empty()) materialDraft_.name = path.stem().stem().string();
	if (materialDraft_.shaderGraph && !document.contains("graphOverrides")) {
		ShaderGraphAsset source;
		if (ShaderGraphAssetReferences::LoadSource(materialDraft_.shaderGraph, &database, source)) {
			ShaderGraphParameterDefaults::ImportLegacyOverrides(source, materialDraft_);
		}
	}

	editingMaterialAsset_ = id;
	savedFields_ = ToJson(materialDraft_);
	contentRevision_ = database.GetContentRevision(id);
	databaseLifetime_ = database.GetCacheLifetime();
	readOnly_ = !RuntimePaths::CanWritePath(path);
	preview_.Invalidate();
	return true;
}

void Engine::InspectorMaterialSession::SaveMaterialDraft(const EditorPanelContext& context) {

	const auto fields = ToJson(materialDraft_);
	auto after = EditedDocument(savedDocument_, savedFields_, fields);
	if (after == savedDocument_) {
		if (pendingEdit_ && context.renderPipeline)
			context.renderPipeline->GetRenderAssetLibrary().InvalidateMaterial(editingMaterialAsset_);
		pendingEdit_ = false;
		diagnostic_.clear();
		return;
	}
	auto& database = *context.editorContext->assetDatabase;
	if (materialDraft_.shaderGraph && !savedDocument_.contains("graphOverrides")) {
		after["graphOverrides"] = fields["graphOverrides"];
	}
	auto command = std::make_unique<EditMaterialAssetCommand>(editingMaterialAsset_, database.GetProjectRoot(),
		savedDocument_, after);
	if (!context.host || !context.host->ExecuteEditorCommand(std::move(command))) {
		pendingEdit_ = true;
		diagnostic_ = "マテリアルを保存できませんでした コンソールの診断を確認してください";
		return;
	}
	pendingEdit_ = false;
	diagnostic_.clear();
	savedDocument_ = std::move(after);
	savedFields_ = fields;
	contentRevision_ = database.GetContentRevision(editingMaterialAsset_);
	databaseLifetime_ = database.GetCacheLifetime();
	preview_.Invalidate();
}

void Engine::InspectorMaterialSession::EndEdit(const EditorPanelContext& context) {

	if (pendingEdit_ && context.renderPipeline) {
		context.renderPipeline->GetRenderAssetLibrary().InvalidateMaterial(editingMaterialAsset_);
	}
	editingMaterialAsset_ = {};
	pendingEdit_ = false;
	diagnostic_.clear();
}
