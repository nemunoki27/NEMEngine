#include "ProjectAssetMoveTransaction.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <Engine/Core/Foundation/Utility/ScopedValue.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <exception>
#include <system_error>
#include <utility>

//============================================================================
//	ProjectAssetMoveTransaction classMethods
//============================================================================
Engine::ProjectAssetMoveTransaction::~ProjectAssetMoveTransaction() {

	// 例外で抜けた場合も適用済みの移動を戻す
	if (!committed_ && !rolledBack_) {
		Rollback();
	}
}

bool Engine::ProjectAssetMoveTransaction::Add(const std::filesystem::path& source, const std::filesystem::path& target) {

	if (preparing_ || started_ || committed_ || rolledBack_ || source.empty() || target.empty()) {
		return false;
	}
	const auto sourceKey = StorageFileUtility::PathKey(source);
	const auto targetKey = StorageFileUtility::PathKey(target);
	if (sourceKey == targetKey) {
		return false;
	}
	// 同じパスの二重移動や循環する移動を登録しない
	for (const auto& entry : entries_) {
		const auto entrySource = StorageFileUtility::PathKey(entry.source);
		const auto entryTarget = StorageFileUtility::PathKey(entry.target);
		if (entrySource == sourceKey || entrySource == targetKey || entryTarget == sourceKey || entryTarget == targetKey) {
			return false;
		}
	}
	entries_.push_back({source, target});
	return true;
}

bool Engine::ProjectAssetMoveTransaction::Execute(std::string& diagnostic) {

	if (preparing_ || entries_.empty() || started_ || committed_ || rolledBack_) {
		diagnostic = "Assetの移動計画が無効です";
		return false;
	}
	// 全ての移動先を変更前に確認する
	std::error_code error;
	for (auto& entry : entries_) {
		if (!std::filesystem::exists(entry.source, error) || error || std::filesystem::exists(entry.target, error) || error) {
			diagnostic = "移動元が見つからないか、移動先が使用されています";
			return false;
		}
		StorageFileUtility::FileIdentity identity;
		if (!StorageFileUtility::ReadIdentity(entry.source, identity, error)) {
			diagnostic = "移動元のファイル識別情報を取得できません";
			return false;
		}
		if (entry.prepared && identity != entry.identity) {
			diagnostic = "文書の編集準備中に移動元が差し替わりました";
			return false;
		}
		entry.identity = identity;
	}
	started_ = true;
	for (auto& entry : entries_) {
		const bool moved =
			entry.prepared
				? StorageFileUtility::MoveIfRevision(entry.source, entry.target, entry.revision, entry.identity, error)
				: StorageFileUtility::MoveIfIdentity(entry.source, entry.target, entry.identity, error);
		if (!moved) {
			diagnostic = "Assetまたは付随ファイルを移動できません";
			return false;
		}
		// 移動後に履歴のメモリ確保を行わない
		entry.moved = true;
		++movedCount_;
	}
	// 元のファイルを保持したまま全ての編集内容を適用する
	for (auto& entry : entries_) {
		if (!PublishPrepared(entry, diagnostic)) {
			return false;
		}
	}
	executed_ = true;
	return true;
}

void Engine::ProjectAssetMoveTransaction::Commit() {

	// 全ファイルを移動した操作だけを確定する
	if (!preparing_ && executed_ && !rollbackStarted_ && movedCount_ == entries_.size()) {
		committed_ = true;
		for (auto& entry : entries_) {
			CleanupPrepared(entry);
		}
	}
}

bool Engine::ProjectAssetMoveTransaction::Rollback() noexcept {

	if (preparing_ || committed_ || rolledBack_) {
		return false;
	}
	rollbackStarted_ = true;
	bool restored = true;
	// 復元先に新しいファイルがあれば上書きせず診断する
	for (auto iterator = entries_.rbegin(); iterator != entries_.rend(); ++iterator) {
		auto& entry = *iterator;
		if (!entry.moved) {
			continue;
		}
		std::error_code error;
		bool originalRestored = false;
		try {
			originalRestored = RestorePrepared(entry);
		} catch (...) {
			// 残りの移動を戻し、失敗した所有は記録へ残す
		}
		if (!originalRestored) {
			error = std::make_error_code(std::errc::state_not_recoverable);
		}
		if (!originalRestored || !StorageFileUtility::MoveIfIdentity(entry.target, entry.source, entry.identity, error)) {
			restored = false;
			try {
				Logger::Output(LogType::Engine, spdlog::level::err, "Assetの移動を取り消せません path={} 戻し先={} 詳細={}",
					Algorithm::PathToUTF8(entry.target), Algorithm::PathToUTF8(entry.source), error.message());
			} catch (...) {
				// 診断に失敗しても残りの復元を続ける
			}
		} else {
			// 失敗した移動は再試行用に残す
			entry.moved = false;
			--movedCount_;
		}
	}
	rolledBack_ = movedCount_ == 0;
	return restored;
}

bool Engine::ProjectAssetMoveTransaction::Prepare(
	size_t index, const std::function<bool(std::string&)>& prepare, std::string& diagnostic) {

	if (preparing_ || started_ || committed_ || rolledBack_ || index >= entries_.size() || !prepare ||
		entries_[index].prepared) {
		diagnostic = "移動する文書の編集対象が無効です";
		return false;
	}
	const ScopedValue preparing(preparing_, true);
	try {
		auto& entry = entries_[index];
		std::error_code error;
		StorageFileUtility::FileIdentity identity;
		if (!StorageFileUtility::ReadIdentity(entry.source, identity, error)) {
			diagnostic = "移動する文書の所有を取得できません";
			return false;
		}
		auto revision = StorageFileUtility::FileRevision(entry.source);
		auto bytes = StorageFileUtility::ReadVerifiedBytes(entry.source, revision);
		diagnostic.clear();
		if (!prepare(bytes)) {
			if (diagnostic.empty()) {
				diagnostic = "移動する文書の編集内容を準備できません";
			}
			return false;
		}
		entry.prepared = std::move(bytes);
		entry.revision = std::move(revision);
		entry.identity = identity;
		diagnostic.clear();
		return true;
	} catch (const std::exception& error) {
		diagnostic = error.what();
		return false;
	}
}

bool Engine::ProjectAssetMoveTransaction::PublishPrepared(MoveEntry& entry, std::string& diagnostic) {

	if (!entry.prepared) {
		return true;
	}
	try {
		auto publication = std::make_unique<StorageFilePublication>(entry.target, entry.revision, entry.prepared);
		if (publication->GetRecord().beforeIdentity != entry.identity) {
			diagnostic = "移動した文書の所有が変わりました";
			return false;
		}
		// 退避前に公開記録の所有を操作へ渡す
		entry.publication = std::move(publication);
		entry.publication->Persist();
		entry.publication->Retire();
		entry.publication->Publish();
		return true;
	} catch (const std::exception& error) {
		diagnostic = error.what();
		return false;
	}
}

bool Engine::ProjectAssetMoveTransaction::RestorePrepared(MoveEntry& entry) {

	if (!entry.publication) {
		return true;
	}
	auto& publication = *entry.publication;
	const auto& record = publication.GetRecord();
	std::error_code error;
	if (publication.HasRetiredOriginal()) {
		// 他の処理が差し替えた文書は削除しない
		if (StorageFileUtility::FileRevision(entry.target) != "missing" &&
			!StorageFileUtility::RemoveIfRevision(entry.target, record.after, record.afterIdentity, error)) {
			return false;
		}
		publication.RestoreRetired();
	}
	StorageFileUtility::FileIdentity identity;
	if (!StorageFileUtility::ReadIdentity(entry.target, identity, error) || identity != entry.identity ||
		StorageFileUtility::FileRevision(entry.target) != record.before) {
		return false;
	}
	CleanupPrepared(entry);
	return true;
}

void Engine::ProjectAssetMoveTransaction::CleanupPrepared(MoveEntry& entry) noexcept {

	if (!entry.publication) {
		return;
	}
	try {
		std::error_code error;
		if (StorageFilePublication::Cleanup(entry.target, entry.publication->GetRecord(), error)) {
			entry.publication.reset();
		} else {
			Logger::Output(LogType::Engine, spdlog::level::err,
				"移動した文書の作業先を回収できません path={} 作業先={} 詳細={}", Algorithm::PathToUTF8(entry.target),
				entry.publication->GetRecord().stageName, error.message());
		}
	} catch (...) {
		// 回収できない所有記録は残す
	}
}
