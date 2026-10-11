#include "ProjectPanel.h"

//============================================================================
//	include
//============================================================================
#include "ProjectPanelUI.h"
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Editor/Core/EditorContext.h>
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Editor/Utility/EditorTextureHelper.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

// c++
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

	// 未準備の画像は枠だけ表示する
	bool DrawProjectIconButton(const char* id, ImTextureID texture, float size, const ImVec2& uv0, const ImVec2& uv1) {

		if (texture == ImTextureID{}) {

			const ImVec2 padding = ImGui::GetStyle().FramePadding;
			return ImGui::Button(id, ImVec2(size + padding.x * 2.0f, size + padding.y * 2.0f));
		}
		return ImGui::ImageButton(id, texture, ImVec2(size, size), uv0, uv1, ImVec4(0.06f, 0.06f, 0.06f, 1.0f));
	}

	// 透過テクスチャの背景へチェッカーを描画する
	void DrawTextureCheckerboard(ImDrawList* drawList, const ImVec2& min, const ImVec2& max) {

		constexpr float kCellSize = 8.0f;
		const ImU32 colors[2]{
			IM_COL32(62, 62, 62, 255),
			IM_COL32(94, 94, 94, 255),
		};
		uint32_t row = 0;
		for (float y = min.y; y < max.y; y += kCellSize, ++row) {

			uint32_t column = 0;
			for (float x = min.x; x < max.x; x += kCellSize, ++column) {

				drawList->AddRectFilled(ImVec2(x, y),
					ImVec2((std::min)(x + kCellSize, max.x), (std::min)(y + kCellSize, max.y)), colors[(row + column) & 1]);
			}
		}
	}

	// アイテムの幅に基づいて利用可能な幅に収まる列数を計算する
	int32_t CalcGridColumnCount(float availableWidth, float itemWidth) {
		if (itemWidth <= 0.0f) {
			return 1;
		}
		return (std::max)(1, static_cast<int32_t>(availableWidth / itemWidth));
	}

	// 長い名前を2行へ収める
	std::string BuildTwoLineLabel(const char* text, float width) {

		ImFont* font = ImGui::GetFont();
		const float fontSize = ImGui::GetFontSize();
		const char* textEnd = text + std::strlen(text);

		// 1行に収まる名前はそのまま返す
		const char* line1End = font->CalcWordWrapPosition(fontSize, text, textEnd, width);
		if (line1End >= textEnd) {
			return std::string(text, textEnd);
		}

		// 折り返し位置の空白を飛ばす
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

		// 2行を超える部分を省略する
		const float ellipsisWidth = ImGui::CalcTextSize("...").x;
		const float trimWidth = (std::max)(1.0f, width - ellipsisWidth);
		const char* fit = font->CalcWordWrapPosition(fontSize, line2Begin, textEnd, trimWidth);
		display.append(line2Begin, fit);
		display.append("...");
		return display;
	}

	// 名前をアイコンの中央へ揃える
	void DrawCenteredItemLabel(const char* text, float width) {

		// 3行以上にならないよう2行へ省略してから描画する
		const std::string display = BuildTwoLineLabel(text, width);

		const float startX = ImGui::GetCursorPosX();
		// 現在のフォントスケール下での折り返し後サイズを測る
		const ImVec2 textSize = ImGui::CalcTextSize(display.c_str(), nullptr, false, width);
		// 収まる名前だけ中央へ寄せる
		const float offsetX = (width - textSize.x) * 0.5f;
		if (offsetX > 0.0f) {
			ImGui::SetCursorPosX(startX + offsetX);
		}
		ImGui::PushTextWrapPos(startX + width);
		ImGui::TextWrapped("%s", display.c_str());
		ImGui::PopTextWrapPos();
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
}

using namespace Engine::ProjectPanelUI;

void Engine::ProjectPanel::DrawDirectoryContents(
	const EditorPanelContext& context, AssetDatabase& database, const ProjectDirectoryNode& node) {

	float iconSize = 64.0f;

	int32_t columnCount = CalcGridColumnCount(ImGui::GetContentRegionAvail().x, iconSize + 8.0f);

	if (!ImGui::BeginTable("##ProjectGrid", columnCount, ImGuiTableFlags_SizingFixedFit)) {
		DrawDirectoryContextMenu(database, node);
		return;
	}

	// フォルダ
	for (const auto& child : node.GetChildren()) {

		ImGui::TableNextColumn();
		DrawFolderGridItem(context, database, child, iconSize);
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

			ProjectAssetFileResult result = ProjectAssetFileUtility::CopyAsset(
				copiedAsset_, assetSource_, selectedDirectory_, context.editorContext->sceneStorage);
			RefreshAfterFileOperation(database, result);
		}
	}
}

void Engine::ProjectPanel::DrawFolderGridItem(
	const EditorPanelContext& context, AssetDatabase& database, const ProjectDirectoryNode& node, float iconSize) {

	// フォルダへ移動する、検索中なら検索を解除して通常のフォルダ表示へ戻す
	auto navigate = [&]() {
		selectedDirectory_ = node.virtualPath;
		selectedAsset_ = {};
		fileSearchFilter_.Clear();
	};

	ImGui::PushID(node.virtualPath.c_str());
	ImGui::BeginGroup();

	if (DrawProjectIconButton("##FolderButton", thumbnailCache_.GetFolderIconTextureID(), iconSize,
			ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f))) {

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
	// アイコンと名前をまとめてHover判定する
	if (ImGui::BeginItemTooltip()) {
		ImGui::TextUnformatted(node.virtualPath.c_str());
		ImGui::EndTooltip();
	}
	DrawProjectItemMoveDropTarget(database, node.virtualPath);
	DrawPrefabCreateDropTarget(context, database, node.virtualPath);
	DrawFolderContextMenu(context, database, node);
	ImGui::PopID();
}

ImTextureID Engine::ProjectPanel::ResolveAssetIconTextureID(const ProjectAssetEntry& asset, ImVec2& outUV0, ImVec2& outUV1) {

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
		if (modelPreview_.TryGetModelPreviewImage(asset.assetID, previewTextureID, previewUV0, previewUV1)) {
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

void Engine::ProjectPanel::DrawAssetGridItem(
	const EditorPanelContext& context, AssetDatabase& database, const ProjectAssetEntry& asset, float iconSize) {

	ImGui::PushID(asset.assetPath.c_str());
	ImGui::BeginGroup();

	ImVec2 uv0(0.0f, 0.0f);
	ImVec2 uv1(1.0f, 1.0f);
	// 画面外の画像は読込を要求しない
	const bool visible = ImGui::IsRectVisible(ImVec2(iconSize, iconSize));
	if (visible && asset.type == AssetType::Mesh) {
		modelPreview_.RequestVisibleMesh(asset.assetID);
	}
	ImTextureID textureID = visible ? ResolveAssetIconTextureID(asset, uv0, uv1) : ImTextureID{};

	bool clicked = false;
	Vector2 textureSize{};
	if (textureID != ImTextureID{} && asset.type == AssetType::Texture &&
		thumbnailCache_.TryGetAssetTextureSize(asset.assetPath, textureSize)) {

		const ImVec2 buttonMin = ImGui::GetCursorScreenPos();
		const ImVec2 buttonMax(buttonMin.x + iconSize, buttonMin.y + iconSize);
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(buttonMin, buttonMax, ImGui::GetColorU32(ImGuiCol_FrameBg));

		const float aspect = textureSize.x / textureSize.y;
		ImVec2 imageSize(iconSize, iconSize);
		if (1.0f < aspect) {
			imageSize.y = iconSize / aspect;
		} else {
			imageSize.x = iconSize * aspect;
		}
		const ImVec2 imageMin(buttonMin.x + (iconSize - imageSize.x) * 0.5f, buttonMin.y + (iconSize - imageSize.y) * 0.5f);
		const ImVec2 imageMax(imageMin.x + imageSize.x, imageMin.y + imageSize.y);
		DrawTextureCheckerboard(drawList, imageMin, imageMax);

		const ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
		const bool nearest = thumbnailCache_.UsesNearestSampling(asset.assetPath);
		const ImDrawCallback setSampler =
			nearest ? platformIO.DrawCallback_SetSamplerNearest : platformIO.DrawCallback_SetSamplerLinear;
		if (setSampler) {
			drawList->AddCallback(setSampler);
		}
		drawList->AddImage(textureID, imageMin, imageMax, uv0, uv1);
		if (setSampler && platformIO.DrawCallback_SetSamplerLinear) {
			drawList->AddCallback(platformIO.DrawCallback_SetSamplerLinear);
		}
		drawList->AddRect(buttonMin, buttonMax, ImGui::GetColorU32(ImGuiCol_Border));
		clicked = ImGui::InvisibleButton("##AssetButton", ImVec2(iconSize, iconSize));
	} else {

		clicked = DrawProjectIconButton("##AssetButton", textureID, iconSize, uv0, uv1);
	}
	if (clicked) {

		// 単クリックでProjectの選択を変える
		selectedAsset_ = asset.assetID;
	}
	if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
		// ダブルクリックでInspectorへ表示する
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
	// アイコンと名前をまとめてHover判定する
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
	std::vector<const ProjectDirectoryNode*>& outFolders, std::vector<const ProjectAssetEntry*>& outAssets) const {

	// 子フォルダは名前で、アセットは表示名で一致判定しながらツリー全体を辿る
	for (const auto& child : node.GetChildren()) {

		if (fileSearchFilter_.Matches(child.name)) {
			outFolders.emplace_back(&child);
		}
		CollectSearchMatches(child, outFolders, outAssets);
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
