#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Assets/Project/ProjectAssetIndex.h>
#include <Engine/Editor/Assets/Project/ProjectAssetThumbnailCache.h>
#include <imgui.h>

namespace Engine::ProjectPanelUI {

	// Asset種別の標準アイコンを取得する
	ImTextureID ResolveDefaultAssetIcon(ProjectAssetThumbnailCache& thumbnailCache, const ProjectAssetEntry& asset);
	// Assetの参照をドラッグへ渡す
	void DrawDefaultAssetDragDropSource(const ProjectAssetEntry& asset, ImGuiDragDropFlags flags);
} // Engine::ProjectPanelUI
