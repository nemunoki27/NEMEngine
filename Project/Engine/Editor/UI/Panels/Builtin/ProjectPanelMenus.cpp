#include "ProjectPanel.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Editor/Core/EditorContext.h>
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Editor/Assets/Project/ProjectAssetOperations.h>
#include <Engine/Editor/Scripting/ManagedIdeLauncher.h>
#include <Engine/Editor/Utility/EditorShell.h>
#include <Engine/Editor/Assets/Importer/Font/MSDFFontGenerator.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

// c++
#include <array>

void Engine::ProjectPanel::DrawDirectoryContextMenu(
	[[maybe_unused]] AssetDatabase& database, const ProjectDirectoryNode& node) {

	if (!ImGui::BeginPopupContextWindow(
			"ProjectDirectoryContextMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
		return;
	}

	if (ImGui::BeginMenu("作成")) {

		DrawCreateMenuItems(node.virtualPath);
		ImGui::EndMenu();
	}
	ImGui::EndPopup();
}

void Engine::ProjectPanel::DrawFolderContextMenu(
	const EditorPanelContext& context, AssetDatabase& database, const ProjectDirectoryNode& node) {

	if (!ImGui::BeginPopupContextItem("ProjectFolderContextMenu", ImGuiPopupFlags_MouseButtonRight)) {
		return;
	}

	if (ImGui::MenuItem("開く")) {

		selectedDirectory_ = node.virtualPath;
		selectedAsset_ = {};
	}
	if (ImGui::MenuItem("名前変更")) {

		BeginRenameDirectory(node);
	}
	if (ImGui::BeginMenu("作成")) {

		DrawCreateMenuItems(node.virtualPath);
		ImGui::EndMenu();
	}
	if (ImGui::MenuItem("複製")) {

		ProjectAssetFileResult result =
			ProjectAssetFileUtility::DuplicateDirectory(assetSource_, node.virtualPath, context.editorContext->sceneStorage);
		RefreshAfterFileOperation(database, result);
	}
	if (ImGui::MenuItem("削除")) {

		ProjectAssetFileResult result = ProjectAssetFileUtility::DeleteDirectory(
			assetSource_, node.virtualPath, database, context.editorContext->sceneStorage);
		if (!result.success) {
			sceneStorageInspector_.ReportFailure(result.message);
		}
		RefreshAfterFileOperation(database, result);
	}
	ImGui::EndPopup();
}

void Engine::ProjectPanel::DrawAssetContextMenu(
	const EditorPanelContext& context, AssetDatabase& database, const ProjectAssetEntry& asset) {

	if (!ImGui::BeginPopupContextItem("ProjectAssetContextMenu", ImGuiPopupFlags_MouseButtonRight)) {
		return;
	}

	selectedAsset_ = asset.assetID;

	if (ImGui::MenuItem("名前変更")) {

		BeginRenameAsset(asset);
	}
	ImGui::Separator();
	if (ImGui::MenuItem("エクスプローラーで開く")) {

		const std::filesystem::path assetPath = RuntimePaths::ResolveAssetPath(asset.assetPath);
		EditorShell::OpenDirectory(assetPath.parent_path());
	}
	if (ImGui::MenuItem("開く")) {

		context.editorState->SelectAsset(asset.assetID);
		HandleAssetDoubleClick(context, asset);
	}
	if (ImGui::MenuItem("複製")) {

		ProjectAssetFileResult result = ProjectAssetFileUtility::DuplicateAsset(asset, context.editorContext->sceneStorage);
		RefreshAfterFileOperation(database, result);
	}
	if (ImGui::MenuItem("削除")) {

		// 削除前に参照元を集めて確認ポップアップを開く
		pendingDeleteAsset_ = asset;
		deleteErrorMessage_.clear();
		pendingDeleteReferencers_.clear();
		for (const AssetID& referencer : database.FindReferencers(asset.assetID)) {

			const AssetMeta* meta = database.Find(referencer);
			pendingDeleteReferencers_.emplace_back(meta ? meta->assetPath : ToString(referencer));
		}
		requestOpenDeletePopup_ = true;
	}
	// アセット種別固有の右クリック項目はRegistryへ委ねる
	if (const AssetActionDescriptor* action = assetActionRegistry_.Find(asset.type)) {
		if (action->onContextMenu) {
			action->onContextMenu(context, asset);
		}
	}
	// 文字集合の変更後にFontを再生成する
	if (MSDFFontGenerator::IsFontSourceExtension(asset.assetPath)) {

		ImGui::Separator();
		if (ImGui::MenuItem("フォントデータ再生成")) {

			// Fontの生成物を作り直す
			const std::filesystem::path sourcePath = RuntimePaths::ResolveAssetPath(asset.assetPath);
			const MSDFFontGenerator::Result result = MSDFFontGenerator::EnsureGenerated(database, sourcePath, true);
			if (!result.success) {

				Logger::Output(
					LogType::Engine, spdlog::level::warn, "ProjectPanel: フォントの再生成に失敗しました {}", result.message);
			} else {

				// Atlasの転送後にFontの世代を揃える
				const std::filesystem::path atlasPath = database.ResolveFullPath(result.atlasAssetID);
				TextureUploadService& textureUploadService = context.graphicsCore->GetTextureUploadService();
				textureUploadService.RequestReloadByFile(atlasPath);
				textureUploadService.WaitAll();
				context.renderPipeline->ReloadAsset(database, result.fontAssetID);

				Logger::Output(LogType::Engine, spdlog::level::info, "ProjectPanel: フォントデータを再生成しました path={}",
					result.fontAssetPath);
			}
		}
	}
	ImGui::EndPopup();
}

void Engine::ProjectPanel::DrawCreateAssetPopup(AssetDatabase& database) {

	if (requestOpenCreatePopup_) {

		ImGui::OpenPopup("アセットの作成");
		requestOpenCreatePopup_ = false;
	}

	if (!MyGUI::BeginPopupModal("アセットの作成", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	const char* kindLabel = ProjectAssetFileUtility::GetCreateMenuLabel(pendingCreateKind_);
	ImGui::Text("作成 %s", kindLabel);
	ImGui::TextDisabled("%s", pendingCreateDirectory_.c_str());
	ImGui::Separator();

	TextInputPopupResult inputResult = MyGUI::InputTextPopupContent(
		"名前", createNameBuffer_, createErrorMessage_.empty() ? nullptr : createErrorMessage_.c_str());

	if (inputResult.submitted) {

		ProjectAssetFileResult result =
			ProjectAssetFileUtility::Create(assetSource_, pendingCreateDirectory_, pendingCreateKind_, createNameBuffer_);

		if (result.success) {

			createErrorMessage_.clear();
			RefreshAfterFileOperation(database, result);
			// 作成したScriptをIDEで開く
			if (pendingCreateKind_ == ProjectAssetFileKind::Script && !result.fullPath.empty()) {
				ManagedIdeLauncher::OpenFile(result.fullPath, 1, 1);
			}
			ImGui::CloseCurrentPopup();
		} else {

			createErrorMessage_ = result.message.empty() ? "Failed to create asset." : result.message;
		}
	}
	if (inputResult.canceled) {

		createErrorMessage_.clear();
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}

void Engine::ProjectPanel::DrawRenameAssetPopup(const EditorPanelContext& context, AssetDatabase& database) {

	if (requestOpenRenamePopup_) {

		ImGui::OpenPopup("アセットの名前変更");
		requestOpenRenamePopup_ = false;
	}

	if (!MyGUI::BeginPopupModal("アセットの名前変更", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	ImGui::Text(pendingRenameIsDirectory_ ? "フォルダ名変更" : "アセット名変更");
	ImGui::TextDisabled(
		"%s", pendingRenameIsDirectory_ ? pendingRenameDirectoryPath_.c_str() : pendingRenameAsset_.assetPath.c_str());
	if (!renameProtectedSuffix_.empty()) {
		ImGui::TextDisabled("変更不可拡張子: %s", renameProtectedSuffix_.c_str());
	}
	ImGui::Separator();

	TextInputPopupResult inputResult = MyGUI::InputTextPopupContent(
		"名前", renameNameBuffer_, renameErrorMessage_.empty() ? nullptr : renameErrorMessage_.c_str());

	if (inputResult.submitted) {

		ProjectAssetFileResult result =
			pendingRenameIsDirectory_
				? ProjectAssetFileUtility::RenameDirectory(assetSource_, pendingRenameDirectoryPath_, renameNameBuffer_)
				: ProjectAssetFileUtility::RenameAsset(pendingRenameAsset_, renameNameBuffer_);

		if (result.success) {

			renameErrorMessage_.clear();
			RefreshAfterFileOperation(database, result);
			if (!pendingRenameIsDirectory_ && pendingRenameAsset_.type == AssetType::Scene) {
				ProjectAssetOperations::UpdateLoadedSceneName(context, pendingRenameAsset_.assetID, result.fullPath);
			}
			ImGui::CloseCurrentPopup();
		} else {

			renameErrorMessage_ = result.message.empty() ? "ファイル名の変更に失敗しました" : result.message;
		}
	}
	if (inputResult.canceled) {

		renameErrorMessage_.clear();
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}

void Engine::ProjectPanel::DrawDeleteAssetPopup(const EditorPanelContext& context, AssetDatabase& database) {

	if (requestOpenDeletePopup_) {

		ImGui::OpenPopup("アセットの削除");
		requestOpenDeletePopup_ = false;
	}

	if (!MyGUI::BeginPopupModal("アセットの削除", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	ImGui::Text("アセット削除");
	ImGui::TextDisabled("%s", pendingDeleteAsset_.assetPath.c_str());
	for (const auto& referencer : pendingDeleteReferencers_) {
		ImGui::TextWrapped("参照元: %s", referencer.c_str());
	}
	if (!deleteErrorMessage_.empty()) {
		ImGui::TextWrapped("%s", deleteErrorMessage_.c_str());
	}
	ImGui::Separator();

	if (ImGui::Button("削除")) {

		ProjectAssetFileResult result =
			ProjectAssetFileUtility::DeleteAsset(pendingDeleteAsset_, database, context.editorContext->sceneStorage);
		deleteErrorMessage_ = result.message;
		RefreshAfterFileOperation(database, result);
		if (result.success) {
			pendingDeleteReferencers_.clear();
			ImGui::CloseCurrentPopup();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("キャンセル")) {

		pendingDeleteReferencers_.clear();
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}

void Engine::ProjectPanel::BeginCreateAsset(ProjectAssetFileKind kind, const std::string& directoryVirtualPath) {

	pendingCreateKind_ = kind;
	pendingCreateDirectory_ = directoryVirtualPath;
	createNameBuffer_ = ProjectAssetFileUtility::GetDefaultName(kind);
	createErrorMessage_.clear();
	requestOpenCreatePopup_ = true;
}

void Engine::ProjectPanel::BeginRenameAsset(const ProjectAssetEntry& asset) {

	pendingRenameIsDirectory_ = false;
	pendingRenameAsset_ = asset;
	renameNameBuffer_ = ProjectAssetFileUtility::GetEditableAssetName(asset);
	renameProtectedSuffix_ = ProjectAssetFileUtility::GetProtectedAssetSuffix(asset);
	renameErrorMessage_.clear();
	requestOpenRenamePopup_ = true;
}

void Engine::ProjectPanel::BeginRenameDirectory(const ProjectDirectoryNode& node) {

	pendingRenameIsDirectory_ = true;
	pendingRenameDirectoryPath_ = node.virtualPath;
	renameNameBuffer_ = node.name;
	renameProtectedSuffix_.clear();
	renameErrorMessage_.clear();
	requestOpenRenamePopup_ = true;
}

void Engine::ProjectPanel::DrawCreateMenuItems(const std::string& directoryVirtualPath) {

	// 作成項目の数を列挙内容から決める
	constexpr std::array kCreateKinds{
		ProjectAssetFileKind::Folder,
		ProjectAssetFileKind::Script,
		ProjectAssetFileKind::Scene,
		ProjectAssetFileKind::Prefab,
		ProjectAssetFileKind::Material,
		ProjectAssetFileKind::AnimationClip,
		ProjectAssetFileKind::AnimationController,
		ProjectAssetFileKind::Shader,
		ProjectAssetFileKind::ShaderGraph,
		ProjectAssetFileKind::RenderPipeline,
		ProjectAssetFileKind::RenderPasses,
		ProjectAssetFileKind::RenderTexture,
		ProjectAssetFileKind::Text,
	};

	for (ProjectAssetFileKind kind : kCreateKinds) {

		if (ImGui::MenuItem(ProjectAssetFileUtility::GetCreateMenuLabel(kind))) {
			BeginCreateAsset(kind, directoryVirtualPath);
		}
	}
}
