#pragma once

//============================================================================
//	include
//============================================================================

#include <filesystem>
#include <string>
#include <json.hpp>

namespace Engine {

	//============================================================================
	//	JsonFile class
	//	JSONのファイル入出力
	//============================================================================
	class JsonFile {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// JSONを保存する
		static bool Save(const std::string& directoryFilePath, const nlohmann::json& data);
		static bool Save(const std::filesystem::path& directoryFilePath, const nlohmann::json& data);
		// 正規化した内容を置換と復旧を伴って保存する
		static bool SaveCanonical(const std::filesystem::path& directoryFilePath,
			const nlohmann::json& data, int32_t indent);
		// JSONを読み込む
		static nlohmann::json Load(const std::string& directoryFilePath, bool assertion);
		static nlohmann::json Load(const std::filesystem::path& directoryFilePath, bool assertion);
		// JSONのnullと読込失敗を区別し、失敗時は出力を維持する
		static bool TryLoad(const std::filesystem::path& path, nlohmann::json& output, std::string* diagnostic = nullptr);
		// ファイルを開けるか確認する
		static bool Check(const std::string& directoryFilePath, bool assertion);
		static bool Check(const std::filesystem::path& directoryFilePath, bool assertion);
	};
}
