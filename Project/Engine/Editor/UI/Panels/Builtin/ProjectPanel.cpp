#include "ProjectPanel.h"
#include <Engine/Editor/Assets/Project/ProjectAssetOperations.h>

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Platform/Input/InputSystem.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Editor/Core/EditorContext.h>
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Editor/Utility/EditorTextureHelper.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>


//============================================================================
//	ProjectPanel classMethods
//============================================================================
namespace {

	// 仮想パスを'/'または'\'で分割して、ディレクトリ名のリストを返す
	std::vector<std::string> SplitVirtualPath(const std::string& path) {

		std::vector<std::string> result;
		std::string current;

		for (char c : path) {
			if (c == '/' || c == '\\') {
				if (!current.empty()) {
					result.emplace_back(std::move(current));
					current.clear();
				}
			} else {
				current.push_back(c);
			}
		}
		if (!current.empty()) {
			result.emplace_back(std::move(current));
		}
		return result;
	}
	// 仮想パスを分割して、インデックスから対応するディレクトリノードのリストを構築する
	std::vector<const Engine::ProjectDirectoryNode*> BuildBreadcrumbTrail(
		const Engine::ProjectAssetIndex& index, const std::string& selectedDirectory) {

		std::vector<const Engine::ProjectDirectoryNode*> trail;

		const Engine::ProjectDirectoryNode& root = index.GetRoot();
		trail.push_back(&root);

		if (selectedDirectory == root.virtualPath) {
			return trail;
		}

		std::string currentPath = root.virtualPath;
		const std::string prefix = root.virtualPath + "/";
		if (selectedDirectory.rfind(prefix, 0) != 0) {
			return trail;
		}

		const std::vector<std::string> parts = SplitVirtualPath(selectedDirectory.substr(prefix.size()));
		for (const std::string& part : parts) {

			currentPath += "/" + part;

			if (const auto* node = index.FindDirectory(currentPath)) {
				trail.push_back(node);
			} else {
				break;
			}
		}

		return trail;
	}

}

Engine::ProjectPanel::ProjectPanel(TextureUploadService& textureUploadService, const std::string& instanceID,
	bool primaryInstance, const std::string& displayName)
	: displayName_(displayName) {

	ConfigureInstance("Project", instanceID, primaryInstance);
	thumbnailCache_.Init(textureUploadService);
	RegisterAssetActions();
	if (primaryInstance) {
		LoadPersistentState();
	}
	dirty_ = true;
}

Engine::ProjectPanel::~ProjectPanel() = default;

void Engine::ProjectPanel::RebuildIndex(const AssetDatabase& database) {

	// 共有AssetDatabaseを変更せず、このパネルの表示インデックスだけを更新する
	assetIndex_.Rebuild(database, assetSource_);
	if (!assetIndex_.FindDirectory(selectedDirectory_)) {
		selectedDirectory_ = assetIndex_.GetRoot().virtualPath;
	}
	// 取り込んだ構造リビジョンを控えておき、外部のファイル追加削除との差分で再構築を判断する
	lastSeenStructureRevision_ = database.GetStructureRevision();
	dirty_ = false;
}

void Engine::ProjectPanel::RefreshDatabaseAndIndex(AssetDatabase& database) {

	database.RebuildMeta();
	RebuildIndex(database);
}

void Engine::ProjectPanel::HandleExternalFileDrop([[maybe_unused]] const EditorPanelContext& context, AssetDatabase& database) {

	Input* input = Input::GetInstance();
	if (!input) {
		return;
	}
	std::vector<std::string> droppedPaths;
	Vector2 dropPoint{};
	if (!input->PeekDroppedFiles(droppedPaths, dropPoint)) {
		return;
	}

	// ドロップ位置がProjectウィンドウ内のときだけ取り込む、それ以外は破棄する
	const ImVec2 windowPos = ImGui::GetWindowPos();
	const ImVec2 windowSize = ImGui::GetWindowSize();
	const bool insidePanel = dropPoint.x >= windowPos.x && dropPoint.x <= windowPos.x + windowSize.x &&
							 dropPoint.y >= windowPos.y && dropPoint.y <= windowPos.y + windowSize.y;
	if (!insidePanel) {
		return;
	}
	// ドロップ先のProjectパネルだけがファイルを消費する
	if (!input->TakeDroppedFiles(droppedPaths, dropPoint)) {
		return;
	}

	// カレントフォルダへコピー取り込みする、.metaはRebuildMetaで自動発番される
	// フォルダがドロップされたときは中身ごと再帰コピーする
	bool imported = false;
	for (const std::string& path : droppedPaths) {

		try {
			std::error_code ec;
			const std::filesystem::path externalPath = Algorithm::PathFromUTF8(path);
			const ProjectAssetFileResult result =
				std::filesystem::is_directory(externalPath, ec)
					? ProjectAssetFileUtility::ImportExternalDirectory(assetSource_, selectedDirectory_, externalPath)
					: ProjectAssetFileUtility::ImportExternalFile(assetSource_, selectedDirectory_, externalPath);
			if (result.success) {
				imported = true;
			}
		} catch (const std::exception& exception) {

			Logger::Output(LogType::Engine, spdlog::level::warn,
				"[ProjectPanel] DropされたPathのImportに失敗しました path={} 内容={}", path, exception.what());
		}
	}
	if (imported) {
		try {
			RefreshDatabaseAndIndex(database);
		} catch (const std::exception& exception) {

			Logger::Output(LogType::Engine, spdlog::level::warn, "[ProjectPanel] Import後のAsset更新に失敗しました 内容={}",
				exception.what());
		}
	}
}

void Engine::ProjectPanel::Draw(const EditorPanelContext& context) {

	// プロジェクトパネルの表示状態を確認
	bool* open = ResolveOpenState(&context.layoutState->showProject);
	if (!*open) {
		return;
	}

	const std::string windowName = MakeWindowName(displayName_);
	ApplyInitialDock();
	const bool visible = ImGui::Begin(windowName.c_str(), open);
	if (context.host && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
		context.host->NotifyEditorCommandPanelFocused(EditorCommandPanelKind::Project);
	}
	if (!visible) {
		DrawTitleBarContextMenu(context);
		ImGui::End();
		return;
	}
	DrawTitleBarContextMenu(context);

	// アセットデータベースが利用できない場合はエラーメッセージを表示して終了
	if (!context.editorContext || !context.editorContext->assetDatabase) {
		ImGui::TextDisabled("AssetDatabase is not available.");
		ImGui::End();
		return;
	}

	// 自前のdirtyか、外部のファイル追加削除で進んだ構造リビジョンの差分でインデックスを再構築する
	AssetDatabase& database = *context.editorContext->assetDatabase;
	thumbnailCache_.SetAssetDatabase(&database);
	if (dirty_ || database.GetStructureRevision() != lastSeenStructureRevision_) {
		RebuildIndex(database);
	}

	// 外部エクスプローラーからドロップされたファイルをカレントフォルダへ取り込む
	HandleExternalFileDrop(context, database);

	ImGui::SetWindowFontScale(0.8f);
	DrawSourceSelector(context, database);
	sceneStorageInspector_.DrawSceneStoragePopup(context, database);
	assetDiagnostics_.Draw(database, !context.editorContext->isPlaying && !context.editorContext->isPrefabEditing);
	DrawSearchBar(context);
	ImGui::SetWindowFontScale(1.0f);
	ImGui::Separator();

	// 残り領域の高さを取り、内容表示の子領域へ渡す
	const float regionHeight = ImGui::GetContentRegionAvail().y;

	// 選択ディレクトリの内容をアイコンで描画する、検索中は一致ファイルの一覧へ切り替える
	if (ImGui::BeginChild("##ProjectContent", ImVec2(0.0f, regionHeight), true)) {

		// パンくずは右側コンテンツの一番上に置く
		ImGui::SetWindowFontScale(0.8f);
		DrawBreadcrumb(context, database);
		ImGui::SetWindowFontScale(1.0f);
		ImGui::Separator();

		if (fileSearchFilter_.IsActive()) {

			DrawSearchResults(context, database);
		} else if (const ProjectDirectoryNode* node = assetIndex_.FindDirectory(selectedDirectory_)) {

			DrawDirectoryContents(context, database, *node);
		}
	}
	ImGui::EndChild();

	DrawCreateAssetPopup(database);
	DrawRenameAssetPopup(context, database);
	DrawDeleteAssetPopup(context, database);
	ApplyPendingFileOperationRefresh(database);

	ImGui::End();
}

void Engine::ProjectPanel::DrawSearchBar(const EditorPanelContext& context) {

	// 入力枠の左端に虫眼鏡アイコンを重ねて、その右に入力文字が並ぶようにする
	const ImTextureID searchIcon = EditorTextureHelper::GetSearchIcon(context.graphicsCore->GetTextureUploadService());
	fileSearchFilter_.DrawInput("##ProjectFileSearch", searchIcon, "ファイル検索...");
}

void Engine::ProjectPanel::DrawBreadcrumb([[maybe_unused]] const EditorPanelContext& context, AssetDatabase& database) {

	// パンくずからの移動でも検索状態は解除する
	auto navigate = [&](const std::string& virtualPath) {
		selectedDirectory_ = virtualPath;
		selectedAsset_ = {};
		fileSearchFilter_.Clear();
	};

	// ルートへ戻る
	if (ImGui::Button(GetSourceRootPath())) {
		navigate(assetIndex_.GetRoot().virtualPath);
	}
	DrawProjectItemMoveDropTarget(database, assetIndex_.GetRoot().virtualPath);

	const auto trail = BuildBreadcrumbTrail(assetIndex_, selectedDirectory_);

	// 中間階層はボタン
	for (size_t i = 1; i + 1 < trail.size(); ++i) {

		ImGui::SameLine();
		ImGui::TextWrapped("%s", ">");
		ImGui::SameLine();

		if (ImGui::Button(trail[i]->name.c_str())) {
			navigate(trail[i]->virtualPath);
		}
		DrawProjectItemMoveDropTarget(database, trail[i]->virtualPath);
	}

	// 現在階層はテキスト
	ImGui::SameLine();
	ImGui::TextWrapped("%s", ">");
	ImGui::SameLine();

	const char* currentName = trail.empty() ? GetSourceRootPath() : trail.back()->name.c_str();
	ImGui::TextUnformatted(currentName);
}

void Engine::ProjectPanel::DrawSourceSelector([[maybe_unused]] const EditorPanelContext& context, AssetDatabase& database) {

	auto drawSourceButton = [&](ProjectAssetSource source, const char* label) {
		const bool selected = assetSource_ == source;
		if (selected) {
			ImGui::BeginDisabled();
		}
		if (ImGui::Button(label)) {

			assetSource_ = source;
			selectedDirectory_ = source == ProjectAssetSource::Engine ? "Engine/Assets" : "GameAssets";
			selectedAsset_ = {};
			RebuildIndex(database);
		}
		if (selected) {
			ImGui::EndDisabled();
		}
	};

	drawSourceButton(ProjectAssetSource::Engine, "Engine");
	ImGui::SameLine();
	drawSourceButton(ProjectAssetSource::Game, "Game");
	ImGui::Separator();
}

bool Engine::ProjectPanel::SaveDroppedEntityAsPrefab(const EditorPanelContext& context, AssetDatabase& database,
	const std::string& directoryVirtualPath, const void* payloadData, int32_t payloadSize) {

	// HierarchyのドラッグペイロードはEntityの安定UUIDを保持している
	if (!context.GetWorld() || payloadSize != sizeof(UUID) || payloadData == nullptr) {
		return false;
	}

	ECSWorld& world = *context.GetWorld();
	const UUID stableUUID = *static_cast<const UUID*>(payloadData);
	const Entity entity = world.FindByUUID(stableUUID);
	if (!world.IsAlive(entity)) {
		return false;
	}

	ProjectAssetFileResult result;
	if (!ProjectAssetOperations::SavePrefab(database, world, entity, assetSource_, directoryVirtualPath, result)) {
		return false;
	}
	RefreshAfterFileOperation(database, result);
	return true;
}

void Engine::ProjectPanel::DrawPrefabCreateDropTarget(
	const EditorPanelContext& context, AssetDatabase& database, const std::string& directoryVirtualPath) {

	// Projectのフォルダまたは空白へHierarchy EntityをドロップするとPrefabを作成する
	if (!ImGui::BeginDragDropTarget()) {
		return;
	}
	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kHierarchyDragDropPayloadType)) {
		if (payload->IsDelivery()) {

			SaveDroppedEntityAsPrefab(context, database, directoryVirtualPath, payload->Data, payload->DataSize);
		}
	}
	ImGui::EndDragDropTarget();
}

bool Engine::ProjectPanel::MoveDroppedProjectItem(
	AssetDatabase& database, const std::string& targetDirectoryVirtualPath, const void* payloadData, int32_t payloadSize) {

	if (payloadData == nullptr || payloadSize != sizeof(EditorAssetDragDropPayload)) {
		return false;
	}

	const auto& payload = *static_cast<const EditorAssetDragDropPayload*>(payloadData);
	const std::string sourcePath = payload.assetPath;
	if (sourcePath.empty() || sourcePath == targetDirectoryVirtualPath) {
		return false;
	}

	ProjectAssetFileResult result{};
	if (payload.isDirectory != 0) {

		result = ProjectAssetFileUtility::MoveDirectory(assetSource_, sourcePath, targetDirectoryVirtualPath);
	} else {

		const ProjectAssetEntry* asset = assetIndex_.FindAssetByPath(sourcePath);
		if (!asset) {
			return false;
		}
		result = ProjectAssetFileUtility::MoveAsset(*asset, assetSource_, targetDirectoryVirtualPath);
	}

	RefreshAfterFileOperation(database, result);
	return result.success;
}

void Engine::ProjectPanel::DrawProjectItemMoveDropTarget(
	AssetDatabase& database, const std::string& targetDirectoryVirtualPath) {

	if (!ImGui::BeginDragDropTarget()) {
		return;
	}
	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectAssetDragDropPayloadType)) {
		if (payload->IsDelivery()) {

			MoveDroppedProjectItem(database, targetDirectoryVirtualPath, payload->Data, payload->DataSize);
		}
	}
	ImGui::EndDragDropTarget();
}

void Engine::ProjectPanel::RefreshAfterFileOperation(
	[[maybe_unused]] AssetDatabase& database, const ProjectAssetFileResult& result) {

	if (!result.success) {

		Logger::Output(
			LogType::Engine, spdlog::level::warn, "ProjectPanel: ファイル操作に失敗しました 内容={}", result.message);
		return;
	}

	// ProjectAssetIndexの参照を使っている描画中にRebuildすると、走査中のasset/nodeが破棄される
	pendingFileOperationResult_ = result;
	hasPendingFileOperationRefresh_ = true;
	dirty_ = true;
}

void Engine::ProjectPanel::ApplyPendingFileOperationRefresh(AssetDatabase& database) {

	if (!hasPendingFileOperationRefresh_) {
		return;
	}

	const ProjectAssetFileResult result = pendingFileOperationResult_;
	pendingFileOperationResult_ = {};
	hasPendingFileOperationRefresh_ = false;

	RefreshDatabaseAndIndex(database);

	if (result.isDirectory) {

		selectedDirectory_ = result.assetPath.empty() ? selectedDirectory_ : result.assetPath;
		selectedAsset_ = {};
		return;
	}

	if (const AssetMeta* meta = database.FindByPath(result.assetPath)) {
		selectedAsset_ = meta->guid;
	} else {
		selectedAsset_ = {};
	}
}

const char* Engine::ProjectPanel::GetSourceRootPath() const {

	switch (assetSource_) {
	case ProjectAssetSource::Engine:
		return "Engine/Assets";
	case ProjectAssetSource::Game:
		return "GameAssets";
	}
	return "Engine/Assets";
}
