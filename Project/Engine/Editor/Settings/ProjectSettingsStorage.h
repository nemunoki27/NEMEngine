#pragma once

//============================================================================
//	include
//============================================================================
#include "ProjectRenderingLayerSettings.h"

// c++
#include <filesystem>
#include <vector>

// json
#include <json.hpp>

namespace Engine::ProjectSettingsStorage {

	// 設定文書を読み込む
	nlohmann::json Load(const std::filesystem::path& path);
	// タグ文書を退避と復旧を伴って保存する
	bool SaveTags(const std::filesystem::path& target, const std::vector<std::string>& tags);
	// Layer文書を退避と復旧を伴って保存する
	bool SaveLayers(const std::filesystem::path& target,
		const std::array<std::string, ProjectRenderingLayerSettings::kLayerCount>& names);
}
