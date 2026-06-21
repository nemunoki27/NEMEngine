#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/PostProcess/Stack/PostProcessStackSettings.h>

// json
#include <json.hpp>
// c++
#include <filesystem>

namespace Engine {

	//============================================================================
	//	PostProcessStackSerializer class
	//============================================================================
	class PostProcessStackSerializer {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		// 設定ファイルから読み込む
		static bool Load(const std::filesystem::path& path, PostProcessStackSettings& outSettings);
		// 設定ファイルへ保存する
		static bool Save(const std::filesystem::path& path, const PostProcessStackSettings& settings);

		// JSONから設定を生成する
		static PostProcessStackSettings FromJson(const nlohmann::json& data);
		// 設定からJSONを生成する
		static nlohmann::json ToJson(const PostProcessStackSettings& settings);
	};
} // Engine
