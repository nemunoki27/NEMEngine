#include "RenderTextureAssetInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

// c++
#include <algorithm>
#include <fstream>

void Engine::RenderTextureAssetInspectorDrawer::Load(const EditorPanelContext& context, const AssetMeta& meta) {

	selectedAsset_ = meta.guid;
	loaded_ = false;
	status_.clear();
	try {
		std::ifstream stream(context.editorContext->assetDatabase->ResolveFullPath(meta.guid));
		const auto document = nlohmann::json::parse(stream);
		loaded_ = FromJson(document, saved_);
		if (loaded_) {
			saved_.guid = meta.guid;
			draft_ = saved_;
		}
	} catch (const std::exception&) {
		status_ = "RenderTexture設定を読み込めません";
	}
}

void Engine::RenderTextureAssetInspectorDrawer::Draw(const EditorPanelContext& context, const AssetMeta& meta) {

	if (selectedAsset_ != meta.guid) {
		Load(context, meta);
	}
	ImGui::SeparatorText("RenderTexture設定");
	ImGui::BeginDisabled(!loaded_);
	int32_t width = static_cast<int32_t>(draft_.width);
	int32_t height = static_cast<int32_t>(draft_.height);
	MyGUI::DragInt("幅", width, { .dragSpeed = 1.0f, .minValue = 1, .maxValue = 16384 });
	MyGUI::DragInt("高さ", height, { .dragSpeed = 1.0f, .minValue = 1, .maxValue = 16384 });
	draft_.width = static_cast<uint32_t>(std::clamp(width, 1, 16384));
	draft_.height = static_cast<uint32_t>(std::clamp(height, 1, 16384));
	ImGui::EndDisabled();
	const float buttonWidth = (std::max)(1.0f, (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f);
	ImGui::BeginDisabled(!loaded_ || !HasPendingChanges());
	if (ImGui::Button("保存", ImVec2(buttonWidth, 0.0f))) {
		ApplyPendingChanges(context);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("再読み込み", ImVec2(buttonWidth, 0.0f))) {
		Load(context, meta);
	}
	if (!status_.empty()) {
		ImGui::TextWrapped("%s", status_.c_str());
	}
}

bool Engine::RenderTextureAssetInspectorDrawer::ApplyPendingChanges(const EditorPanelContext& context) {

	if (!loaded_ || !context.editorContext || !context.editorContext->assetDatabase) {
		return false;
	}
	AssetDatabase& database = *context.editorContext->assetDatabase;
	// 完成した設定を保存してから描画cacheを更新する
	if (!StorageFileUtility::WriteBytes(database.ResolveFullPath(selectedAsset_), JsonAdapter::SerializeCanonical(ToJson(draft_), 2))) {
		status_ = "RenderTexture設定を保存できません";
		return false;
	}
	database.NotifyContentChanged(selectedAsset_);
	if (context.renderPipeline) {
		context.renderPipeline->ReloadAsset(database, selectedAsset_);
	}
	saved_ = draft_;
	status_ = "保存しました";
	return true;
}
