#include "ProjectPanel.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/RuntimePaths.h>
#include <Engine/Core/Prefab/PrefabSystem.h>
#include <Engine/Core/ECS/Component/Builtin/HierarchyComponent.h>
#include <Engine/Core/ECS/Component/Builtin/NameComponent.h>
#include <Engine/Core/ECS/Component/Builtin/PrefabLinkComponent.h>
#include <Engine/Core/ECS/Component/Builtin/SceneObjectComponent.h>
#include <Engine/Editor/Command/Methods/InstantiatePrefabCommand.h>
#include <Engine/Editor/Panel/Interface/IEditorPanelHost.h>
#include <Engine/Logger/Logger.h>
#include <Engine/Utility/Json/JsonAdapter.h>
#include <Engine/Utility/Enum/EnumAdapter.h>
#include <Engine/Utility/ImGui/MyGUI.h>

// windows
#include <windows.h>
#include <shellapi.h>

// c++
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <stack>
#include <string>
#include <system_error>
#include <vector>

//============================================================================
//	ProjectPanel classMethods
//============================================================================

namespace {

	// アイテムの幅に基づいて利用可能な幅に収まる列数を計算する
	int32_t CalcGridColumnCount(float availableWidth, float itemWidth) {
		if (itemWidth <= 0.0f) {
			return 1;
		}
		return (std::max)(1, static_cast<int32_t>(availableWidth / itemWidth));
	}
	// ドラッグ&ドロップのソースを描画する
	void DrawAssetDragDropSource(const Engine::ProjectAssetEntry& asset,
		ImGuiDragDropFlags flags = ImGuiDragDropFlags_None) {

		if (ImGui::BeginDragDropSource(flags)) {

			Engine::EditorAssetDragDropPayload payload{};
			payload.assetID = asset.assetID;
			payload.assetType = asset.type;
			payload.isDirectory = 0;
			strncpy_s(payload.assetPath, asset.assetPath.c_str(), sizeof(payload.assetPath) - 1);

			ImGui::SetDragDropPayload(Engine::IEditorPanel::kProjectAssetDragDropPayloadType,
				&payload, sizeof(payload));

			ImGui::TextUnformatted(asset.displayName.c_str());
			ImGui::TextDisabled("%s", asset.assetPath.c_str());

			ImGui::EndDragDropSource();
		}
	}
	// Project内のファイル/フォルダ移動用ドラッグソースを描画する
	void DrawProjectFileMoveSource(const std::string& virtualPath, bool isDirectory, const char* displayName) {

		if (!ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
			return;
		}

		Engine::EditorAssetDragDropPayload payload{};
		payload.assetType = Engine::AssetType::Unknown;
		payload.isDirectory = isDirectory ? 1 : 0;
		strncpy_s(payload.assetPath, virtualPath.c_str(), sizeof(payload.assetPath) - 1);

		ImGui::SetDragDropPayload(Engine::IEditorPanel::kProjectAssetDragDropPayloadType, &payload, sizeof(payload));
		ImGui::TextUnformatted(displayName);
		ImGui::TextDisabled("%s", virtualPath.c_str());
		ImGui::EndDragDropSource();
	}
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
		const Engine::ProjectAssetIndex& index,
		const std::string& selectedDirectory) {

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

	// ShellExecuteWで対象を開く
	bool OpenWithShell(const std::filesystem::path& target,
		const std::wstring& parameters = std::wstring{},
		const std::filesystem::path& workingDirectory = std::filesystem::path{}) {

		if (target.empty()) {
			return false;
		}

		const wchar_t* parameterText = parameters.empty() ? nullptr : parameters.c_str();
		const wchar_t* workingDirectoryText = workingDirectory.empty() ? nullptr : workingDirectory.c_str();
		const HINSTANCE result = ::ShellExecuteW(nullptr, L"open", target.c_str(), parameterText, workingDirectoryText, SW_SHOWNORMAL);
		return reinterpret_cast<INT_PTR>(result) > 32;
	}
	// 環境変数からパスを取得する
	std::filesystem::path GetEnvironmentPath(const char* name) {

		char* value = nullptr;
		size_t valueLength = 0;
		if (_dupenv_s(&value, &valueLength, name) != 0 || value == nullptr) {
			return {};
		}

		std::filesystem::path result = value;
		std::free(value);
		return result;
	}
	// インストールされているVisual Studioのdevenv.exeを探す
	std::filesystem::path FindVisualStudioExecutable() {

		const std::array<std::filesystem::path, 2> roots = {
			GetEnvironmentPath("ProgramFiles"),
			GetEnvironmentPath("ProgramFiles(x86)"),
		};
		constexpr std::array<const char*, 3> versions = { "18", "17", "16" };
		constexpr std::array<const char*, 4> editions = { "Community", "Professional", "Enterprise", "Preview" };

		for (const auto& root : roots) {
			if (root.empty()) {
				continue;
			}
			for (const char* version : versions) {
				for (const char* edition : editions) {

					const std::filesystem::path devenv =
						root / "Microsoft Visual Studio" / version / edition / "Common7/IDE/devenv.exe";
					std::error_code ec;
					if (std::filesystem::exists(devenv, ec) && !ec) {
						return devenv;
					}
				}
			}
		}
		return {};
	}
	// ProjectPanelの表示状態を保存するパスを返す
	std::filesystem::path GetProjectPanelStatePath() {

		return Engine::RuntimePaths::GetEngineAssetPath("Config/projectPanel.exeConfig.json");
	}

	// ScriptアセットをVisual Studioで開く
	bool OpenScriptAssetInVisualStudio(const Engine::ProjectAssetEntry& asset) {

		const std::filesystem::path scriptPath = Engine::RuntimePaths::ResolveAssetPath(asset.assetPath);
		std::error_code scriptEc;
		if (scriptPath.empty() || !std::filesystem::exists(scriptPath, scriptEc) || scriptEc) {
			Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::warn,
				"ProjectPanel: script file was not found. path={}", asset.assetPath);
			return false;
		}

		const std::filesystem::path workingDirectory = Engine::RuntimePaths::GetProjectRoot();
		const std::filesystem::path visualStudio = FindVisualStudioExecutable();
		if (!visualStudio.empty()) {

			const std::wstring parameters = L"/Edit \"" + scriptPath.wstring() + L"\"";
			if (OpenWithShell(visualStudio, parameters, workingDirectory)) {
				Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::info,
					"ProjectPanel: opened script in Visual Studio. path={}", scriptPath.string());
				return true;
			}
		}

		if (OpenWithShell(scriptPath, std::wstring{}, workingDirectory)) {
			Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::info,
				"ProjectPanel: opened script via shell association because Visual Studio was not found. path={}", scriptPath.string());
			return true;
		}

		Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::warn,
			"ProjectPanel: failed to open Visual Studio for script asset.");
		return false;
	}

	// Prefab保存元のEntityツリーへPrefabLinkを設定する
	void AttachPrefabLinkToSourceTree(Engine::ECSWorld& world, const Engine::Entity& root, Engine::AssetID prefabAsset) {

		if (!world.IsAlive(root) || !prefabAsset) {
			return;
		}

		// 同じPrefab化操作で作られたEntity群をまとめるID
		const Engine::UUID prefabInstanceID = Engine::UUID::New();

		std::stack<Engine::Entity> stack;
		stack.push(root);
		while (!stack.empty()) {

			const Engine::Entity entity = stack.top();
			stack.pop();

			if (!world.IsAlive(entity) || !world.HasComponent<Engine::SceneObjectComponent>(entity)) {
				continue;
			}

			const auto& sceneObject = world.GetComponent<Engine::SceneObjectComponent>(entity);
			auto& prefabLink = world.HasComponent<Engine::PrefabLinkComponent>(entity) ?
				world.GetComponent<Engine::PrefabLinkComponent>(entity) :
				world.AddComponent<Engine::PrefabLinkComponent>(entity);

			// Prefabファイル内のLocalFileIDは保存時点のSceneObject.localFileIDと同じ値を使う
			prefabLink.prefabAsset = prefabAsset;
			prefabLink.prefabLocalFileID = sceneObject.localFileID;
			prefabLink.prefabInstanceID = prefabInstanceID;
			prefabLink.isPrefabRoot = entity == root;

			if (!world.HasComponent<Engine::HierarchyComponent>(entity)) {
				continue;
			}

			Engine::Entity child = world.GetComponent<Engine::HierarchyComponent>(entity).firstChild;
			while (child.IsValid() && world.IsAlive(child)) {

				stack.push(child);
				if (!world.HasComponent<Engine::HierarchyComponent>(child)) {
					break;
				}
				child = world.GetComponent<Engine::HierarchyComponent>(child).nextSibling;
			}
		}
	}
}

Engine::ProjectPanel::ProjectPanel(TextureUploadService& textureUploadService) {

	thumbnailCache_.Init(textureUploadService);
	LoadPersistentState();
	dirty_ = true;
}

Engine::ProjectPanel::~ProjectPanel() {

	SavePersistentState();
}

void Engine::ProjectPanel::Rebuild(AssetDatabase& database) {

	// インデックスとサムネイルキャッシュを再構築する
	database.RebuildMeta();
	assetIndex_.Rebuild(database, assetSource_);
	if (!assetIndex_.FindDirectory(selectedDirectory_)) {
		selectedDirectory_ = assetIndex_.GetRoot().virtualPath;
	}
	dirty_ = false;
}

void Engine::ProjectPanel::Draw(const EditorPanelContext& context) {

	// プロジェクトパネルの表示状態を確認
	if (!context.layoutState->showProject) {
		return;
	}

	const bool wasOpen = context.layoutState->showProject;
	if (!ImGui::Begin("Project", &context.layoutState->showProject)) {
		ImGui::End();
		if (wasOpen && !context.layoutState->showProject) {

			SavePersistentState();
		}
		return;
	}

	// アセットデータベースが利用できない場合はエラーメッセージを表示して終了
	if (!context.editorContext || !context.editorContext->assetDatabase) {
		ImGui::TextDisabled("AssetDatabase is not available.");
		ImGui::End();
		return;
	}

	// 変更された場合のインデックスの再構築
	AssetDatabase& database = *context.editorContext->assetDatabase;
	if (dirty_) {
		Rebuild(database);
	}

	ImGui::SetWindowFontScale(0.8f);
	DrawSourceSelector(context, database);
	DrawHeader(context, database);
	ImGui::SetWindowFontScale(1.0f);
	ImGui::Separator();

	ImGui::SameLine();

	// ディレクトリの内容を描画
	if (ImGui::BeginChild("##ProjectContent", ImVec2(0.0f, 0.0f), true)) {
		if (const ProjectDirectoryNode* node = assetIndex_.FindDirectory(selectedDirectory_)) {

			DrawDirectoryContents(context, database, *node);
		}
	}
	ImGui::EndChild();

	DrawCreateAssetPopup(database);

	ImGui::End();
	if (wasOpen && !context.layoutState->showProject) {

		SavePersistentState();
	}
}

void Engine::ProjectPanel::DrawHeader([[maybe_unused]] const EditorPanelContext& context, AssetDatabase& database) {

	// ルートへ戻る
	if (ImGui::Button(GetSourceRootPath())) {
		selectedDirectory_ = assetIndex_.GetRoot().virtualPath;
		selectedAsset_ = {};
	}
	DrawProjectItemMoveDropTarget(database, assetIndex_.GetRoot().virtualPath);

	const auto trail = BuildBreadcrumbTrail(assetIndex_, selectedDirectory_);

	// 中間階層はボタン
	for (size_t i = 1; i + 1 < trail.size(); ++i) {

		ImGui::SameLine();
		ImGui::TextUnformatted(">");
		ImGui::SameLine();

		if (ImGui::Button(trail[i]->name.c_str())) {
			selectedDirectory_ = trail[i]->virtualPath;
			selectedAsset_ = {};
		}
		DrawProjectItemMoveDropTarget(database, trail[i]->virtualPath);
	}

	// 現在階層はテキスト
	ImGui::SameLine();
	ImGui::TextUnformatted(">");
	ImGui::SameLine();

	const char* currentName = trail.empty() ? GetSourceRootPath() : trail.back()->name.c_str();
	ImGui::TextUnformatted(currentName);

	// 右端に Refresh
	const char* label = "Refresh";
	float buttonWidth = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
	const char* createLabel = "Create";
	float createButtonWidth = ImGui::CalcTextSize(createLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;

	float rightX = ImGui::GetWindowContentRegionMax().x - buttonWidth - createButtonWidth - 12.0f;
	const float nextX = (std::max)(ImGui::GetCursorPosX() + 16.0f, rightX);

	ImGui::SameLine(nextX);
	if (ImGui::Button(createLabel, ImVec2(createButtonWidth, 0.0f))) {
		ImGui::OpenPopup("##ProjectCreateMenu");
	}
	if (ImGui::BeginPopup("##ProjectCreateMenu")) {

		DrawCreateMenuItems(selectedDirectory_);
		ImGui::EndPopup();
	}

	ImGui::SameLine();
	if (ImGui::Button(label, ImVec2(buttonWidth, 0.0f))) {
		Rebuild(database);
		selectedAsset_ = {};
	}
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
			Rebuild(database);
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

void Engine::ProjectPanel::DrawDirectoryContents(const EditorPanelContext& context,
	AssetDatabase& database, const ProjectDirectoryNode& node) {

	float iconSize = 64.0f;
	int32_t columnCount = CalcGridColumnCount(ImGui::GetContentRegionAvail().x, iconSize + 8.0f);

	if (!ImGui::BeginTable("##ProjectGrid", columnCount, ImGuiTableFlags_SizingFixedFit)) {
		DrawDirectoryContextMenu(database, node);
		return;
	}

	// フォルダ
	for (const auto& child : node.children) {

		ImGui::TableNextColumn();
		ImGui::PushID(child->virtualPath.c_str());

		ImGui::BeginGroup();

		if (ImGui::ImageButton("##FolderButton", thumbnailCache_.GetFolderIconTextureID(), ImVec2(iconSize, iconSize),
			ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), ImVec4(0.16f, 0.16f, 0.16f, 1.0f))) {

			// Project内のフォルダ移動ではInspectorの選択状態を変更しない
			selectedDirectory_ = child->virtualPath;
			selectedAsset_ = {};
		}

		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
			// ダブルクリックでもInspectorの選択状態は維持する
			selectedDirectory_ = child->virtualPath;
			selectedAsset_ = {};
		}
		DrawProjectFileMoveSource(child->virtualPath, true, child->name.c_str());

		ImGui::SetWindowFontScale(0.8f);
		ImGui::TextWrapped("%s", child->name.c_str());
		ImGui::SetWindowFontScale(1.0f);

		if (ImGui::BeginItemTooltip()) {
			ImGui::TextUnformatted(child->virtualPath.c_str());
			ImGui::EndTooltip();
		}

		ImGui::EndGroup();
		DrawProjectItemMoveDropTarget(database, child->virtualPath);
		DrawPrefabCreateDropTarget(context, database, child->virtualPath);
		DrawFolderContextMenu(database, *child);
		ImGui::PopID();
	}

	// アセット
	for (const auto& asset : node.assets) {

		ImGui::TableNextColumn();
		ImGui::PushID(asset.assetPath.c_str());

		ImGui::BeginGroup();

		ImTextureID textureID = thumbnailCache_.GetAssetTextureID(asset.assetPath, asset.type);

		if (ImGui::ImageButton("##AssetButton", textureID, ImVec2(iconSize, iconSize),
			ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), ImVec4(0.16f, 0.16f, 0.16f, 1.0f))) {

			// 単クリックはProject内の選択だけに留め、Inspectorへは反映しない
			selectedAsset_ = asset.assetID;
		}
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
			// Inspectorへ表示するのはダブルクリック時だけにする
			selectedAsset_ = asset.assetID;
			context.editorState->SelectAsset(asset.assetID);
			HandleAssetDoubleClick(context, asset);
		}

		DrawAssetDragDropSource(asset);

		ImGui::SetWindowFontScale(0.5f);
		ImGui::TextWrapped("%s", asset.displayName.c_str());
		ImGui::SetWindowFontScale(1.0f);
		DrawAssetDragDropSource(asset, ImGuiDragDropFlags_SourceAllowNullID);

		if (ImGui::BeginItemTooltip()) {
			ImGui::Text("Path: %s", asset.assetPath.c_str());
			ImGui::Text("Type: %s", EnumAdapter<AssetType>::ToString(asset.type));
			ImGui::Text("ID:   %s", Engine::ToString(asset.assetID).c_str());
			ImGui::EndTooltip();
		}

		ImGui::EndGroup();
		DrawAssetContextMenu(context, database, asset);
		ImGui::PopID();
	}

	ImGui::EndTable();

	ImGui::Spacing();
	ImGui::Button("Drop Here", ImVec2(ImGui::GetContentRegionAvail().x, 24.0f));
	DrawPrefabCreateDropTarget(context, database, node.virtualPath);
	DrawProjectItemMoveDropTarget(database, node.virtualPath);
	DrawDirectoryContextMenu(database, node);
}

void Engine::ProjectPanel::DrawDirectoryContextMenu(AssetDatabase& database, const ProjectDirectoryNode& node) {

	if (!ImGui::BeginPopupContextWindow("ProjectDirectoryContextMenu",
		ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
		return;
	}

	if (ImGui::BeginMenu("Create")) {

		DrawCreateMenuItems(node.virtualPath);
		ImGui::EndMenu();
	}
	if (ImGui::MenuItem("Refresh")) {

		Rebuild(database);
		selectedAsset_ = {};
	}
	ImGui::EndPopup();
}

void Engine::ProjectPanel::DrawFolderContextMenu(AssetDatabase& database, const ProjectDirectoryNode& node) {

	if (!ImGui::BeginPopupContextItem("ProjectFolderContextMenu", ImGuiPopupFlags_MouseButtonRight)) {
		return;
	}

	if (ImGui::MenuItem("Open")) {

		selectedDirectory_ = node.virtualPath;
		selectedAsset_ = {};
	}
	if (ImGui::BeginMenu("Create")) {

		DrawCreateMenuItems(node.virtualPath);
		ImGui::EndMenu();
	}
	if (ImGui::MenuItem("Duplicate")) {

		ProjectAssetFileResult result = ProjectAssetFileUtility::DuplicateDirectory(assetSource_, node.virtualPath);
		RefreshAfterFileOperation(database, result);
	}
	if (ImGui::MenuItem("Delete")) {

		ProjectAssetFileResult result = ProjectAssetFileUtility::DeleteDirectory(assetSource_, node.virtualPath);
		RefreshAfterFileOperation(database, result);
	}
	ImGui::EndPopup();
}

void Engine::ProjectPanel::DrawAssetContextMenu(const EditorPanelContext& context,
	AssetDatabase& database, const ProjectAssetEntry& asset) {

	if (!ImGui::BeginPopupContextItem("ProjectAssetContextMenu", ImGuiPopupFlags_MouseButtonRight)) {
		return;
	}

	selectedAsset_ = asset.assetID;

	if (ImGui::MenuItem("Open")) {

		context.editorState->SelectAsset(asset.assetID);
		HandleAssetDoubleClick(context, asset);
	}
	if (ImGui::MenuItem("Duplicate")) {

		ProjectAssetFileResult result = ProjectAssetFileUtility::DuplicateAsset(asset);
		RefreshAfterFileOperation(database, result);
	}
	if (ImGui::MenuItem("Delete")) {

		ProjectAssetFileResult result = ProjectAssetFileUtility::DeleteAsset(asset);
		RefreshAfterFileOperation(database, result);
	}
	if (asset.type == AssetType::Prefab) {

		ImGui::Separator();
		if (ImGui::MenuItem("Instantiate Prefab", nullptr, false, context.CanEditScene())) {

			context.host->ExecuteEditorCommand(std::make_unique<InstantiatePrefabCommand>(asset.assetID));
		}
	}
	ImGui::EndPopup();
}

void Engine::ProjectPanel::DrawCreateAssetPopup(AssetDatabase& database) {

	if (requestOpenCreatePopup_) {

		ImGui::OpenPopup("Create Project Asset");
		requestOpenCreatePopup_ = false;
	}

	if (!ImGui::BeginPopupModal("Create Project Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	const char* kindLabel = ProjectAssetFileUtility::GetCreateMenuLabel(pendingCreateKind_);
	ImGui::Text("Create %s", kindLabel);
	ImGui::TextDisabled("%s", pendingCreateDirectory_.c_str());
	ImGui::Separator();

	TextInputPopupResult inputResult = MyGUI::InputTextPopupContent(
		"Name",
		createNameBuffer_,
		createErrorMessage_.empty() ? nullptr : createErrorMessage_.c_str());

	if (inputResult.submitted) {

		ProjectAssetFileResult result = ProjectAssetFileUtility::Create(
			assetSource_,
			pendingCreateDirectory_,
			pendingCreateKind_,
			createNameBuffer_);

		if (result.success) {

			createErrorMessage_.clear();
			RefreshAfterFileOperation(database, result);
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

void Engine::ProjectPanel::HandleAssetDoubleClick(const EditorPanelContext& context, const ProjectAssetEntry& asset) {

	if (asset.type == AssetType::Scene) {
		if (context.host) {
			context.host->RequestOpenScene(asset.assetID);
		}
		return;
	}
	if (asset.type != AssetType::Script) {
		return;
	}

	OpenScriptAssetInVisualStudio(asset);
}

bool Engine::ProjectPanel::SaveDroppedEntityAsPrefab(const EditorPanelContext& context,
	AssetDatabase& database, const std::string& directoryVirtualPath, const void* payloadData, int32_t payloadSize) {

	// HierarchyのドラッグペイロードはEntityの安定UUIDを保持している
	if (!context.CanEditScene() || !context.GetWorld() || payloadSize != sizeof(UUID) || payloadData == nullptr) {
		return false;
	}

	ECSWorld& world = *context.GetWorld();
	const UUID stableUUID = *static_cast<const UUID*>(payloadData);
	const Entity entity = world.FindByUUID(stableUUID);
	if (!world.IsAlive(entity)) {
		return false;
	}

	// Prefab名はEntity名を優先し、名前がなければNewPrefabにする
	std::string prefabName = "NewPrefab";
	if (world.HasComponent<NameComponent>(entity)) {

		const std::string& entityName = world.GetComponent<NameComponent>(entity).name;
		if (!entityName.empty()) {
			prefabName = entityName;
		}
	}

	ProjectAssetFileResult result = ProjectAssetFileUtility::Create(
		assetSource_,
		directoryVirtualPath,
		ProjectAssetFileKind::Prefab,
		prefabName);
	if (!result.success) {

		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ProjectPanel: failed to create prefab asset. message={}", result.message);
		return false;
	}

	PrefabSystem prefabSystem{};
	if (!prefabSystem.SavePrefab(database, world, entity, result.assetPath)) {

		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ProjectPanel: failed to save prefab. path={}", result.assetPath);
		return false;
	}

	const AssetID prefabAsset = database.ImportOrGet(result.assetPath, AssetType::Prefab);
	AttachPrefabLinkToSourceTree(world, entity, prefabAsset);

	RefreshAfterFileOperation(database, result);
	return true;
}

void Engine::ProjectPanel::DrawPrefabCreateDropTarget(const EditorPanelContext& context,
	AssetDatabase& database, const std::string& directoryVirtualPath) {

	if (!context.CanEditScene()) {
		return;
	}

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

bool Engine::ProjectPanel::MoveDroppedProjectItem(AssetDatabase& database,
	const std::string& targetDirectoryVirtualPath, const void* payloadData, int32_t payloadSize) {

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

void Engine::ProjectPanel::DrawProjectItemMoveDropTarget(AssetDatabase& database,
	const std::string& targetDirectoryVirtualPath) {

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

void Engine::ProjectPanel::BeginCreateAsset(ProjectAssetFileKind kind, const std::string& directoryVirtualPath) {

	pendingCreateKind_ = kind;
	pendingCreateDirectory_ = directoryVirtualPath;
	createNameBuffer_ = ProjectAssetFileUtility::GetDefaultName(kind);
	createErrorMessage_.clear();
	requestOpenCreatePopup_ = true;
}

void Engine::ProjectPanel::DrawCreateMenuItems(const std::string& directoryVirtualPath) {

	constexpr std::array<ProjectAssetFileKind, 9> kCreateKinds = {
		ProjectAssetFileKind::Folder,
		ProjectAssetFileKind::Script,
		ProjectAssetFileKind::Scene,
		ProjectAssetFileKind::Prefab,
		ProjectAssetFileKind::Material,
		ProjectAssetFileKind::Animation,
		ProjectAssetFileKind::Shader,
		ProjectAssetFileKind::RenderPipeline,
		ProjectAssetFileKind::Text,
	};

	for (ProjectAssetFileKind kind : kCreateKinds) {

		if (ImGui::MenuItem(ProjectAssetFileUtility::GetCreateMenuLabel(kind))) {
			BeginCreateAsset(kind, directoryVirtualPath);
		}
	}
}

void Engine::ProjectPanel::RefreshAfterFileOperation(AssetDatabase& database, const ProjectAssetFileResult& result) {

	if (!result.success) {

		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ProjectPanel: file operation failed. message={}", result.message);
		return;
	}

	Rebuild(database);

	if (result.isDirectory) {

		selectedDirectory_ = result.assetPath.empty() ? selectedDirectory_ : result.assetPath;
		selectedAsset_ = {};
		return;
	}

	if (const AssetMeta* meta = database.FindByPath(result.assetPath)) {
		selectedAsset_ = meta->guid;
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

void Engine::ProjectPanel::LoadPersistentState() {

	const std::filesystem::path path = GetProjectPanelStatePath();
	if (!JsonAdapter::Check(path.string())) {
		return;
	}

	const nlohmann::json data = JsonAdapter::Load(path.string());
	if (!data.is_object()) {
		return;
	}

	const std::string source = data.value("assetSource", "Engine");
	assetSource_ = EnumAdapter<ProjectAssetSource>::FromString(data.value("assetSource", "Engine")).value();

	const std::string defaultDirectory = assetSource_ == ProjectAssetSource::Game ? "GameAssets" : "Engine/Assets";
	selectedDirectory_ = data.value("selectedDirectory", defaultDirectory);
	if (selectedDirectory_.empty()) {

		selectedDirectory_ = defaultDirectory;
	}
	selectedAsset_ = {};
}

void Engine::ProjectPanel::SavePersistentState() const {

	nlohmann::json data = nlohmann::json::object();
	data["assetSource"] = EnumAdapter<ProjectAssetSource>::ToString(assetSource_);
	data["selectedDirectory"] = selectedDirectory_;

	JsonAdapter::Save(GetProjectPanelStatePath().string(), data);
}
