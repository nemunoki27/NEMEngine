#include "JsonFileJournal.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <chrono>
#include <format>
#include <unordered_set>
#include <stdexcept>

// Windows
#include <Windows.h>

using namespace Engine;
using namespace Engine::StorageFileUtility;

namespace {

	class ScopeLock {
	public:
		explicit ScopeLock(const Engine::JsonFileJournal::Scope& scope) {

			if (scope.recoveryRoot.empty() || !scope.isWritable) throw std::invalid_argument("保存範囲が未設定です");
			std::filesystem::create_directories(scope.recoveryRoot);
			file_ = CreateFileW((scope.recoveryRoot / "operation.lock").c_str(), GENERIC_READ | GENERIC_WRITE,
				0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file_ == INVALID_HANDLE_VALUE) throw std::runtime_error("同じ保存範囲を使用中か、操作記録を開けません");
		}
		~ScopeLock() { Release(); }
		ScopeLock(const ScopeLock&) = delete;
		ScopeLock& operator=(const ScopeLock&) = delete;

		void Release() {

			if (file_ != INVALID_HANDLE_VALUE) {
				CloseHandle(file_);
				file_ = INVALID_HANDLE_VALUE;
			}
		}
	private:
		HANDLE file_ = INVALID_HANDLE_VALUE;
	};

	// pending公開前の退避だけを片付ける
	class PreparationDirectory {
	public:
		explicit PreparationDirectory(const Path& path) : path_(path) {}
		~PreparationDirectory() {

			if (!published_) {
				std::error_code error;
				std::filesystem::remove_all(path_, error);
			}
		}
		PreparationDirectory(const PreparationDirectory&) = delete;
		PreparationDirectory& operator=(const PreparationDirectory&) = delete;
		void Publish() { published_ = true; }
	private:
		Path path_;
		bool published_ = false;
	};

	nlohmann::json ReadJournal(const Path& directory) {

		for (const char* name : { "operation.json", "operation.json.bak", "operation.json.tmp" }) {
			auto journal = JsonAdapter::Load(directory / name, false);
			if (!journal.is_object() || !journal.contains("files") || !journal["files"].is_array() ||
				!journal.contains("state") || !journal["state"].is_string()) continue;
			if (std::string_view(name) != "operation.json") journal["state"] = "pending";
			return journal;
		}
		return {};
	}
}

bool Engine::JsonFileJournal::Commit(const Scope& scope, const std::vector<JsonFileChange>& changes, const std::string& label,
	std::string& error, const RecoveryAction& recover) {

	error.clear();
	try {
		ScopeLock lock(scope);
		const auto pending = GetRecoveries(scope, true);
		if (!pending.empty()) {
			error = "未完了の保存操作があります、先に復旧してください: " + Algorithm::PathToUTF8(pending.front());
			return false;
		}
		if (changes.empty()) return true;
		std::unordered_set<std::string> paths;
		std::vector<const JsonFileChange*> effective;
		for (const JsonFileChange& change : changes) {
			if (!scope.isWritable(change.path) || IsInside(change.path, scope.recoveryRoot) ||
				PathKey(change.path) == PathKey(scope.recoveryRoot) || !paths.insert(PathKey(change.path)).second) {
				error = "変更対象のパスが不正または重複しています";
				return false;
			}
			if (change.remove && !std::filesystem::exists(change.path)) continue;
			nlohmann::json existing;
			if (!change.remove && JsonAdapter::TryLoad(change.path, existing) && existing == change.data) continue;
			effective.push_back(&change);
		}
		if (effective.empty()) return true;
		const Path recovery = scope.recoveryRoot / ToString(Engine::UUID::New());
		if (!std::filesystem::create_directory(recovery)) {
			error = "復旧記録先が既に存在します";
			return false;
		}
		PreparationDirectory preparation(recovery);
		nlohmann::json journal = {{ "state", "preparing" }, { "label", label }, { "files", nlohmann::json::array() }};
		journal["createdAtUtc"] = std::format("{:%Y-%m-%d %H:%M:%S}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
		for (const JsonFileChange* entry : effective) {
			const JsonFileChange& change = *entry;
			const std::string before = FileRevision(change.path);
			const size_t index = journal["files"].size();
			const std::string backup = std::to_string(index) + ".before";
			const Path staged = recovery / (std::to_string(index) + ".after");
			if (before != "missing") {
				std::filesystem::copy_file(change.path, recovery / backup);
				if (FileRevision(recovery / backup) != before) throw std::runtime_error("退避中に外部変更を検出しました");
			}
			if (!change.remove && !JsonAdapter::SaveCanonical(staged, change.data)) {
				error = "保存データを準備できません";
				return false;
			}
			journal["files"].push_back({{ "path", Algorithm::PathToUTF8(std::filesystem::absolute(change.path)) },
				{ "backup", backup }, { "before", before }, { "after", change.remove ? "missing" : FileRevision(staged) }});
		}
		journal["state"] = "pending";
		if (!JsonAdapter::SaveCanonical(recovery / "operation.json", journal)) {
			error = "操作記録を保存できません";
			return false;
		}
		preparation.Publish();
		try {
			for (size_t i = 0; i < effective.size(); ++i) {
				const JsonFileChange& change = *effective[i];
				if (FileRevision(change.path) != journal["files"][i]["before"].get<std::string>()) {
					throw std::runtime_error("操作中に外部変更を検出しました");
				}
				if (change.remove) {
					std::filesystem::remove(change.path);
				} else if (!JsonAdapter::SaveCanonical(change.path, change.data)) {
					throw std::runtime_error("ファイルを保存できません");
				}
			}
			journal["state"] = "completed";
			if (!JsonAdapter::SaveCanonical(recovery / "operation.json", journal)) {
				throw std::runtime_error("操作の完了を記録できません");
			}
			return true;
		} catch (const std::exception& exception) {
			error = exception.what();
			// pending記録を残してから復旧側へ排他を渡す
			lock.Release();
			std::string rollbackError;
			const bool recovered = recover(recovery, rollbackError);
			if (!recovered) error += " / 復旧が必要です: " + rollbackError;
			return false;
		}
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}
}

std::vector<std::filesystem::path> Engine::JsonFileJournal::GetRecoveries(const Scope& scope, bool unfinishedOnly) {

	std::vector<Path> result;
	std::error_code ec;
	for (std::filesystem::directory_iterator it(scope.recoveryRoot, ec), end;
		!ec && it != end; it.increment(ec)) {
		if (!it->is_directory()) continue;
		const auto journal = ReadJournal(it->path());
		const bool invalid = !journal.is_object() && (std::filesystem::exists(it->path() / "operation.json") ||
			std::filesystem::exists(it->path() / "operation.json.bak") || std::filesystem::exists(it->path() / "operation.json.tmp"));
		if (invalid || (journal.is_object() && (!unfinishedOnly || journal.value("state", std::string{}) == "pending"))) result.push_back(it->path());
	}
	if (ec && ec != std::errc::no_such_file_or_directory) {
		throw std::filesystem::filesystem_error("復旧記録を列挙できません", scope.recoveryRoot, ec);
	}
	return result;
}

bool Engine::JsonFileJournal::Recover(const Scope& scope, const Path& directory, std::string& error, const RecoveryCheck& check) {

	error.clear();
	try {
		ScopeLock lock(scope);
		if (!IsInside(directory, scope.recoveryRoot)) throw std::runtime_error("退避先が不正です");
		auto journal = ReadJournal(directory);
		if (!journal.is_object()) throw std::runtime_error("操作記録を読み込めません、退避フォルダーを確認してください");
		const auto& files = journal.at("files");
		for (const auto& entry : files) {
			const Path target = Algorithm::PathFromUTF8(entry.at("path").get<std::string>());
			if (!scope.isWritable(target)) throw std::runtime_error("復旧対象がアセットルートの外側です");
			check(target);
			const std::string current = FileRevision(target);
			// ファイル置換の途中で終了した場合は、元ファイルの退避を照合する
			const bool interruptedReplace = current == "missing" && journal.value("state", "") == "pending" &&
				entry.at("before") != "missing" && FileRevision(Path(target.wstring() + L".bak")) == entry.at("before").get<std::string>();
			if (!interruptedReplace && current != entry.at("before").get<std::string>() && current != entry.at("after").get<std::string>()) throw std::runtime_error("操作後の外部変更を検出しました: " + Algorithm::PathToUTF8(target));
			const Path backup = directory / entry.at("backup").get<std::string>();
			if (!IsInside(backup, directory) || (entry.at("before") != "missing" && FileRevision(backup) != entry.at("before").get<std::string>())) throw std::runtime_error("退避データが不正です");
		}
		// 復旧自体が中断しても次回起動で再開を案内する
		journal["state"] = "pending";
		if (!JsonAdapter::SaveCanonical(directory / "operation.json", journal)) throw std::runtime_error("復旧開始を記録できません");
		for (auto it = files.rbegin(); it != files.rend(); ++it) {
			const Path target = Algorithm::PathFromUTF8(it->at("path").get<std::string>());
			if (FileRevision(target) == it->at("before").get<std::string>()) continue;
			if (it->at("before") == "missing") {
				std::filesystem::remove(target);
			} else {
				std::filesystem::create_directories(target.parent_path());
				if (!CopyFileAtomically(directory / it->at("backup").get<std::string>(), target)) {
					throw std::runtime_error("退避ファイルを復旧できません");
				}
			}
		}
		journal["state"] = "recovered";
		if (!JsonAdapter::SaveCanonical(directory / "operation.json", journal)) throw std::runtime_error("復旧完了を記録できません");
		return true;
	} catch (const std::exception& exception) { error = exception.what(); return false; }
}
