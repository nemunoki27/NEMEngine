#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <filesystem>
#include <string>
#include <vector>

namespace Engine {

	// 保存元と配布情報を同じ識別子で管理する
	struct EditorTemplateEntry {

		std::string id;
		std::string name;
		std::string description;
		std::string kind;
		std::string version = "1.0.0";
		std::string engineVersion;
		AssetID startupScene{};
		std::vector<std::string> additionalPaths;
		std::filesystem::path root;
	};

	struct EditorTemplateResult {

		bool success = false;
		std::string message;
		std::filesystem::path path;
	};

	// 配布元の作成に必要な設定を固定する
	struct EditorTemplateSaveRequest {

		EditorTemplateEntry entry;
		std::filesystem::path repository;
		std::filesystem::path project;
	};
}
