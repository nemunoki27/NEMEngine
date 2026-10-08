#include "ProjectAssetCopyTransaction.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Identity/AssetGUID.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <Engine/Core/Foundation/Utility/ScopedValue.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Serialization/StorageDirectoryLease.h>
#include <Engine/Core/Foundation/Serialization/StorageFilePublication.h>

// c++
#include <algorithm>
#include <exception>
#include <stdexcept>
#include <system_error>
#include <utility>

//============================================================================
//	ProjectAssetCopyTransaction classMethods
//============================================================================
Engine::ProjectAssetCopyTransaction::ProjectAssetCopyTransaction(const std::filesystem::path& targetDirectory)
	: targetDirectory_(targetDirectory.lexically_normal()) {

	// 末尾の区切り文字で同じフォルダーの比較が変わらないようにする
	if (!targetDirectory_.has_filename() && targetDirectory_ != targetDirectory_.root_path()) {
		targetDirectory_ = targetDirectory_.parent_path();
	}
}

Engine::ProjectAssetCopyTransaction::~ProjectAssetCopyTransaction() {

	// 確定していない公開だけを取り消す
	if (!committed_) {
		Rollback();
	}
	RemoveStaging();
}

bool Engine::ProjectAssetCopyTransaction::Add(const std::filesystem::path& source, const std::filesystem::path& target) {

	// コピー開始後は計画を変更しない
	if (!stagingDirectory_.empty() || committed_ || source.empty() || target.filename().empty() || target.filename() == "." ||
		target.filename() == ".." || target.parent_path().lexically_normal() != targetDirectory_) {
		return false;
	}
	const auto duplicate =
		std::find_if(entries_.begin(), entries_.end(), [&](const CopyEntry& entry) { return entry.target == target; });
	if (duplicate != entries_.end()) {
		return false;
	}
	entries_.push_back({source, target});
	return true;
}

bool Engine::ProjectAssetCopyTransaction::Stage(std::string& diagnostic) {

	if (entries_.empty() || targetDirectory_.empty() || !stagingDirectory_.empty() || committed_) {
		diagnostic = "Assetのコピー計画が無効です";
		return false;
	}

	// 公開先の衝突をコピー前に確認する
	std::error_code error;
	for (const auto& entry : entries_) {
		if (std::filesystem::exists(entry.target, error) || error) {
			diagnostic = "コピー先にファイルが存在するか、確認できません";
			return false;
		}
	}

	// 既存の作業先を使用しない
	auto staging = targetDirectory_ / Algorithm::PathFromUTF8(".nem-copy-" + ToString(AssetGUID::New()));
	if (!std::filesystem::create_directory(staging, error) || error) {
		diagnostic = "Assetのコピー作業先を作成できません";
		return false;
	}
	stagingDirectory_ = std::move(staging);
	if (!StorageFileUtility::ReadIdentity(stagingDirectory_, stagingIdentity_, error)) {
		diagnostic = "Assetのコピー作業先の識別情報を取得できません";
		return false;
	}

	// コピー中の作業フォルダーを保持する
	const auto lease = AcquireStaging(diagnostic);
	if (!lease) {
		return false;
	}

	// 失敗したコピーも専用の作業先に閉じ込める
	for (size_t index = 0; index < entries_.size(); ++index) {
		const auto& entry = entries_[index];
		StorageFileUtility::FileIdentity identity;
		std::string revision;
		if (!StorageFileUtility::CopyFileWithoutReplacement(entry.source, GetStagedPath(index), identity, revision)) {
			diagnostic = "Assetまたは付随ファイルをコピーできません";
			return false;
		}
		if (!CaptureEntry(index, identity, std::move(revision), diagnostic)) {
			return false;
		}
	}
	staged_ = true;
	return true;
}

bool Engine::ProjectAssetCopyTransaction::Prepare(size_t index, const AssetCopyPreparation& prepare, std::string& diagnostic) {

	if (preparing_ || !staged_ || publishedCount_ || committed_ || index >= entries_.size() || !prepare) {
		diagnostic = "Assetの編集対象が無効です";
		return false;
	}
	const ScopedValue preparing(preparing_, true);
	try {
		const auto lease = AcquireStaging(diagnostic);
		if (!lease || !MatchesStaged(index, diagnostic)) {
			return false;
		}
		const auto path = GetStagedPath(index);
		auto& entry = entries_[index];
		// ファイルへ書き込まず、照合したbyteだけを編集する
		auto bytes = StorageFileUtility::ReadVerifiedBytes(path, entry.revision);
		diagnostic.clear();
		if (!prepare(path, bytes)) {
			if (diagnostic.empty()) {
				diagnostic = "コピーしたAssetの編集内容を準備できません";
			}
			return false;
		}
		StorageFilePublication publication(path, entry.revision, bytes);
		const auto record = publication.GetRecord();
		auto nextRevision = record.after;
		// 編集中に差し替わった別ファイルを所有しない
		if (record.beforeIdentity != entry.identity) {
			diagnostic = "コピーしたAssetの所有が変わりました";
			return false;
		}
		publication.Persist();
		try {
			publication.Retire();
			publication.Publish();
		} catch (...) {
			// 公開に失敗した元の作業ファイルを戻す
			if (publication.HasRetiredOriginal() && StorageFileUtility::FileRevision(path) == "missing") {
				publication.RestoreRetired();
			}
			std::error_code error;
			StorageFilePublication::Cleanup(path, record, error);
			throw;
		}
		// 作成時の所有だけを次の公開へ引き継ぐ
		entry.identity = record.afterIdentity;
		entry.revision.swap(nextRevision);
		std::error_code error;
		if (!StorageFilePublication::Cleanup(path, record, error)) {
			Logger::Output(LogType::Engine, spdlog::level::err, "Assetの編集作業先を回収できません 詳細={}", error.message());
		}
		diagnostic.clear();
		return true;
	} catch (const std::exception& exception) {
		diagnostic = "コピーしたAssetの編集に失敗しました: " + std::string(exception.what());
	} catch (...) {
		diagnostic = "コピーしたAssetの編集に失敗しました";
	}
	return false;
}

bool Engine::ProjectAssetCopyTransaction::Publish(std::string& diagnostic) {

	if (preparing_ || !staged_ || publishedCount_ || committed_) {
		diagnostic = "Assetのコピー準備が完了していません";
		return false;
	}

	const auto lease = AcquireStaging(diagnostic);
	if (!lease) {
		return false;
	}
	// 付随ファイルの後にAsset本体を公開する
	for (size_t index = entries_.size(); index > 0; --index) {
		auto& entry = entries_[index - 1];
		if (!MatchesStaged(index - 1, diagnostic)) {
			return false;
		}
		std::error_code error;
		if (!StorageFileUtility::MoveIfRevision(
				GetStagedPath(index - 1), entry.target, entry.revision, entry.identity, error)) {
			diagnostic = "コピーしたファイルを公開できません";
			return false;
		}
		++publishedCount_;
	}
	return true;
}

void Engine::ProjectAssetCopyTransaction::Commit() {

	// 全ファイルを公開した操作だけを確定する
	if (!preparing_ && staged_ && publishedCount_ == entries_.size()) {
		committed_ = true;
		RemoveStaging();
	}
}

std::filesystem::path Engine::ProjectAssetCopyTransaction::GetStagedPath(size_t index) const {

	if (stagingDirectory_.empty() || index >= entries_.size()) {
		return {};
	}
	return stagingDirectory_ / entries_[index].target.filename();
}

Engine::ProjectAssetCopySnapshot Engine::ProjectAssetCopyTransaction::GetStagedSnapshot(size_t index) const {

	if (!staged_ || publishedCount_ || committed_ || index >= entries_.size()) {
		throw std::out_of_range("コピーしたファイルのSnapshotを取得できません");
	}
	const auto& entry = entries_[index];
	return {entry.identity, entry.revision};
}

bool Engine::ProjectAssetCopyTransaction::MatchesStaged(size_t index, std::string& diagnostic) const {

	std::error_code error;
	StorageFileUtility::FileIdentity identity;
	const auto& entry = entries_[index];
	const auto path = GetStagedPath(index);
	if (!StorageFileUtility::ReadIdentity(path, identity, error) || identity != entry.identity ||
		StorageFileUtility::FileRevision(path) != entry.revision) {
		diagnostic = "コピーしたAssetの外部変更を検出しました";
		return false;
	}
	return true;
}

bool Engine::ProjectAssetCopyTransaction::CaptureEntry(
	size_t index, const StorageFileUtility::FileIdentity& expected, std::string revision, std::string& diagnostic) {

	// 照合に失敗しても作成時の所有だけを回収対象にする
	entries_[index].revision = std::move(revision);
	entries_[index].identity = expected;
	return MatchesStaged(index, diagnostic);
}

std::unique_ptr<Engine::StorageDirectoryLease> Engine::ProjectAssetCopyTransaction::AcquireStaging(
	std::string& diagnostic) const {

	try {
		return std::make_unique<StorageDirectoryLease>(stagingDirectory_, stagingIdentity_);
	} catch (const std::exception& exception) {
		diagnostic = "Assetのコピー作業先を保持できません: " + std::string(exception.what());
		return {};
	}
}

void Engine::ProjectAssetCopyTransaction::Rollback() noexcept {

	// 公開途中に作成したファイルだけを逆順に戻す
	std::error_code error;
	while (publishedCount_ > 0) {
		const size_t index = entries_.size() - publishedCount_;
		try {
			if (!StorageFileUtility::RemoveIfRevision(
					entries_[index].target, entries_[index].revision, entries_[index].identity, error)) {
				Logger::Output(LogType::Engine, spdlog::level::err, "Assetのコピーを取り消せません path={} 詳細={}",
					Algorithm::PathToUTF8(entries_[index].target), error.message());
			}
		} catch (...) {
			// 取消処理の失敗で終了処理を中断しない
		}
		--publishedCount_;
	}
}

void Engine::ProjectAssetCopyTransaction::RemoveStaging() noexcept {

	if (stagingDirectory_.empty()) {
		return;
	}
	try {
		std::error_code error;
		StorageFileUtility::FileIdentity identity;
		if (!StorageFileUtility::ReadIdentity(stagingDirectory_, identity, error) || identity != stagingIdentity_) {
			Logger::Output(LogType::Engine, spdlog::level::err, "Assetのコピー作業先の所有を確認できません path={}",
				Algorithm::PathToUTF8(stagingDirectory_));
			return;
		}
		{
			StorageDirectoryLease lease(stagingDirectory_, stagingIdentity_);
			// 登録していないファイルは削除しない
			for (const auto& entry : entries_) {
				if (entry.revision.empty()) {
					continue;
				}
				StorageFileUtility::RemoveIfRevision(
					stagingDirectory_ / entry.target.filename(), entry.revision, entry.identity, error);
			}
		}
		if (!error) {
			StorageFileUtility::RemoveIfIdentity(stagingDirectory_, stagingIdentity_, error);
		}
		if (error) {
			Logger::Output(LogType::Engine, spdlog::level::err, "Assetのコピー作業先を削除できません path={}",
				Algorithm::PathToUTF8(stagingDirectory_));
			return;
		}
		stagingDirectory_.clear();
	} catch (...) {
		// 片付けに失敗した作業先を保持する
	}
}
