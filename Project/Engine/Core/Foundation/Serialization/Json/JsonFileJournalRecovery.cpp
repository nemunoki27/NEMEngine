#include "JsonFileJournalInternal.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <unordered_set>
#include <stdexcept>

using namespace Engine;
using namespace Engine::StorageFileUtility;
using namespace Engine::JsonFileJournal::Internal;

std::vector<std::filesystem::path> Engine::JsonFileJournal::GetRecoveries(const Scope& scope, bool unfinishedOnly) {

	std::vector<Path> result;
	std::error_code ec;
	for (std::filesystem::directory_iterator it(scope.recoveryRoot, ec), end; !ec && it != end; it.increment(ec)) {
		if (!it->is_directory()) {
			continue;
		}
		const auto journal = ReadJournal(it->path());
		const bool invalid = !journal.is_object() && (std::filesystem::exists(it->path() / "operation.json") ||
														 std::filesystem::exists(it->path() / "operation.json.bak") ||
														 std::filesystem::exists(it->path() / "operation.json.tmp"));
		if (invalid || (journal.is_object() && (!unfinishedOnly || journal.value("state", std::string{}) == "pending"))) {
			result.push_back(it->path());
		}
	}
	if (ec && ec != std::errc::no_such_file_or_directory) {
		throw std::filesystem::filesystem_error("復旧記録を列挙できません", scope.recoveryRoot, ec);
	}
	return result;
}

bool Engine::JsonFileJournal::RecoverPending(const Scope& scope, std::string& error, const RecoveryCheck& check) {

	error.clear();
	try {
		// 各操作の保存範囲と退避内容を復元前に照合する
		for (const Path& directory : GetRecoveries(scope, true)) {
			if (!Recover(scope, directory, error, check)) {
				return false;
			}
		}
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}
}

bool Engine::JsonFileJournal::Recover(
	const Scope& scope, const Path& directory, std::string& error, const RecoveryCheck& check) {

	error.clear();
	try {
		ScopeLock lock(scope);
		if (!IsInside(directory, scope.recoveryRoot)) {
			throw std::runtime_error("退避先が不正です");
		}
		auto recoveryLease = LeaseDirectory(directory);
		auto journal = ReadJournal(directory);
		if (!journal.is_object()) {
			throw std::runtime_error("操作記録を読み込めません、退避フォルダーを確認してください");
		}
		auto& files = journal.at("files");
		std::unordered_set<std::string> targets;
		for (const auto& entry : files) {
			const Path target = Algorithm::PathFromUTF8(entry.at("path").get<std::string>());
			if (check) {
				check(target);
			}
			ValidateRecoveryFile(scope, directory, entry, journal.value("state", "") == "pending");
			if (!targets.insert(PathKey(target)).second) {
				throw std::runtime_error("復旧対象のパスが重複しています");
			}
		}
		// 復旧自体が中断しても次回起動で再開を案内する
		journal["state"] = "pending";
		if (!JsonAdapter::SaveCanonical(directory / "operation.json", journal)) {
			throw std::runtime_error("復旧開始を記録できません");
		}
		for (auto it = files.rbegin(); it != files.rend(); ++it) {
			if (check) {
				check(Algorithm::PathFromUTF8(it->at("path").get<std::string>()));
			}
			std::string current;
			const Path target = ValidateRecoveryFile(scope, directory, *it, true, true, &current);
			if (current == it->at("before").get<std::string>()) {
				continue;
			}
			// 退避データを読み取って保存前の内容を準備
			std::optional<std::string> original;
			if (it->at("before") != "missing") {
				const Path backup = directory / it->at("backup").get<std::string>();
				original = ReadVerifiedBytes(backup, it->at("before").get<std::string>());
			}
			StorageFilePublication publication(target, current, original);
			if (!it->contains("publications")) {
				(*it)["publications"] = nlohmann::json::array();
			}
			(*it)["publications"].push_back(EncodePublication(publication.GetRecord()));
			// 復旧の所有も保存してから公開先を変更
			if (!JsonAdapter::SaveCanonical(directory / "operation.json", journal)) {
				throw std::runtime_error("復旧用の所有を記録できません");
			}
			publication.Persist();
			publication.Retire();
			publication.Publish();
		}
		journal["state"] = "recovered";
		if (!JsonAdapter::SaveCanonical(directory / "operation.json", journal)) {
			throw std::runtime_error("復旧完了を記録できません");
		}
		CleanupPublications(journal);
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}
}

bool Engine::JsonFileJournal::KeepCurrent(const Scope& scope, const Path& directory, std::string& error) {

	error.clear();
	try {
		ScopeLock lock(scope);
		if (!IsInside(directory, scope.recoveryRoot)) {
			throw std::runtime_error("退避先が不正です");
		}
		auto recoveryLease = LeaseDirectory(directory);
		auto journal = ReadJournal(directory);
		if (!journal.is_object() || journal.value("state", "") != "pending") {
			throw std::runtime_error("未完了の操作記録を読み込めません");
		}
		std::unordered_set<std::string> targets;
		for (auto& entry : journal.at("files")) {
			// 保存範囲と退避元を検証して現在の内容を記録
			const Path target = ValidateRecoveryFile(scope, directory, entry, true, false);
			if (!targets.insert(PathKey(target)).second) {
				throw std::runtime_error("復旧対象のパスが重複しています");
			}
			entry["after"] = FileRevision(target);
		}
		// 文書には触れず退避記録だけを確定
		journal["state"] = "completed";
		journal["keptCurrent"] = true;
		if (!JsonAdapter::SaveCanonical(directory / "operation.json", journal)) {
			throw std::runtime_error("現在の内容を維持する操作を記録できません");
		}
		CleanupPublications(journal);
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}
}
