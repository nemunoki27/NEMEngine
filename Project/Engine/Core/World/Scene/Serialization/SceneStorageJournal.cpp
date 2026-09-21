#include "SceneStorageJournal.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Serialization/SceneStorageFiles.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <chrono>
#include <format>
#include <unordered_set>

using namespace Engine;
using namespace Engine::SceneStorageFiles;

namespace {

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

bool Engine::SceneStorageJournal::Commit(const std::vector<SceneStorageChange>& changes, const std::string& label,
	std::string& error, const RecoveryAction& recover) {

	if (!GetRecoveries(true).empty()) {
		error = "未完了のシーン操作があります、Projectパネルの検証・修復から先に復旧してください";
		return false;
	}
	if (changes.empty()) return true;
	std::vector<const SceneStorageChange*> effective;
	for (const SceneStorageChange& change : changes) {
		if (change.remove && !std::filesystem::exists(change.path)) continue;
		if (!change.remove && JsonAdapter::Load(change.path, false) == change.data) continue;
		effective.push_back(&change);
	}
	if (effective.empty()) return true;
	const Path recovery = RuntimePaths::GetSavedRoot() / "SceneAssetRecovery" / ToString(Engine::UUID::New());
	std::filesystem::create_directories(recovery);
	nlohmann::json journal = {{ "state", "preparing" }, { "label", label }, { "files", nlohmann::json::array() }};
	journal["createdAtUtc"] = std::format("{:%Y-%m-%d %H:%M:%S}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
	std::unordered_set<std::string> paths;
	for (const SceneStorageChange* entry : effective) {
		const SceneStorageChange& change = *entry;
		if (!IsWritable(change.path) || !paths.insert(PathKey(change.path)).second) {
			error = "変更対象のパスが不正または重複しています";
			return false;
		}
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
	try {
		for (size_t i = 0; i < effective.size(); ++i) {
			const SceneStorageChange& change = *effective[i];
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
		std::string rollbackError;
		const bool recovered = recover(recovery, rollbackError);
		if (!recovered) error += " / 復旧が必要です: " + rollbackError;
		return false;
	}
}

std::vector<std::filesystem::path> Engine::SceneStorageJournal::GetRecoveries(bool unfinishedOnly) {

	std::vector<Path> result;
	std::error_code ec;
	for (std::filesystem::directory_iterator it(RuntimePaths::GetSavedRoot() / "SceneAssetRecovery", ec), end;
		!ec && it != end; it.increment(ec)) {
		const auto journal = ReadJournal(it->path());
		const bool invalid = !journal.is_object() && (std::filesystem::exists(it->path() / "operation.json") ||
			std::filesystem::exists(it->path() / "operation.json.bak") || std::filesystem::exists(it->path() / "operation.json.tmp"));
		if (invalid || (journal.is_object() && (!unfinishedOnly || journal.value("state", std::string{}) == "pending"))) result.push_back(it->path());
	}
	return result;
}

bool Engine::SceneStorageJournal::Recover(const Path& directory, std::string& error, const RecoveryCheck& check) {

	try {
		if (!IsInside(directory, RuntimePaths::GetSavedRoot() / "SceneAssetRecovery")) throw std::runtime_error("退避先が不正です");
		auto journal = ReadJournal(directory);
		if (!journal.is_object()) throw std::runtime_error("操作記録を読み込めません、退避フォルダーを確認してください");
		const auto& files = journal.at("files");
		for (const auto& entry : files) {
			const Path target = Algorithm::PathFromUTF8(entry.at("path").get<std::string>());
			if (!IsWritable(target)) throw std::runtime_error("復旧対象がアセットルートの外側です");
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
				std::filesystem::copy_file(directory / it->at("backup").get<std::string>(), target, std::filesystem::copy_options::overwrite_existing);
			}
		}
		journal["state"] = "recovered";
		if (!JsonAdapter::SaveCanonical(directory / "operation.json", journal)) throw std::runtime_error("復旧完了を記録できません");
		return true;
	} catch (const std::exception& exception) { error = exception.what(); return false; }
}
