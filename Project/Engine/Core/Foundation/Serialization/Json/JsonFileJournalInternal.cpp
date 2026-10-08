#include "JsonFileJournalInternal.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <stdexcept>
#include <string_view>

// Windows
#include <Windows.h>

using namespace Engine;
using namespace Engine::StorageFileUtility;
using namespace Engine::JsonFileJournal;

Engine::JsonFileJournal::Internal::ScopeLock::ScopeLock(const Scope& scope) : file_(INVALID_HANDLE_VALUE) {

	if (scope.recoveryRoot.empty() || !scope.isWritable) {
		throw std::invalid_argument("保存範囲が未設定です");
	}
	// 同じ保存範囲の操作を排他取得
	std::filesystem::create_directories(scope.recoveryRoot);
	FileIdentity identity;
	std::error_code error;
	if (!ReadIdentity(scope.recoveryRoot, identity, error)) {
		throw std::system_error(error, "保存範囲の所有を取得できません");
	}
	directory_ = std::make_unique<StorageDirectoryLease>(scope.recoveryRoot, identity);
	file_ = CreateFileW((scope.recoveryRoot / "operation.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file_ == INVALID_HANDLE_VALUE) {
		throw std::runtime_error("同じ保存範囲を使用中か、操作記録を開けません");
	}
}

Engine::JsonFileJournal::Internal::ScopeLock::~ScopeLock() {
	Release();
}

void Engine::JsonFileJournal::Internal::ScopeLock::Release() {

	// 復旧側へ保存範囲を渡す
	if (file_ != INVALID_HANDLE_VALUE) {
		CloseHandle(file_);
		file_ = INVALID_HANDLE_VALUE;
	}
	directory_.reset();
}

namespace Engine::JsonFileJournal::Internal {
	StorageDirectoryLease LeaseDirectory(const Path& directory) {

		FileIdentity identity;
		std::error_code error;
		if (!ReadIdentity(directory, identity, error)) {
			throw std::system_error(error, "操作記録の所有を取得できません");
		}
		return StorageDirectoryLease(directory, identity);
	}

	nlohmann::json ReadJournal(const Path& directory) {

		for (const char* name : {"operation.json", "operation.json.bak", "operation.json.tmp"}) {
			auto journal = JsonAdapter::Load(directory / name, false);
			if (!journal.is_object() || !journal.contains("files") || !journal["files"].is_array() ||
				!journal.contains("state") || !journal["state"].is_string()) {
				continue;
			}
			const auto& state = journal["state"].get_ref<const std::string&>();
			if (state != "pending" && state != "completed" && state != "recovered") {
				continue;
			}
			if (std::string_view(name) != "operation.json") {
				journal["state"] = "pending";
			}
			return journal;
		}
		return {};
	}

	// 復旧先と退避元を適用直前にも照合する
	Path ValidateRecoveryFile(const JsonFileJournal::Scope& scope, const Path& directory, const nlohmann::json& entry,
		bool pending, bool checkCurrent, std::string* revision) {

		const Path target = Algorithm::PathFromUTF8(entry.at("path").get<std::string>());
		if (!scope.isWritable(target) || IsInside(target, scope.recoveryRoot) ||
			PathKey(target) == PathKey(scope.recoveryRoot)) {
			throw std::runtime_error("復旧対象のパスが保存範囲の外側です");
		}
		const std::string before = entry.at("before").get<std::string>();
		if (entry.contains("publications")) {
			if (!entry.at("publications").is_array()) {
				throw std::runtime_error("公開用の所有一覧が不正です");
			}
			// 回収前にも専用フォルダー名と所有記録を検証
			for (const auto& data : entry.at("publications")) {
				StorageFilePublication::Resume(target, DecodePublication(data));
			}
		}
		const std::string current = FileRevision(target);
		// 記録した退避の所有が残る場合だけ中断と判断
		const bool interruptedReplace = current == "missing" && pending && before != "missing" &&
										(HasRetiredRevision(target, entry, before) ||
											HasRetiredRevision(target, entry, entry.at("after").get<std::string>()));
		if (checkCurrent && !interruptedReplace && current != before && current != entry.at("after").get<std::string>()) {
			throw std::runtime_error("操作後の外部変更を検出しました: " + Algorithm::PathToUTF8(target));
		}
		const Path backup = directory / entry.at("backup").get<std::string>();
		if (!IsInside(backup, directory) || (before != "missing" && FileRevision(backup) != before)) {
			throw std::runtime_error("退避データが不正です");
		}
		if (revision) {
			*revision = current;
		}
		return target;
	}

	namespace {

		// 所有識別子の桁落ちを防いで操作記録へ保存する
		nlohmann::json EncodeIdentity(const FileIdentity& identity) {

			return {{"volumeID", identity.volumeID}, {"fileID", identity.fileID}};
		}

		// 符号や配列長の異なる所有記録を拒否する
		FileIdentity DecodeIdentity(const nlohmann::json& data) {

			const auto& volume = data.at("volumeID");
			const auto& bytes = data.at("fileID");
			if (!volume.is_number_unsigned() || !bytes.is_array() || bytes.size() != 16) {
				throw std::runtime_error("公開用の所有記録が不正です");
			}
			FileIdentity identity;
			identity.volumeID = volume.get<uint64_t>();
			for (size_t index = 0; index < bytes.size(); ++index) {
				if (!bytes[index].is_number_unsigned() || bytes[index].get<uint64_t>() > 255) {
					throw std::runtime_error("公開用のファイル識別子が不正です");
				}
				identity.fileID[index] = bytes[index].get<uint8_t>();
			}
			return identity;
		}

	}

	nlohmann::json EncodePublication(const StorageFilePublicationRecord& record) {

		return {{"stageName", record.stageName}, {"stageIdentity", EncodeIdentity(record.stageIdentity)},
			{"before", record.before}, {"beforeIdentity", EncodeIdentity(record.beforeIdentity)}, {"after", record.after},
			{"afterIdentity", EncodeIdentity(record.afterIdentity)}};
	}

	StorageFilePublicationRecord DecodePublication(const nlohmann::json& data) {

		StorageFilePublicationRecord record;
		record.stageName = data.at("stageName").get<std::string>();
		record.stageIdentity = DecodeIdentity(data.at("stageIdentity"));
		record.before = data.at("before").get<std::string>();
		record.beforeIdentity = DecodeIdentity(data.at("beforeIdentity"));
		record.after = data.at("after").get<std::string>();
		record.afterIdentity = DecodeIdentity(data.at("afterIdentity"));
		return record;
	}

	bool HasRetiredRevision(const Path& target, const nlohmann::json& entry, const std::string& revision) {

		if (revision == "missing" || !entry.contains("publications")) {
			return false;
		}
		for (const auto& data : entry.at("publications")) {
			const auto record = DecodePublication(data);
			// 専用フォルダー名を検証してから存在を照合
			auto publication = StorageFilePublication::Resume(target, record);
			if (record.before == revision && std::filesystem::exists(target.parent_path() / record.stageName) &&
				publication.HasRetiredOriginal()) {
				return true;
			}
		}
		return false;
	}

	void CleanupPublications(const nlohmann::json& journal) {

		for (const auto& entry : journal.at("files")) {
			if (!entry.contains("publications")) {
				continue;
			}
			const Path target = Algorithm::PathFromUTF8(entry.at("path").get<std::string>());
			for (const auto& data : entry.at("publications")) {
				try {
					std::error_code error;
					// 回収できない所有は記録を残して保護
					StorageFilePublication::Cleanup(target, DecodePublication(data), error);
				} catch (const std::system_error&) {
					// 回収失敗で確定済みの文書を巻き戻さない
				}
			}
		}
	}

}
