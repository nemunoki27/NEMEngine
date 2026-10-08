#include "InspectorMaterialSession.h"
#include "MaterialAssetFields.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

// c++
#include <filesystem>

namespace {

	// Materialのパス参照をAssetIDへ解決する
	void ResolveMaterialPipelineReferences(Engine::AssetDatabase& database, nlohmann::json& data) {

		if (!data.contains("passes") || !data["passes"].is_array()) {
			return;
		}
		for (auto& passJson : data["passes"]) {

			if (!passJson.is_object() || !passJson["pipeline"].is_string()) {
				continue;
			}

			const std::string text = passJson["pipeline"].get<std::string>();
			if (text.empty() || Engine::TryParseAssetGUID32Hex(text)) {
				continue;
			}

			const std::filesystem::path fullPath = database.ResolveAssetPath(text);
			if (!std::filesystem::exists(fullPath)) {
				continue;
			}

			const Engine::AssetID pipeline = database.ImportOrGet(text, Engine::AssetType::RenderPipeline);
			if (pipeline) {
				passJson["pipeline"] = Engine::ToString(pipeline);
			}
		}
	}
}

//============================================================================
//	InspectorMaterialSession classMethods
//============================================================================

void Engine::InspectorMaterialSession::DrawMaterialAssetInspector(const EditorPanelContext& context, const AssetMeta& meta) {

	// 選択Assetの編集値を読み込む
	if (!LoadMaterialDraft(context, meta)) {
		ImGui::TextDisabled("Failed to load material.");
		return;
	}

	// 編集確定時に保存して実行cacheへ反映する
	if (MaterialAssetFields::Draw(context, materialDraft_)) {
		SaveMaterialDraft(context, meta);
	}
}

bool Engine::InspectorMaterialSession::LoadMaterialDraft(const EditorPanelContext& context, const AssetMeta& meta) {

	if (materialDraftValid_ && editingMaterialAsset_ == meta.guid) {
		return true;
	}

	materialDraftValid_ = false;
	editingMaterialAsset_ = meta.guid;
	materialDraft_ = MaterialAsset{};

	if (!context.editorContext || !context.editorContext->assetDatabase) {
		return false;
	}

	const std::filesystem::path path = context.editorContext->assetDatabase->ResolveFullPath(meta.guid);
	if (path.empty()) {
		return false;
	}

	nlohmann::json data = JsonAdapter::Load(path.string(), false);
	ResolveMaterialPipelineReferences(*context.editorContext->assetDatabase, data);
	if (!FromJson(data, materialDraft_)) {
		return false;
	}
	if (!materialDraft_.guid) {
		materialDraft_.guid = meta.guid;
	}
	if (materialDraft_.name.empty()) {
		materialDraft_.name = path.stem().stem().string();
	}

	materialDraftValid_ = true;
	return true;
}

void Engine::InspectorMaterialSession::SaveMaterialDraft(const EditorPanelContext& context, const AssetMeta& meta) {

	if (!materialDraftValid_ || !context.editorContext || !context.editorContext->assetDatabase) {
		return;
	}

	materialDraft_.guid = meta.guid;

	const std::filesystem::path path = context.editorContext->assetDatabase->ResolveFullPath(meta.guid);
	if (path.empty()) {
		return;
	}

	// 保存失敗時は編集値を保持する
	if (!JsonAdapter::Save(path.string(), ToJson(materialDraft_))) {
		Logger::Output(LogType::Engine, spdlog::level::err, "Materialの保存に失敗しました path={}", path.string());
		return;
	}

	// Materialの実行cacheへ変更を通知する
	if (context.renderPipeline) {
		context.renderPipeline->ReloadMaterial(meta.guid);
	}
}
