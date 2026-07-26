#include "ProjectPanel.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>
#include <Engine/Core/Platform/Input/InputSystem.h>
#include <Engine/Editor/Scripting/ManagedIdeLauncher.h>
#include <Engine/Editor/Utility/EditorShell.h>
#include <Engine/Editor/Assets/Importer/Font/MSDFFontGenerator.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabSystem.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Editor/Commands/Entity/EditorEntitySnapshot.h>
#include <Engine/Editor/Core/EditorContext.h>
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
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
#include <Engine/Editor/Utility/EditorTextureHelper.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
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
#include <string>
#include <system_error>
#include <vector>

#include <Engine/Core/Rendering/Meshes/Import/AssimpMaterialTextureExtractor.h>

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
	// 折り返しで3行以上になるときは2行目末尾を...で省略した表示文字列を作る
	std::string BuildTwoLineLabel(const char* text, float width) {

		ImFont* font = ImGui::GetFont();
		const float fontSize = ImGui::GetFontSize();
		const char* textEnd = text + std::strlen(text);

		// 1行目の折り返し位置、収まるならそのまま返す
		const char* line1End = font->CalcWordWrapPosition(fontSize, text, textEnd, width);
		if (line1End >= textEnd) {
			return std::string(text, textEnd);
		}

		// 折り返しでスキップされる空白を飛ばして2行目の先頭を決める
		const char* line2Begin = line1End;
		while (line2Begin < textEnd && *line2Begin == ' ') {
			++line2Begin;
		}
		std::string display(text, line1End);
		display.push_back('\n');

		const char* line2End = font->CalcWordWrapPosition(fontSize, line2Begin, textEnd, width);
		if (line2End >= textEnd) {
			display.append(line2Begin, textEnd);
			return display;
		}

		// 2行に収まらないので...分の幅を空けて詰めて省略する
		const float ellipsisWidth = ImGui::CalcTextSize("...").x;
		const float trimWidth = (std::max)(1.0f, width - ellipsisWidth);
		const char* fit = font->CalcWordWrapPosition(fontSize, line2Begin, textEnd, trimWidth);
		display.append(line2Begin, fit);
		display.append("...");
		return display;
	}
	// アイコン中心に揃うようにラベルを中央寄せで折り返し描画する、widthはアイコンボタンの表示幅
	void DrawCenteredItemLabel(const char* text, float width) {

		// 3行以上にならないよう2行へ省略してから描画する
		const std::string display = BuildTwoLineLabel(text, width);

		const float startX = ImGui::GetCursorPosX();
		// 現在のフォントスケール下での折り返し後サイズを測る
		const ImVec2 textSize = ImGui::CalcTextSize(display.c_str(), nullptr, false, width);
		// ラベルがアイコンより狭いときだけ中央へ寄せる、はみ出すときは左端のまま折り返す
		const float offsetX = (width - textSize.x) * 0.5f;
		if (offsetX > 0.0f) {
			ImGui::SetCursorPosX(startX + offsetX);
		}
		ImGui::PushTextWrapPos(startX + width);
		ImGui::TextWrapped("%s", display.c_str());
		ImGui::PopTextWrapPos();
	}
	// アセットアイコンの標準解決
	ImTextureID ResolveDefaultAssetIcon(Engine::ProjectAssetThumbnailCache& thumbnailCache,
		const Engine::ProjectAssetEntry& asset) {

		return thumbnailCache.GetAssetTextureID(asset.assetPath, asset.type);
	}
	// ドラッグ&ドロップの標準ソースを描画する
	void DrawDefaultAssetDragDropSource(const Engine::ProjectAssetEntry& asset, ImGuiDragDropFlags flags) {

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

	// ProjectPanelの表示状態を保存するパスを返す
	std::filesystem::path GetProjectPanelStatePath() {

		return Engine::RuntimePaths::GetUserSettingsPath(Engine::ConfigPaths::kProjectPanel);
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
		// 既定はVisual StudioでUserSettings/Editor/ManagedIDE.jsonから変更できる
		return Engine::ManagedIdeLauncher::OpenFile(scriptPath, 1, 1);
	}

}

Engine::ProjectPanel::ProjectPanel(TextureUploadService& textureUploadService,
	const std::string& instanceID, bool primaryInstance, const std::string& displayName) :
	displayName_(displayName) {

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
	// ドロップ先のProjectパネルだけがファイルを消費する
	if (!input->TakeDroppedFiles(droppedPaths, dropPoint)) {
		return;
	}

	// カレントフォルダへコピー取り込みする、.metaはRebuildMetaで自動発番される
	// フォルダがドロップされたときは中身ごと再帰コピーする
	bool imported = false;
	for (const std::string& path : droppedPaths) {

		std::error_code ec;
		const std::filesystem::path externalPath(path);
		const ProjectAssetFileResult result = std::filesystem::is_directory(externalPath, ec) ?
			ProjectAssetFileUtility::ImportExternalDirectory(assetSource_, selectedDirectory_, externalPath) :
			ProjectAssetFileUtility::ImportExternalFile(assetSource_, selectedDirectory_, externalPath);
		if (result.success) {
			imported = true;
		}
	}
	if (imported) {
		RefreshDatabaseAndIndex(database);
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
	if (!ImGui::Begin(windowName.c_str(), open)) {
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
	if (dirty_ || database.GetStructureRevision() != lastSeenStructureRevision_) {
		RebuildIndex(database);
	}

	// 外部エクスプローラーからドロップされたファイルをカレントフォルダへ取り込む
	HandleExternalFileDrop(context, database);

	ImGui::SetWindowFontScale(0.8f);
	DrawSourceSelector(context, database);
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
	DrawRenameAssetPopup(database);
	DrawDeleteAssetPopup(database);
	ApplyPendingFileOperationRefresh(database);

	ImGui::End();
}

void Engine::ProjectPanel::DrawEditorTool([[maybe_unused]] const EditorToolContext& context) {}

nlohmann::json Engine::ProjectPanel::SaveLayoutState() const {

	return {
		{ "displayName", displayName_ },
		{ "assetSource", EnumAdapter<ProjectAssetSource>::ToString(assetSource_) },
		{ "selectedDirectory", selectedDirectory_ },
	};
}

void Engine::ProjectPanel::LoadLayoutState(const nlohmann::json& state) {

	if (!state.is_object()) {
		return;
	}

	displayName_ = state.value("displayName", displayName_);
	assetSource_ = EnumAdapter<ProjectAssetSource>::FromString(
		state.value("assetSource", EnumAdapter<ProjectAssetSource>::ToString(assetSource_))).value_or(assetSource_);
	selectedDirectory_ = state.value("selectedDirectory", selectedDirectory_);
	selectedAsset_ = {};
	dirty_ = true;
}

nlohmann::json Engine::ProjectPanel::MakeDuplicateState([[maybe_unused]] const EditorPanelContext& context) const {

	nlohmann::json state = SaveLayoutState();
	state.erase("displayName");
	return state;
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
		ImGui::TextUnformatted(">");
		ImGui::SameLine();

		if (ImGui::Button(trail[i]->name.c_str())) {
			navigate(trail[i]->virtualPath);
		}
		DrawProjectItemMoveDropTarget(database, trail[i]->virtualPath);
	}

	// 現在階層はテキスト
	ImGui::SameLine();
	ImGui::TextUnformatted(">");
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
		DrawFolderGridItem(context, database, *child, iconSize);
	}

	// アセット
	for (const auto& asset : node.assets) {

		ImGui::TableNextColumn();
		DrawAssetGridItem(context, database, asset, iconSize);
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

void Engine::ProjectPanel::DrawFolderGridItem(const EditorPanelContext& context, AssetDatabase& database,
	const ProjectDirectoryNode& node, float iconSize) {

	// フォルダへ移動する、検索中なら検索を解除して通常のフォルダ表示へ戻す
	auto navigate = [&]() {

		selectedDirectory_ = node.virtualPath;
		selectedAsset_ = {};
		fileSearchFilter_.Clear();
		};

	ImGui::PushID(node.virtualPath.c_str());
	ImGui::BeginGroup();

	if (ImGui::ImageButton("##FolderButton", thumbnailCache_.GetFolderIconTextureID(), ImVec2(iconSize, iconSize),
		ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), ImVec4(0.06f, 0.06f, 0.06f, 1.0f))) {

		// Project内のフォルダ移動ではInspectorの選択状態を変更しない
		navigate();
	}
	if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
		// ダブルクリックでもInspectorの選択状態は維持する
		navigate();
	}
	DrawProjectFileMoveSource(node.virtualPath, true, node.name.c_str());

	ImGui::SetWindowFontScale(0.8f);
	DrawCenteredItemLabel(node.name.c_str(), iconSize + ImGui::GetStyle().FramePadding.x * 2.0f);
	ImGui::SetWindowFontScale(1.0f);

	ImGui::EndGroup();
	// アイコンと名前のどちらにカーソルを当ててもツールチップを出すため、グループ全体で判定する
	if (ImGui::BeginItemTooltip()) {
		ImGui::TextUnformatted(node.virtualPath.c_str());
		ImGui::EndTooltip();
	}
	DrawProjectItemMoveDropTarget(database, node.virtualPath);
	DrawPrefabCreateDropTarget(context, database, node.virtualPath);
	DrawFolderContextMenu(database, node);
	ImGui::PopID();
}

ImTextureID Engine::ProjectPanel::ResolveAssetIconTextureID(const ProjectAssetEntry& asset,
	ImVec2& outUV0, ImVec2& outUV1) {

	outUV0 = ImVec2(0.0f, 0.0f);
	outUV1 = ImVec2(1.0f, 1.0f);

	ImTextureID textureID = {};
	if (const AssetActionDescriptor* action = assetActionRegistry_.Find(asset.type)) {
		if (action->iconResolver) {
			textureID = action->iconResolver(thumbnailCache_, asset);
		}
	}
	if (textureID == ImTextureID{}) {
		textureID = ResolveDefaultAssetIcon(thumbnailCache_, asset);
	}
	if (asset.type == AssetType::Mesh) {

		// モデルプレビューAtlasが用意できているときだけ実プレビューへ差し替える
		ImTextureID previewTextureID = static_cast<ImTextureID>(0);
		ImVec2 previewUV0{};
		ImVec2 previewUV1{};
		if (TryGetModelPreviewImage(asset.assetID, previewTextureID, previewUV0, previewUV1)) {
			textureID = previewTextureID;
			outUV0 = previewUV0;
			outUV1 = previewUV1;
		}
	}
	return textureID;
}

void Engine::ProjectPanel::DrawAssetDragDropSource(const ProjectAssetEntry& asset, ImGuiDragDropFlags flags) {

	if (const AssetActionDescriptor* action = assetActionRegistry_.Find(asset.type)) {
		if (action->onDragSource) {

			action->onDragSource(asset, flags);
			return;
		}
	}
	DrawDefaultAssetDragDropSource(asset, flags);
}

void Engine::ProjectPanel::DrawAssetGridItem(const EditorPanelContext& context, AssetDatabase& database,
	const ProjectAssetEntry& asset, float iconSize) {

	ImGui::PushID(asset.assetPath.c_str());
	ImGui::BeginGroup();

	ImVec2 uv0(0.0f, 0.0f);
	ImVec2 uv1(1.0f, 1.0f);
	ImTextureID textureID = ResolveAssetIconTextureID(asset, uv0, uv1);

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

	ImGui::SetWindowFontScale(0.8f);
	DrawCenteredItemLabel(asset.displayName.c_str(), iconSize + ImGui::GetStyle().FramePadding.x * 2.0f);
	ImGui::SetWindowFontScale(1.0f);
	DrawAssetDragDropSource(asset, ImGuiDragDropFlags_SourceAllowNullID);

	ImGui::EndGroup();
	// アイコンと名前のどちらにカーソルを当ててもツールチップを出すため、グループ全体で判定する
	if (ImGui::BeginItemTooltip()) {
		const AssetActionDescriptor* action = assetActionRegistry_.Find(asset.type);
		const char* typeName = action ? action->displayName.c_str() : EnumAdapter<AssetType>::ToString(asset.type);
		ImGui::Text("Path: %s", asset.assetPath.c_str());
		ImGui::Text("Type: %s", typeName);
		ImGui::Text("ID:   %s", Engine::ToString(asset.assetID).c_str());
		ImGui::EndTooltip();
	}
	DrawAssetContextMenu(context, database, asset);
	ImGui::PopID();
}

void Engine::ProjectPanel::CollectSearchMatches(const ProjectDirectoryNode& node,
	std::vector<const ProjectDirectoryNode*>& outFolders,
	std::vector<const ProjectAssetEntry*>& outAssets) const {

	// 子フォルダは名前で、アセットは表示名で一致判定しながらツリー全体を辿る
	for (const auto& child : node.children) {

		if (fileSearchFilter_.Matches(child->name)) {
			outFolders.emplace_back(child.get());
		}
		CollectSearchMatches(*child, outFolders, outAssets);
	}
	for (const ProjectAssetEntry& asset : node.assets) {

		if (fileSearchFilter_.Matches(asset.displayName)) {
			outAssets.emplace_back(&asset);
		}
	}
}

void Engine::ProjectPanel::DrawSearchResults(const EditorPanelContext& context, AssetDatabase& database) {

	// 検索は現在のソース全体(Engine/またはGame/)を対象にツリーのルートから集める
	std::vector<const ProjectDirectoryNode*> folders;
	std::vector<const ProjectAssetEntry*> assets;
	CollectSearchMatches(assetIndex_.GetRoot(), folders, assets);

	if (folders.empty() && assets.empty()) {

		ImGui::TextDisabled("一致するファイルがありません");
		return;
	}

	const float iconSize = 64.0f;
	const int32_t columnCount = CalcGridColumnCount(ImGui::GetContentRegionAvail().x, iconSize + 8.0f);
	if (!ImGui::BeginTable("##ProjectSearchGrid", columnCount, ImGuiTableFlags_SizingFixedFit)) {
		return;
	}

	for (const ProjectDirectoryNode* folder : folders) {

		ImGui::TableNextColumn();
		DrawFolderGridItem(context, database, *folder, iconSize);
	}
	for (const ProjectAssetEntry* asset : assets) {

		ImGui::TableNextColumn();
		DrawAssetGridItem(context, database, *asset, iconSize);
	}

	ImGui::EndTable();
}

void Engine::ProjectPanel::DrawDirectoryContextMenu([[maybe_unused]] AssetDatabase& database, const ProjectDirectoryNode& node) {

	if (!ImGui::BeginPopupContextWindow("ProjectDirectoryContextMenu",
		ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
		return;
	}

	if (ImGui::BeginMenu("作成")) {

		DrawCreateMenuItems(node.virtualPath);
		ImGui::EndMenu();
	}
	ImGui::EndPopup();
}

void Engine::ProjectPanel::DrawFolderContextMenu(AssetDatabase& database, const ProjectDirectoryNode& node) {

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

		ProjectAssetFileResult result = ProjectAssetFileUtility::DuplicateDirectory(assetSource_, node.virtualPath);
		RefreshAfterFileOperation(database, result);
	}
	if (ImGui::MenuItem("削除")) {

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

		ProjectAssetFileResult result = ProjectAssetFileUtility::DuplicateAsset(asset);
		RefreshAfterFileOperation(database, result);
	}
	if (ImGui::MenuItem("削除")) {

		// 削除前に参照元を集めて確認ポップアップを開く
		pendingDeleteAsset_ = asset;
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
	// フォントソースは隣接MSDFの作り直し項目を出す、game_charset編集後の反映にも使う
	if (MSDFFontGenerator::IsFontSourceExtension(asset.assetPath)) {

		ImGui::Separator();
		if (ImGui::MenuItem("フォントデータ再生成")) {

			// 強制再生成、生成物の登録反映は次フレームの構造リビジョン差分で行われる
			const std::filesystem::path sourcePath = RuntimePaths::ResolveAssetPath(asset.assetPath);
			const MSDFFontGenerator::Result result = MSDFFontGenerator::EnsureGenerated(database, sourcePath, true);
			if (!result.success) {

				Logger::Output(LogType::Engine, spdlog::level::warn,
					"ProjectPanel: font regeneration failed. {}", result.message);
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

	if (!ImGui::BeginPopupModal("アセットの作成", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	const char* kindLabel = ProjectAssetFileUtility::GetCreateMenuLabel(pendingCreateKind_);
	ImGui::Text("作成 %s", kindLabel);
	ImGui::TextDisabled("%s", pendingCreateDirectory_.c_str());
	ImGui::Separator();

	TextInputPopupResult inputResult = MyGUI::InputTextPopupContent(
		"名前",
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
			// スクリプトタイプの場合、VisualStudioで開く
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

		ImGui::OpenPopup("アセットの名前変更");
		requestOpenRenamePopup_ = false;
	}

	if (!ImGui::BeginPopupModal("アセットの名前変更", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	ImGui::Text(pendingRenameIsDirectory_ ? "フォルダ名変更" : "アセット名変更");
	ImGui::TextDisabled("%s", pendingRenameIsDirectory_ ? pendingRenameDirectoryPath_.c_str() : pendingRenameAsset_.assetPath.c_str());
	if (!renameProtectedSuffix_.empty()) {
		ImGui::TextDisabled("変更不可拡張子: %s", renameProtectedSuffix_.c_str());
	}
	ImGui::Separator();

	TextInputPopupResult inputResult = MyGUI::InputTextPopupContent("名前", renameNameBuffer_,
		renameErrorMessage_.empty() ? nullptr : renameErrorMessage_.c_str());

	if (inputResult.submitted) {

		ProjectAssetFileResult result = pendingRenameIsDirectory_ ?
			ProjectAssetFileUtility::RenameDirectory(assetSource_, pendingRenameDirectoryPath_, renameNameBuffer_) :
			ProjectAssetFileUtility::RenameAsset(pendingRenameAsset_, renameNameBuffer_);

		if (result.success) {

			renameErrorMessage_.clear();
			RefreshAfterFileOperation(database, result);
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

void Engine::ProjectPanel::DrawDeleteAssetPopup(AssetDatabase& database) {

	if (requestOpenDeletePopup_) {

		ImGui::OpenPopup("アセットの削除");
		requestOpenDeletePopup_ = false;
	}

	if (!ImGui::BeginPopupModal("アセットの削除", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	ImGui::Text("アセット削除");
	ImGui::TextDisabled("%s", pendingDeleteAsset_.assetPath.c_str());
	ImGui::Separator();

	if (ImGui::Button("削除")) {

		ProjectAssetFileResult result = ProjectAssetFileUtility::DeleteAsset(pendingDeleteAsset_);
		RefreshAfterFileOperation(database, result);
		pendingDeleteReferencers_.clear();
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("キャンセル")) {

		pendingDeleteReferencers_.clear();
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}

void Engine::ProjectPanel::RegisterAssetActions() {

	// Sceneはエディターへ開く要求を出す
	AssetActionDescriptor scene{};
	scene.type = AssetType::Scene;
	scene.displayName = "Scene";
	scene.iconResolver = ResolveDefaultAssetIcon;
	scene.onDoubleClick = [](const EditorPanelContext& context, const ProjectAssetEntry& asset) {
		if (context.host) {
			context.host->RequestOpenScene(asset.assetID);
		}
		};
	scene.onDragSource = DrawDefaultAssetDragDropSource;
	assetActionRegistry_.Register(std::move(scene));

	// Prefabは隔離ワールドへ展開して編集モードへ入る、右クリックではインスタンス化項目を出す
	AssetActionDescriptor prefab{};
	prefab.type = AssetType::Prefab;
	prefab.displayName = "Prefab";
	prefab.iconResolver = ResolveDefaultAssetIcon;
	prefab.onDoubleClick = [](const EditorPanelContext& context, const ProjectAssetEntry& asset) {
		if (context.host) {
			context.host->RequestEnterPrefabEdit(asset.assetID);
		}
		};
	prefab.onContextMenu = [](const EditorPanelContext& context, const ProjectAssetEntry& asset) {

		ImGui::Separator();
		if (ImGui::MenuItem("Instantiate Prefab", nullptr, false, context.CanEditScene())) {

			if (context.host) {
				context.host->ExecuteEditorCommand(std::make_unique<InstantiatePrefabCommand>(asset.assetID));
			}
		}
		};
	prefab.onDragSource = DrawDefaultAssetDragDropSource;
	assetActionRegistry_.Register(std::move(prefab));

	// Scriptは共通IDE launcherで開く
	AssetActionDescriptor script{};
	script.type = AssetType::Script;
	script.displayName = "Script";
	script.iconResolver = ResolveDefaultAssetIcon;
	script.onDoubleClick = [](const EditorPanelContext& /*context*/, const ProjectAssetEntry& asset) {
		OpenScriptAssetInVisualStudio(asset);
		};
	script.onDragSource = DrawDefaultAssetDragDropSource;
	assetActionRegistry_.Register(std::move(script));
}

void Engine::ProjectPanel::HandleAssetDoubleClick(const EditorPanelContext& context, const ProjectAssetEntry& asset) {

	// .txtは専用エディタを持たないのでOS既定の関連付けで開く、game_charsetの編集はこの経路
	const std::string extension = Algorithm::ToLower(std::filesystem::path(asset.assetPath).extension().string());
	if (extension == ".txt") {

		EditorShell::OpenWithSystemDefault(RuntimePaths::ResolveAssetPath(asset.assetPath));
		return;
	}

	// アセット種別ごとのダブルクリック処理はRegistryへ委ねる
	if (const AssetActionDescriptor* action = assetActionRegistry_.Find(asset.type)) {
		if (action->onDoubleClick) {
			action->onDoubleClick(context, asset);
		}
	}
}

bool Engine::ProjectPanel::SaveDroppedEntityAsPrefab(const EditorPanelContext& context,
	AssetDatabase& database, const std::string& directoryVirtualPath, const void* payloadData, int32_t payloadSize) {

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
	prefabSystem.SetPrefabLinkToSubtree(world, entity, prefabAsset);

	RefreshAfterFileOperation(database, result);
	return true;
}

void Engine::ProjectPanel::DrawPrefabCreateDropTarget(const EditorPanelContext& context,
	AssetDatabase& database, const std::string& directoryVirtualPath) {

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

		Logger::Output(LogType::Engine, spdlog::level::warn, "ProjectPanel: file operation failed. message={}", result.message);
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
}

void Engine::ProjectPanel::SavePersistentState() const {

	nlohmann::json data = nlohmann::json::object();
	data["assetSource"] = EnumAdapter<ProjectAssetSource>::ToString(assetSource_);
	data["selectedDirectory"] = selectedDirectory_;

	JsonAdapter::Save(GetProjectPanelStatePath().string(), data);
}
