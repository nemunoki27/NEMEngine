#include "JsonFileJournalInternal.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Serialization/StoragePreparationDirectory.h>
#include <Engine/Core/Foundation/Serialization/ContentHash.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <span>
#include <chrono>
#include <format>
#include <unordered_set>
#include <stdexcept>
#include <utility>

using namespace Engine;
using namespace Engine::StorageFileUtility;

using namespace Engine::JsonFileJournal::Internal;

bool Engine::JsonFileJournal::Commit(const Scope& scope, const std::vector<JsonFileChange>& changes, const std::string& label,
	std::string& error, const RecoveryAction& recover, const CommitCheck& check, const CommitCheck& afterWriteCheck) {

	error.clear();
	try {
		ScopeLock lock(scope);
		const auto pending = GetRecoveries(scope, true);
		if (!pending.empty()) {
			error = "未完了の保存操作があります、先に復旧してください: " + Algorithm::PathToUTF8(pending.front());
			return false;
		}
		// 保存範囲を確保してから読込時の状態を照合する
		if (check) {
			check();
		}
		if (changes.empty()) {
			if (afterWriteCheck) {
				afterWriteCheck();
			}
			return true;
		}
		std::unordered_set<std::string> paths;
		std::vector<const JsonFileChange*> effective;
		for (const JsonFileChange& change : changes) {
			if (!scope.isWritable(change.path) || IsInside(change.path, scope.recoveryRoot) ||
				PathKey(change.path) == PathKey(scope.recoveryRoot) || !paths.insert(PathKey(change.path)).second) {
				error = "変更対象のパスが不正または重複しています";
				return false;
			}
			if (change.createOnly && (change.remove || std::filesystem::exists(change.path))) {
				error = "新規作成先が既に存在するか、削除要求と競合しています";
				return false;
			}
			if (change.remove && !std::filesystem::exists(change.path)) {
				continue;
			}
			nlohmann::json existing;
			if (!change.remove && change.bytes) {
				const auto bytes = std::span(reinterpret_cast<const uint8_t*>(change.bytes->data()), change.bytes->size());
				if (FileRevision(change.path) == ContentHash::SHA256(bytes)) {
					continue;
				}
			} else if (!change.remove && JsonAdapter::TryLoad(change.path, existing) && existing == change.data) {
				if (!change.canonicalize) {
					continue;
				}
				// 正規化では字下げや改行だけの差も保存する
				const std::string serialized = JsonAdapter::SerializeCanonical(change.data);
				const auto bytes = std::span(reinterpret_cast<const uint8_t*>(serialized.data()), serialized.size());
				if (FileRevision(change.path) == ContentHash::SHA256(bytes)) {
					continue;
				}
			}
			effective.push_back(&change);
		}
		if (effective.empty()) {
			if (afterWriteCheck) {
				afterWriteCheck();
			}
			return true;
		}
		const Path recovery = scope.recoveryRoot / ToString(Engine::UUID::New());
		if (!std::filesystem::create_directory(recovery)) {
			error = "復旧記録先が既に存在します";
			return false;
		}
		StoragePreparationDirectory preparation(recovery);
		StorageDirectoryLease recoveryLease(recovery, preparation.GetIdentity());
		std::vector<StorageFilePublication> publications;
		publications.reserve(effective.size());
		nlohmann::json journal = {{"state", "preparing"}, {"label", label}, {"files", nlohmann::json::array()}};
		journal["createdAtUtc"] =
			std::format("{:%Y-%m-%d %H:%M:%S}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
		for (const JsonFileChange* entry : effective) {
			const JsonFileChange& change = *entry;
			const std::string before = FileRevision(change.path);
			if (change.createOnly && before != "missing") {
				throw std::runtime_error("保存準備中に新規作成先が使用されました");
			}
			const size_t index = journal["files"].size();
			const std::string backup = std::to_string(index) + ".before";
			std::optional<std::string> serialized;
			if (before != "missing") {
				preparation.Track(recovery / backup, before);
				// 読み取った元データを非置換で退避
				const std::string original = ReadVerifiedBytes(change.path, before);
				FileIdentity identity;
				if (!WriteBytesWithoutReplacement(recovery / backup, original, identity)) {
					throw std::runtime_error("元データを退避できません");
				}
				preparation.Capture(recovery / backup, identity);
				if (FileRevision(recovery / backup) != before) {
					throw std::runtime_error("退避中に外部変更を検出しました");
				}
			}
			if (!change.remove) {
				serialized = change.bytes ? *change.bytes : JsonAdapter::SerializeCanonical(change.data);
				if (!change.bytes && serialized->empty()) {
					throw std::runtime_error("保存データを準備できません");
				}
			}
			// 保存先のボリュームに公開用の所有を確保
			publications.emplace_back(change.path, before, serialized);
			const auto& publication = publications.back().GetRecord();
			journal["files"].push_back({{"path", Algorithm::PathToUTF8(std::filesystem::absolute(change.path))},
				{"backup", backup}, {"before", before}, {"after", publication.after},
				{"publications", nlohmann::json::array({EncodePublication(publication)})}});
		}
		journal["state"] = "pending";
		if (!JsonAdapter::SaveCanonical(recovery / "operation.json", journal)) {
			error = "操作記録を保存できません";
			return false;
		}
		preparation.Publish();
		// 操作記録へ回収の所有を渡してから適用
		for (auto& publication : publications) {
			publication.Persist();
		}
		try {
			for (auto& publication : publications) {
				// 所有と内容を排他照合して退避と公開
				publication.Retire();
				publication.Publish();
			}
			// 参照を含めた保存結果を確定前に検証する
			if (afterWriteCheck) {
				afterWriteCheck();
			}
			journal["state"] = "completed";
			if (!JsonAdapter::SaveCanonical(recovery / "operation.json", journal)) {
				throw std::runtime_error("操作の完了を記録できません");
			}
			CleanupPublications(journal);
			return true;
		} catch (const std::exception& exception) {
			error = exception.what();
			// pending記録を残してから復旧側へ排他を渡す
			lock.Release();
			std::string rollbackError;
			const bool recovered = recover(recovery, rollbackError);
			if (!recovered) {
				error += " / 復旧が必要です: " + rollbackError;
			}
			return false;
		}
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}
}
