#pragma once

//============================================================================
//	include
//============================================================================
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <json.hpp>

namespace Engine {

	struct JsonFileChange {
		std::filesystem::path path;
		nlohmann::json data;
		bool remove = false;
		bool canonicalize = false;
		// JSON以外の成果物はバイト列のまま保存する
		std::optional<std::string> bytes;
	};

	namespace JsonFileJournal {

		// 呼出し元が保存範囲と復旧記録の所有を決める
		struct Scope {
			std::filesystem::path recoveryRoot;
			std::function<bool(const std::filesystem::path&)> isWritable;
		};

		using RecoveryCheck = std::function<void(const std::filesystem::path&)>;
		using RecoveryAction = std::function<bool(const std::filesystem::path&, std::string&)>;
		using CommitCheck = std::function<void()>;

		// 全ファイルの保存後に検証し、失敗時は保存前へ戻す
		bool Commit(const Scope& scope, const std::vector<JsonFileChange>& changes, const std::string& label,
			std::string& error, const RecoveryAction& recover, const CommitCheck& check = {},
			const CommitCheck& afterWriteCheck = {});
		std::vector<std::filesystem::path> GetRecoveries(const Scope& scope, bool unfinishedOnly = false);
		// 外部変更のない未完了操作だけを保存前へ戻す
		bool RecoverPending(const Scope& scope, std::string& error, const RecoveryCheck& check = {});
		bool Recover(const Scope& scope, const std::filesystem::path& directory, std::string& error, const RecoveryCheck& check);
	}
}
