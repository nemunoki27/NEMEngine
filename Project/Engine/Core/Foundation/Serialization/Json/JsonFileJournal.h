#pragma once

//============================================================================
//	include
//============================================================================
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include <json.hpp>

namespace Engine {

	struct JsonFileChange {
		std::filesystem::path path;
		nlohmann::json data;
		bool remove = false;
	};

	namespace JsonFileJournal {

		// 呼出し元が保存範囲と復旧記録の所有を決める
		struct Scope {
			std::filesystem::path recoveryRoot;
			std::function<bool(const std::filesystem::path&)> isWritable;
		};

		using RecoveryCheck = std::function<void(const std::filesystem::path&)>;
		using RecoveryAction = std::function<bool(const std::filesystem::path&, std::string&)>;

		bool Commit(const Scope& scope, const std::vector<JsonFileChange>& changes, const std::string& label,
			std::string& error, const RecoveryAction& recover);
		std::vector<std::filesystem::path> GetRecoveries(const Scope& scope, bool unfinishedOnly = false);
		bool Recover(const Scope& scope, const std::filesystem::path& directory, std::string& error, const RecoveryCheck& check);
	}
}
