#include "ProjectPanel.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Platform/Input/InputSystem.h>
#include <Engine/Editor/Scripting/ManagedIdeLauncher.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabSystem.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Editor/Commands/Entity/EditorEntitySnapshot.h>
#include <Engine/Editor/Core/EditorContext.h>
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>
#include <Engine/Core/Rendering/Textures/TextureAssetResolver.h>
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Editor/Commands/Entity/InstantiatePrefabCommand.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Editor/Tools/Core/EditorToolContext.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>

// assimp
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

// windows
#include <windows.h>
#include <shellapi.h>

// c++
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <initializer_list>
#include <stack>
#include <string>
#include <system_error>
#include <vector>

#include <Engine/Editor/Assets/Importer/Model/AssimpMaterialTextureExtractor.h>

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

	// .cs openは共通IDE launcherのManagedIdeLauncherへ統一した
	// 以前のOpenWithShell / FindVisualStudioExecutableはlauncher側へ移管したため削除

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

		// .cs open / Compiler Error List jump / Script exceptionのstack jumpを共通IDE launcherに統一する
		// 既定は関連付けのSystemDefaultでProjectSettings/ManagedScriptingEditor.jsonでexecutable指定も可能
		return Engine::ManagedIdeLauncher::OpenFile(scriptPath, 1, 1);
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

void Engine::ProjectPanel::HandleExternalFileDrop([[maybe_unused]] const EditorPanelContext& context, AssetDatabase& database) {

	Input* input = Input::GetInstance();
	if (!input) {
		return;
	}
	std::vector<std::string> droppedPaths;
	Vector2 dropPoint{};
	if (!input->TakeDroppedFiles(droppedPaths, dropPoint)) {
		return;
	}

	// クライアント座標のドロップ点をImGui座標へ合わせる、viewports無効ではmain viewportのposは0
	const ImVec2 viewportPos = ImGui::GetMainViewport()->Pos;
	const float dropX = dropPoint.x + viewportPos.x;
	const float dropY = dropPoint.y + viewportPos.y;

	// ドロップ位置がProjectウィンドウ内のときだけ取り込む、それ以外は破棄する
	const ImVec2 windowPos = ImGui::GetWindowPos();
	const ImVec2 windowSize = ImGui::GetWindowSize();
	const bool insidePanel =
		dropX >= windowPos.x && dropX <= windowPos.x + windowSize.x &&
		dropY >= windowPos.y && dropY <= windowPos.y + windowSize.y;
	if (!insidePanel) {
		return;
	}

	// カレントフォルダへコピー取り込みする、.metaはRebuildで自動発番される
	bool imported = false;
	for (const std::string& path : droppedPaths) {

		const ProjectAssetFileResult result =
			ProjectAssetFileUtility::ImportExternalFile(assetSource_, selectedDirectory_, path);
		if (result.success) {
			imported = true;
		}
	}
	if (imported) {
		Rebuild(database);
	}
}

void Engine::ProjectPanel::Draw(const EditorPanelContext& context) {

	// 選択がプレファブ編集インスタンスから外れていたら編集を終了する、パネル表示状態に依らず毎フレーム確認する
	UpdatePrefabEditLifecycle(context);

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

	// 外部エクスプローラーからドロップされたファイルをカレントフォルダへ取り込む
	HandleExternalFileDrop(context, database);

	ImGui::SetWindowFontScale(0.8f);
	DrawSourceSelector(context, database);
	DrawHeader(context, database);
	ImGui::SetWindowFontScale(1.0f);
	ImGui::Separator();

	// 左右の子領域の高さを合わせるため残り高さを先に取っておく
	const float regionHeight = ImGui::GetContentRegionAvail().y;

	// 左ツリーが極端に潰れないよう、また右を潰しきらないよう幅の最小最大を制約する
	const float regionWidth = ImGui::GetContentRegionAvail().x;
	const float maxTreeWidth = (std::max)(120.0f, regionWidth - 140.0f);
	ImGui::SetNextWindowSizeConstraints(ImVec2(120.0f, regionHeight), ImVec2(maxTreeWidth, regionHeight));

	// 左側にUnity風のフォルダ階層ツリーと検索ボックスを表示する
	// 子の右枠自体をドラッグして幅を変えられるようにResizeXを付ける
	if (ImGui::BeginChild("##ProjectFolderTree", ImVec2(folderTreeWidth_, regionHeight),
		ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX)) {

		DrawFolderTree(database);
	}
	ImGui::EndChild();

	ImGui::SameLine();

	// 右側に選択ディレクトリの内容をアイコンで描画する
	if (ImGui::BeginChild("##ProjectContent", ImVec2(0.0f, regionHeight), true)) {
		if (const ProjectDirectoryNode* node = assetIndex_.FindDirectory(selectedDirectory_)) {

			DrawDirectoryContents(context, database, *node);
		}
	}
	ImGui::EndChild();

	DrawCreateAssetPopup(database);
	DrawRenameAssetPopup(database);
	DrawDeleteAssetPopup(database);
	ApplyPendingFileOperationRefresh(database);

	ImGui::End();
	if (wasOpen && !context.layoutState->showProject) {

		SavePersistentState();
	}

	if (context.layoutState->showProject && showModelPreviewSettingsWindow_) {

		DrawModelPreviewSettingsWindow();
	}
}

void Engine::ProjectPanel::DrawEditorTool([[maybe_unused]] const EditorToolContext& context) {

	// ProjectPanelはToolPanel上の独立ウィンドウを持たず、RenderTexture作成機能だけを利用する
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

	// 右端にRefresh
	const char* label = "Refresh";
	float buttonWidth = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
	const char* createLabel = "Create";
	float createButtonWidth = ImGui::CalcTextSize(createLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
	const char* previewSettingsLabel = "プレビュー設定";
	float previewSettingsButtonWidth =
		ImGui::CalcTextSize(previewSettingsLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;

	float rightX = ImGui::GetWindowContentRegionMax().x - buttonWidth -
		createButtonWidth - previewSettingsButtonWidth - 24.0f;
	const float nextX = (std::max)(ImGui::GetCursorPosX() + 16.0f, rightX);

	ImGui::SameLine(nextX);
	if (ImGui::Button(previewSettingsLabel, ImVec2(previewSettingsButtonWidth, 0.0f))) {

		showModelPreviewSettingsWindow_ = true;
		SavePersistentState();
	}

	ImGui::SameLine();
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

void Engine::ProjectPanel::DrawFolderTree(AssetDatabase& database) {

	ImGui::SetWindowFontScale(0.72f);

	// HierarchyPanelと同じく一番上に検索ボックスを置く
	// 検索ボックスはスクロール領域の外に置き、ツリーをスクロールしても常に見えるようにする
	folderSearchFilter_.DrawInput("##ProjectFolderSearch");
	ImGui::Separator();

	// ツリー本体だけを別の子領域でスクロールさせる
	if (ImGui::BeginChild("##ProjectFolderTreeScroll", ImVec2(0.0f, 0.0f), false)) {

		// ルートから再帰的にフォルダ階層を描画する
		DrawFolderTreeNode(database, assetIndex_.GetRoot());
	}
	ImGui::EndChild();

	ImGui::SetWindowFontScale(1.0f);
}

void Engine::ProjectPanel::DrawFolderTreeNode(AssetDatabase& database, const ProjectDirectoryNode& node) {

	// 検索中は自身か子孫が一致するノードだけ表示する
	if (folderSearchFilter_.IsActive() && !FolderTreeMatchesSearch(node)) {
		return;
	}

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (node.virtualPath == selectedDirectory_) {
		flags |= ImGuiTreeNodeFlags_Selected;
	}
	const bool isLeaf = node.children.empty();
	if (isLeaf) {
		flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}
	// 検索中は階層を開いて一致フォルダを見えるようにする
	if (folderSearchFilter_.IsActive() && !isLeaf) {
		ImGui::SetNextItemOpen(true);
	}

	ImGui::PushID(node.virtualPath.c_str());
	const bool opened = ImGui::TreeNodeEx("##FolderNode", flags, "%s", node.name.c_str());
	// 展開矢印以外のラベルクリックで表示ディレクトリを切り替える
	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {

		selectedDirectory_ = node.virtualPath;
		selectedAsset_ = {};
	}
	// 右側のグリッドと同じくフォルダ移動のドロップ先にする
	DrawProjectItemMoveDropTarget(database, node.virtualPath);

	if (opened && !isLeaf) {

		for (const auto& child : node.children) {
			DrawFolderTreeNode(database, *child);
		}
		ImGui::TreePop();
	}
	ImGui::PopID();
}

bool Engine::ProjectPanel::FolderTreeMatchesSearch(const ProjectDirectoryNode& node) const {

	if (folderSearchFilter_.Matches(node.name)) {
		return true;
	}
	for (const auto& child : node.children) {
		if (FolderTreeMatchesSearch(*child)) {
			return true;
		}
	}
	return false;
}

void Engine::ProjectPanel::DrawDirectoryContents(const EditorPanelContext& context,
	AssetDatabase& database, const ProjectDirectoryNode& node) {

	float iconSize = 64.0f;
	PrepareModelPreviewAtlas(context, database, node);

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
			ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), ImVec4(0.06f, 0.06f, 0.06f, 1.0f))) {

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
		ImVec2 uv0(0.0f, 0.0f);
		ImVec2 uv1(1.0f, 1.0f);
		if (asset.type == AssetType::Mesh) {

			ImTextureID previewTextureID = static_cast<ImTextureID>(0);
			ImVec2 previewUV0{};
			ImVec2 previewUV1{};
			if (TryGetModelPreviewImage(asset.assetID, previewTextureID, previewUV0, previewUV1)) {
				textureID = previewTextureID;
				uv0 = previewUV0;
				uv1 = previewUV1;
			}
		}

		if (ImGui::ImageButton("##AssetButton", textureID, ImVec2(iconSize, iconSize),
			uv0, uv1, ImVec4(0.06f, 0.06f, 0.06f, 1.0f))) {

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
	ImGui::Button("ドラッグアンドドロップしてプレファブ化", ImVec2(ImGui::GetContentRegionAvail().x, 24.0f));
	DrawPrefabCreateDropTarget(context, database, node.virtualPath);
	DrawProjectItemMoveDropTarget(database, node.virtualPath);
	DrawDirectoryContextMenu(database, node);

	// Ctrl+C/Ctrl+Vで選択アセットのコピーと現在ディレクトリへの貼り付けを行う
	// パネルにフォーカスがあり、リネーム等のテキスト入力中でないときだけ受け付ける
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive()) {

		const ImGuiIO& io = ImGui::GetIO();
		// 選択中アセットを内部クリップボードへ控える
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && selectedAsset_) {
			for (const ProjectAssetEntry& asset : node.assets) {
				if (asset.assetID == selectedAsset_) {

					copiedAsset_ = asset;
					hasCopiedAsset_ = true;
					break;
				}
			}
		}
		// 控えたアセットを現在ディレクトリへコピーする、ペースト先は元と別フォルダでもよい
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false) && hasCopiedAsset_) {

			ProjectAssetFileResult result = ProjectAssetFileUtility::CopyAsset(copiedAsset_, assetSource_, selectedDirectory_);
			RefreshAfterFileOperation(database, result);
		}
	}
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

	if (ImGui::MenuItem("Rename")) {

		BeginRenameAsset(asset);
	}
	ImGui::Separator();
	if (ImGui::MenuItem("Open")) {

		context.editorState->SelectAsset(asset.assetID);
		HandleAssetDoubleClick(context, asset);
	}
	if (ImGui::MenuItem("Duplicate")) {

		ProjectAssetFileResult result = ProjectAssetFileUtility::DuplicateAsset(asset);
		RefreshAfterFileOperation(database, result);
	}
	if (ImGui::MenuItem("Delete")) {

		// 削除前に参照元を集めて確認ポップアップを開く
		pendingDeleteAsset_ = asset;
		pendingDeleteReferencers_.clear();
		for (const AssetID& referencer : database.FindReferencers(asset.assetID)) {

			const AssetMeta* meta = database.Find(referencer);
			pendingDeleteReferencers_.emplace_back(meta ? meta->assetPath : ToString(referencer));
		}
		requestOpenDeletePopup_ = true;
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
			// C# Script作成時は共通IDE launcherで開く
			// .cs.metaのscriptTypeId発番/ build / reloadはsource watcherが新規.csを検知して
			// 既存のasync metadata syncからbuildパイプラインで行い、inline UUIDはtemplateに出さない
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

void Engine::ProjectPanel::DrawRenameAssetPopup(AssetDatabase& database) {

	if (requestOpenRenamePopup_) {

		ImGui::OpenPopup("Rename Project Asset");
		requestOpenRenamePopup_ = false;
	}

	if (!ImGui::BeginPopupModal("Rename Project Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	ImGui::Text("Rename Asset");
	ImGui::TextDisabled("%s", pendingRenameAsset_.assetPath.c_str());
	if (!renameProtectedSuffix_.empty()) {
		ImGui::TextDisabled("Protected suffix: %s", renameProtectedSuffix_.c_str());
	}
	ImGui::Separator();

	TextInputPopupResult inputResult = MyGUI::InputTextPopupContent(
		"Name",
		renameNameBuffer_,
		renameErrorMessage_.empty() ? nullptr : renameErrorMessage_.c_str());

	if (inputResult.submitted) {

		ProjectAssetFileResult result = ProjectAssetFileUtility::RenameAsset(
			pendingRenameAsset_,
			renameNameBuffer_);

		if (result.success) {

			renameErrorMessage_.clear();
			RefreshAfterFileOperation(database, result);
			ImGui::CloseCurrentPopup();
		} else {

			renameErrorMessage_ = result.message.empty() ? "Failed to rename asset." : result.message;
		}
	}
	if (inputResult.canceled) {

		renameErrorMessage_.clear();
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}

void Engine::ProjectPanel::DrawDeleteAssetPopup(AssetDatabase& database) {

	if (requestOpenDeletePopup_) {

		ImGui::OpenPopup("Delete Project Asset");
		requestOpenDeletePopup_ = false;
	}

	if (!ImGui::BeginPopupModal("Delete Project Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	ImGui::Text("Delete Asset");
	ImGui::TextDisabled("%s", pendingDeleteAsset_.assetPath.c_str());
	ImGui::Separator();

	// 参照元があるなら、消すと参照切れになることを警告して一覧表示する
	if (!pendingDeleteReferencers_.empty()) {

		ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
			"This asset is referenced by %zu asset(s).", pendingDeleteReferencers_.size());
		ImGui::TextDisabled("Deleting it will leave missing references.");

		if (ImGui::BeginChild("##DeleteReferencers", ImVec2(360.0f, 120.0f), true)) {
			for (const std::string& referencer : pendingDeleteReferencers_) {
				ImGui::BulletText("%s", referencer.c_str());
			}
		}
		ImGui::EndChild();
	} else {

		ImGui::TextUnformatted("No other asset references this asset.");
	}
	ImGui::Separator();

	if (ImGui::Button("Delete")) {

		ProjectAssetFileResult result = ProjectAssetFileUtility::DeleteAsset(pendingDeleteAsset_);
		RefreshAfterFileOperation(database, result);
		pendingDeleteReferencers_.clear();
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel")) {

		pendingDeleteReferencers_.clear();
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
	if (asset.type == AssetType::Prefab) {
		BeginPrefabEdit(context, asset.assetID);
		return;
	}
	if (asset.type != AssetType::Script) {
		return;
	}

	OpenScriptAssetInVisualStudio(asset);
}

void Engine::ProjectPanel::BeginPrefabEdit(const EditorPanelContext& context, AssetID prefabAsset) {

	// Play中はシーンを編集できないため、編集モードに入らない
	if (!context.CanEditScene() || !context.GetWorld() || !context.editorContext ||
		!context.editorContext->assetDatabase || !context.editorState || !prefabAsset) {
		return;
	}

	// 既に別のプレファブを編集中なら、先に保存して片付ける
	EndPrefabEdit(context);

	ECSWorld& world = *context.GetWorld();
	AssetDatabase& database = *context.editorContext->assetDatabase;

	// 一時インスタンスを生成する、アクティブsceneへ所属させて描画されるようにする
	HierarchySystem hierarchySystem{};
	PrefabSystem prefabSystem{};
	PrefabInstantiateResult result{};
	PrefabInstantiateDesc desc{};
	desc.ownerSceneInstanceID = context.editorContext->activeSceneInstanceID;
	if (!prefabSystem.InstantiatePrefab(database, hierarchySystem, world, prefabAsset, result, desc) ||
		!world.IsAlive(result.root)) {
		return;
	}

	// 編集対象として記録し、Inspectorへ出すために選択する
	context.editorState->prefabEditAsset = prefabAsset;
	context.editorState->prefabEditInstance = result.root;
	context.editorState->SelectEntity(result.root);
}

void Engine::ProjectPanel::EndPrefabEdit(const EditorPanelContext& context) {

	EditorState* editorState = context.editorState;
	if (!editorState || !editorState->prefabEditInstance.IsValid()) {
		return;
	}

	ECSWorld* world = context.GetWorld();
	AssetDatabase* database = context.editorContext ? context.editorContext->assetDatabase : nullptr;
	const Entity instance = editorState->prefabEditInstance;
	const AssetID prefabAsset = editorState->prefabEditAsset;

	// 先に状態をクリアしておき、破棄に伴う選択変更で再入しても二重処理しないようにする
	editorState->prefabEditAsset = {};
	editorState->prefabEditInstance = Entity::Null();

	if (world && world->IsAlive(instance)) {

		// 編集結果を元の.prefabへ保存する
		if (database && prefabAsset) {
			if (const AssetMeta* meta = database->Find(prefabAsset)) {
				PrefabSystem prefabSystem{};
				prefabSystem.SavePrefab(*database, *world, instance, meta->assetPath);
			}
		}
		// 一時インスタンスをサブツリーごと破棄する
		EditorEntitySnapshotUtility::DestroySubtree(*world, instance);
	}
}

void Engine::ProjectPanel::UpdatePrefabEditLifecycle(const EditorPanelContext& context) {

	EditorState* editorState = context.editorState;
	if (!editorState || !editorState->prefabEditInstance.IsValid()) {
		return;
	}

	ECSWorld* world = context.GetWorld();
	const Entity instance = editorState->prefabEditInstance;

	// 一時インスタンスが消えていたら編集状態だけ片付ける
	if (!world || !world->IsAlive(instance)) {
		editorState->prefabEditAsset = {};
		editorState->prefabEditInstance = Entity::Null();
		return;
	}

	// 選択がプレファブ編集インスタンス(またはその子孫)から外れたら編集を終了する
	const Entity selected = editorState->selectedEntity;
	bool stillEditing = false;
	for (Entity current = selected; world->IsAlive(current); ) {

		if (current == instance) {
			stillEditing = true;
			break;
		}
		if (!world->HasComponent<HierarchyComponent>(current)) {
			break;
		}
		current = world->GetComponent<HierarchyComponent>(current).parent;
	}
	if (!stillEditing) {
		EndPrefabEdit(context);
	}
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

void Engine::ProjectPanel::BeginRenameAsset(const ProjectAssetEntry& asset) {

	pendingRenameAsset_ = asset;
	renameNameBuffer_ = ProjectAssetFileUtility::GetEditableAssetName(asset);
	renameProtectedSuffix_ = ProjectAssetFileUtility::GetProtectedAssetSuffix(asset);
	renameErrorMessage_.clear();
	requestOpenRenamePopup_ = true;
}

void Engine::ProjectPanel::DrawCreateMenuItems(const std::string& directoryVirtualPath) {

	constexpr std::array<ProjectAssetFileKind, 9> kCreateKinds = {
		ProjectAssetFileKind::Folder,
		ProjectAssetFileKind::Script,
		ProjectAssetFileKind::Scene,
		ProjectAssetFileKind::Prefab,
		ProjectAssetFileKind::Material,
		ProjectAssetFileKind::AnimationClip,
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

void Engine::ProjectPanel::RefreshAfterFileOperation([[maybe_unused]] AssetDatabase& database, const ProjectAssetFileResult& result) {

	if (!result.success) {

		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ProjectPanel: file operation failed. message={}", result.message);
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

	Rebuild(database);

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

void Engine::ProjectPanel::LoadPersistentState() {

	const std::filesystem::path path = GetProjectPanelStatePath();
	if (!JsonAdapter::Check(path.string())) {
		return;
	}

	const nlohmann::json data = JsonAdapter::Load(path.string());
	if (!data.is_object()) {
		return;
	}

	assetSource_ = EnumAdapter<ProjectAssetSource>::FromString(data.value("assetSource", "Engine")).value();

	const std::string defaultDirectory = assetSource_ == ProjectAssetSource::Game ? "GameAssets" : "Engine/Assets";
	selectedDirectory_ = data.value("selectedDirectory", defaultDirectory);
	if (selectedDirectory_.empty()) {

		selectedDirectory_ = defaultDirectory;
	}
	selectedAsset_ = {};

	if (data.contains("modelPreview") && data["modelPreview"].is_object()) {

		const nlohmann::json& modelPreview = data["modelPreview"];
		showModelPreviewSettingsWindow_ = modelPreview.value("showSettingsWindow", showModelPreviewSettingsWindow_);
		if (modelPreview.contains("settings") && modelPreview["settings"].is_object()) {

			const nlohmann::json& settings = modelPreview["settings"];
			modelPreviewSettings_.tileSize = settings.value("tileSize", modelPreviewSettings_.tileSize);
			if (settings.contains("clearColor")) {

				modelPreviewSettings_.clearColor = Color4::FromJson(settings["clearColor"]);
			}
			modelPreviewSettings_.cameraFovY = settings.value("cameraFovY", modelPreviewSettings_.cameraFovY);
			modelPreviewSettings_.cameraDistanceScale =
				settings.value("cameraDistanceScale", modelPreviewSettings_.cameraDistanceScale);
			modelPreviewSettings_.cameraPitchDegrees =
				settings.value("cameraPitchDegrees", modelPreviewSettings_.cameraPitchDegrees);
			modelPreviewSettings_.cameraYawDegrees =
				settings.value("cameraYawDegrees", modelPreviewSettings_.cameraYawDegrees);
			if (settings.contains("lightDirection")) {

				modelPreviewSettings_.lightDirection = Vector3::FromJson(settings["lightDirection"]);
			}
			modelPreviewSettings_.lightIntensity = settings.value("lightIntensity", modelPreviewSettings_.lightIntensity);
		}
	}
	ClampModelPreviewSettings();
}

void Engine::ProjectPanel::SavePersistentState() const {

	nlohmann::json data = nlohmann::json::object();
	data["assetSource"] = EnumAdapter<ProjectAssetSource>::ToString(assetSource_);
	data["selectedDirectory"] = selectedDirectory_;
	data["modelPreview"] = {
		{"showSettingsWindow", showModelPreviewSettingsWindow_},
		{"settings", {
			{"tileSize", modelPreviewSettings_.tileSize},
			{"clearColor", modelPreviewSettings_.clearColor.ToJson()},
			{"cameraFovY", modelPreviewSettings_.cameraFovY},
			{"cameraDistanceScale", modelPreviewSettings_.cameraDistanceScale},
			{"cameraPitchDegrees", modelPreviewSettings_.cameraPitchDegrees},
			{"cameraYawDegrees", modelPreviewSettings_.cameraYawDegrees},
			{"lightDirection", modelPreviewSettings_.lightDirection.ToJson()},
			{"lightIntensity", modelPreviewSettings_.lightIntensity},
		}},
	};

	JsonAdapter::Save(GetProjectPanelStatePath().string(), data);
}
