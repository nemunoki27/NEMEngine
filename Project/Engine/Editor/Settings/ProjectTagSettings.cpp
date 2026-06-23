#include "ProjectTagSettings.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <fstream>
#include <algorithm>

// json
#include <json.hpp>

//============================================================================
//	ProjectTagSettings anonymous
//============================================================================
namespace {

	// 既定タグ、TagSettings.json未作成時に使う。Untaggedは先頭固定
	const std::vector<std::string> kDefaultTags = {
		"Untagged", "Player", "Enemy", "Respawn", "Finish", "GameController", "MainCamera"
	};

	std::vector<std::string> g_tags;
	bool g_loaded = false;

	std::filesystem::path TagSettingsPath() {
		return Engine::RuntimePaths::GetGameRoot() / "ProjectSettings" / "TagSettings.json";
	}

	void LoadFromDisk() {

		g_tags.clear();
		std::ifstream ifs(TagSettingsPath(), std::ios::binary);
		if (ifs.is_open()) {

			nlohmann::json data = nlohmann::json::parse(ifs, nullptr, false);
			if (data.is_object() && data.contains("tags") && data["tags"].is_array()) {
				for (const auto& tag : data["tags"]) {
					if (tag.is_string()) {
						g_tags.push_back(tag.get<std::string>());
					}
				}
			}
		}

		// 読み込めなかった、もしくはUntaggedが無い場合は既定で補う
		if (g_tags.empty()) {
			g_tags = kDefaultTags;
		} else if (std::find(g_tags.begin(), g_tags.end(), "Untagged") == g_tags.end()) {
			g_tags.insert(g_tags.begin(), "Untagged");
		}
		g_loaded = true;
	}
}

//============================================================================
//	ProjectTagSettings methods
//============================================================================
const std::vector<std::string>& Engine::ProjectTagSettings::GetTags() {

	if (!g_loaded) {
		LoadFromDisk();
	}
	return g_tags;
}

void Engine::ProjectTagSettings::Reload() {

	g_loaded = false;
	LoadFromDisk();
}
