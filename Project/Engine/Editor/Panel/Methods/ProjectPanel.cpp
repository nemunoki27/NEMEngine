#include "ProjectPanel.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Utility/Enum/EnumAdapter.h>

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

			ImGui::SetDragDropPayload(Engine::IEditorPanel::kProjectAssetDragDropPayloadType,
				&payload, sizeof(payload));

			ImGui::TextUnformatted(asset.displayName.c_str());
			ImGui::TextDisabled("%s", asset.assetPath.c_str());

			ImGui::EndDragDropSource();
		}
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
}

Engine::ProjectPanel::ProjectPanel(TextureUploadService& textureUploadService) {

	thumbnailCache_.Init(textureUploadService);
	dirty_ = true;
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

	if (!ImGui::Begin("Project", &context.layoutState->showProject)) {
		ImGui::End();
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
	DrawSourceSelector(database);
	DrawHeader(database);
	ImGui::SetWindowFontScale(1.0f);
	ImGui::Separator();

	ImGui::SameLine();

	// ディレクトリの内容を描画
	if (ImGui::BeginChild("##ProjectContent", ImVec2(0.0f, 0.0f), true)) {
		if (const ProjectDirectoryNode* node = assetIndex_.FindDirectory(selectedDirectory_)) {

			DrawDirectoryContents(*node);
		}
	}
	ImGui::EndChild();

	ImGui::End();
}

void Engine::ProjectPanel::DrawHeader(AssetDatabase& database) {

	// ルートへ戻る
	if (ImGui::Button(GetSourceRootPath())) {
		selectedDirectory_ = assetIndex_.GetRoot().virtualPath;
		selectedAsset_ = {};
	}

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

	float rightX = ImGui::GetWindowContentRegionMax().x - buttonWidth - 4.0f;
	const float nextX = (std::max)(ImGui::GetCursorPosX() + 16.0f, rightX);

	ImGui::SameLine(nextX);
	if (ImGui::Button(label, ImVec2(buttonWidth, 0.0f))) {
		Rebuild(database);
		selectedAsset_ = {};
	}
}

void Engine::ProjectPanel::DrawSourceSelector(AssetDatabase& database) {

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

void Engine::ProjectPanel::DrawDirectoryContents(const ProjectDirectoryNode& node) {

	float iconSize = 64.0f;
	int32_t columnCount = CalcGridColumnCount(ImGui::GetContentRegionAvail().x, iconSize + 8.0f);

	if (!ImGui::BeginTable("##ProjectGrid", columnCount, ImGuiTableFlags_SizingFixedFit)) {
		return;
	}

	// フォルダ
	for (const auto& child : node.children) {

		ImGui::TableNextColumn();
		ImGui::PushID(child->virtualPath.c_str());

		ImGui::BeginGroup();

		if (ImGui::ImageButton("##FolderButton", thumbnailCache_.GetFolderIconTextureID(), ImVec2(iconSize, iconSize),
			ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), ImVec4(0.16f, 0.16f, 0.16f, 1.0f))) {

			selectedDirectory_ = child->virtualPath;
			selectedAsset_ = {};
		}

		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
			selectedDirectory_ = child->virtualPath;
			selectedAsset_ = {};
		}

		ImGui::SetWindowFontScale(0.8f);
		ImGui::TextWrapped("%s", child->name.c_str());
		ImGui::SetWindowFontScale(1.0f);

		if (ImGui::BeginItemTooltip()) {
			ImGui::TextUnformatted(child->virtualPath.c_str());
			ImGui::EndTooltip();
		}

		ImGui::EndGroup();
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

			selectedAsset_ = asset.assetID;
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
		ImGui::PopID();
	}

	ImGui::EndTable();
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
