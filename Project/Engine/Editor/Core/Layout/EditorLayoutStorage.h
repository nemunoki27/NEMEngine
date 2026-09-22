#pragma once

//============================================================================
//	include
//============================================================================
#include "EditorStoredLayout.h"

#include <filesystem>

namespace Engine::EditorLayoutStorage {

	// カタログを読み込む
	void LoadCatalog(const std::filesystem::path& path, bool imported,
		std::vector<EditorStoredLayout>& outLayouts, std::string* outDefaultLayoutID = nullptr);
	// カタログを保存する
	void SaveCatalog(const std::filesystem::path& path, const std::vector<EditorStoredLayout>& layouts,
		const std::string* defaultLayoutID = nullptr);
}
