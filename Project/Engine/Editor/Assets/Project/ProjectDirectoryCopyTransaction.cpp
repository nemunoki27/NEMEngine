#include "ProjectDirectoryCopyTransaction.h"

//============================================================================
//	include
//============================================================================
#include "ProjectAssetCopyTransaction.h"
#include "ProjectAssetPath.h"
#include <Engine/Core/Foundation/Identity/AssetGUID.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Serialization/StorageDirectoryLease.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <Engine/Core/Foundation/Utility/ScopedValue.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <algorithm>
#include <system_error>
#include <stdexcept>
#include <unordered_map>
#include <utility>

//============================================================================
//	ProjectDirectoryCopyTransaction classMethods
//============================================================================
Engine::ProjectDirectoryCopyTransaction::ProjectDirectoryCopyTransaction(const std::filesystem::path& target)
	: target_(target.lexically_normal()) {
}

Engine::ProjectDirectoryCopyTransaction::~ProjectDirectoryCopyTransaction() {

	// 未確定のコピーだけを取り消す
	if (!committed_) {
		Rollback();
	}
}

bool Engine::ProjectDirectoryCopyTransaction::Begin(std::string& diagnostic) {

	if (target_.empty() || target_.filename().empty() || target_.filename() == "." || target_.filename() == ".." ||
		!staging_.empty() || committed_) {
		diagnostic = "フォルダーのコピー先が無効です";
		return false;
	}
	std::error_code error;
	if (std::filesystem::exists(target_, error) || error) {
		diagnostic = "フォルダーのコピー先が存在するか確認できません";
		return false;
	}
	// 既存の作業先を所有しない
	auto staging = target_.parent_path() / Algorithm::PathFromUTF8(".nem-copy-" + ToString(AssetGUID::New()));
	if (!std::filesystem::create_directory(staging, error) || error) {
		diagnostic = "フォルダーのコピー作業先を作成できません";
		return false;
	}
	staging_ = std::move(staging);
	if (!StorageFileUtility::ReadIdentity(staging_, rootIdentity_, error)) {
		diagnostic = "コピー作業先の識別情報を取得できません";
		return false;
	}
	return true;
}

bool Engine::ProjectDirectoryCopyTransaction::AddDirectory(const std::filesystem::path& relative, std::string& diagnostic) {

	if (preparing_ || staging_.empty() || published_ || committed_ || !ProjectAssetPath::IsSafeRelativePath(relative)) {
		diagnostic = "コピー先の相対位置が無効です";
		return false;
	}
	try {
		// 作成先の親を保持し、別フォルダーへ接続しない
		std::vector<std::unique_ptr<StorageDirectoryLease>> leases;
		leases.push_back(std::make_unique<StorageDirectoryLease>(staging_, rootIdentity_));
		std::filesystem::path current;
		for (const auto& part : relative) {
			if (part.empty() || part == ".") {
				continue;
			}
			current /= part;
			const auto found = std::ranges::find_if(directories_, [&](const auto& entry) { return entry.relative == current; });
			if (found != directories_.end()) {
				leases.push_back(std::make_unique<StorageDirectoryLease>(staging_ / current, found->identity));
				continue;
			}
			// 作成前に取消履歴を確保する
			directories_.push_back({current});
			std::error_code error;
			if (!std::filesystem::create_directory(staging_ / current, error) || error) {
				directories_.pop_back();
				diagnostic = "コピー先のフォルダーを作成できません";
				return false;
			}
			if (!StorageFileUtility::ReadIdentity(staging_ / current, directories_.back().identity, error)) {
				diagnostic = "コピー先のフォルダーの識別情報を取得できません";
				return false;
			}
			leases.push_back(std::make_unique<StorageDirectoryLease>(staging_ / current, directories_.back().identity));
		}
		return true;
	} catch (const std::exception& exception) {
		diagnostic = "コピー先のフォルダーを保持できません: " + std::string(exception.what());
		return false;
	}
}

bool Engine::ProjectDirectoryCopyTransaction::StageFile(const std::filesystem::path& source,
	const std::filesystem::path& relative, std::string& diagnostic, const AssetCopyPreparation& prepare) {

	if (preparing_ || !ProjectAssetPath::IsSafeRelativePath(relative) || relative.filename().empty() ||
		relative.filename() == "." || !AddDirectory(relative.parent_path(), diagnostic)) {
		diagnostic = "コピーするファイルの相対位置が無効です";
		return false;
	}
	const ScopedValue preparing(preparing_, true);
	try {
		diagnostic.clear();
		const auto normalized = relative.lexically_normal();
		const auto leases = AcquireParents(normalized.parent_path());
		const auto target = staging_ / normalized;
		ProjectAssetCopyTransaction transaction(target.parent_path());
		if (!transaction.Add(source, target) || !transaction.Stage(diagnostic)) {
			return false;
		}
		// 表示名の変更もファイルの公開前に済ませる
		if (prepare && !transaction.Prepare(0, prepare, diagnostic)) {
			return false;
		}
		// パスから所有を取り直さず、作成時の記録を引き継ぐ
		const auto snapshot = transaction.GetStagedSnapshot(0);
		files_.push_back({normalized, snapshot.revision, snapshot.identity});
		if (!transaction.Publish(diagnostic)) {
			files_.pop_back();
			return false;
		}
		transaction.Commit();
		diagnostic.clear();
		return true;
	} catch (const std::exception& exception) {
		diagnostic = "コピーするファイルの親を保持できません: " + std::string(exception.what());
		return false;
	}
}

bool Engine::ProjectDirectoryCopyTransaction::Publish(std::string& diagnostic) {

	if (preparing_ || staging_.empty() || published_ || committed_) {
		diagnostic = "フォルダーのコピー準備が完了していません";
		return false;
	}
	try {
		{
			const auto leases = AcquireDirectories(staging_);
			if (!ValidateFiles(staging_, diagnostic)) {
				return false;
			}
		}
		// 移動時のhandleで所有を再検証する
		std::error_code error;
		if (!StorageFileUtility::MoveIfIdentity(staging_, target_, rootIdentity_, error)) {
			diagnostic = "コピーしたフォルダーを公開できません";
			return false;
		}
		published_ = true;
		const auto leases = AcquireDirectories(target_);
		// 移動の間に追加された未登録の内容も成功扱いにしない
		validated_ = ValidateFiles(target_, diagnostic);
		return validated_;
	} catch (const std::exception& exception) {
		diagnostic = "コピーしたフォルダーを確認できません: " + std::string(exception.what());
		return false;
	}
}

void Engine::ProjectDirectoryCopyTransaction::Commit() {

	// 公開済みの操作だけを確定する
	if (!preparing_ && published_ && validated_) {
		committed_ = true;
	}
}

std::vector<std::unique_ptr<Engine::StorageDirectoryLease>> Engine::ProjectDirectoryCopyTransaction::AcquireParents(
	const std::filesystem::path& relative) const {

	std::vector<std::unique_ptr<StorageDirectoryLease>> leases;
	leases.push_back(std::make_unique<StorageDirectoryLease>(staging_, rootIdentity_));
	std::filesystem::path current;
	for (const auto& part : relative) {
		if (part.empty() || part == ".") {
			continue;
		}
		current /= part;
		const auto found = std::ranges::find_if(directories_, [&](const auto& entry) { return entry.relative == current; });
		if (found == directories_.end()) {
			throw std::runtime_error("コピー先の親フォルダーの所有が未登録です");
		}
		leases.push_back(std::make_unique<StorageDirectoryLease>(staging_ / current, found->identity));
	}
	return leases;
}

std::vector<std::unique_ptr<Engine::StorageDirectoryLease>> Engine::ProjectDirectoryCopyTransaction::AcquireDirectories(
	const std::filesystem::path& root) const {

	std::vector<std::unique_ptr<StorageDirectoryLease>> leases;
	leases.reserve(directories_.size() + 1);
	leases.push_back(std::make_unique<StorageDirectoryLease>(root, rootIdentity_));
	for (const auto& directory : directories_) {
		leases.push_back(std::make_unique<StorageDirectoryLease>(root / directory.relative, directory.identity));
	}
	return leases;
}

bool Engine::ProjectDirectoryCopyTransaction::ValidateFiles(const std::filesystem::path& root, std::string& diagnostic) const {

	// 所有するパスをまとめて照合する
	struct ExpectedEntry {
		std::filesystem::file_type type;
		const StorageFileUtility::FileIdentity* identity;
		const std::string* revision;
	};
	std::unordered_map<std::filesystem::path, ExpectedEntry> expected;
	expected.reserve(files_.size() + directories_.size());
	for (const auto& directory : directories_) {
		expected.emplace(
			directory.relative, ExpectedEntry{std::filesystem::file_type::directory, &directory.identity, nullptr});
	}
	for (const auto& file : files_) {
		expected.emplace(file.relative, ExpectedEntry{std::filesystem::file_type::regular, &file.identity, &file.revision});
	}
	// 未登録の内容を公開先へ取り込まない
	for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
		const auto relative = entry.path().lexically_relative(root);
		const auto found = expected.find(relative);
		if (found == expected.end() || entry.symlink_status().type() != found->second.type) {
			diagnostic = "コピー作業先に未登録のファイルがあります";
			return false;
		}
		// 登録した内容と子フォルダーの差替えを確認
		StorageFileUtility::FileIdentity identity;
		std::error_code error;
		const auto& wanted = found->second;
		if (!StorageFileUtility::ReadIdentity(entry.path(), identity, error) || identity != *wanted.identity ||
			(wanted.revision && StorageFileUtility::FileRevision(entry.path()) != *wanted.revision)) {
			diagnostic = "コピー作業先の外部変更を検出しました";
			return false;
		}
		expected.erase(found);
	}
	if (!expected.empty()) {
		diagnostic = "コピー作業先のファイルが見つかりません";
		return false;
	}
	return true;
}

void Engine::ProjectDirectoryCopyTransaction::Rollback() noexcept {

	if (staging_.empty()) {
		return;
	}
	try {
		const auto& root = published_ ? target_ : staging_;
		std::error_code error;
		auto rootLease = std::make_unique<StorageDirectoryLease>(root, rootIdentity_);
		std::vector<std::unique_ptr<StorageDirectoryLease>> leases(directories_.size());

		// 別の子フォルダーへ差し替わった場合も内容を残す
		for (size_t index = 0; index < directories_.size(); ++index) {
			const auto& directory = directories_[index];
			const auto path = root / directory.relative;
			if (!std::filesystem::exists(path, error) && !error) {
				continue;
			}
			try {
				leases[index] = std::make_unique<StorageDirectoryLease>(path, directory.identity);
			} catch (const std::exception&) {
				Logger::Output(LogType::Engine, spdlog::level::err, "コピーした子フォルダーの所有を確認できません path={}",
					Algorithm::PathToUTF8(path));
				return;
			}
		}
		// 外部で変更されたファイルは削除しない
		for (auto entry = files_.rbegin(); entry != files_.rend(); ++entry) {
			try {
				// 所有と内容の照合から削除まで同じhandleを使う
				if (!StorageFileUtility::RemoveIfRevision(root / entry->relative, entry->revision, entry->identity, error)) {
					Logger::Output(LogType::Engine, spdlog::level::err, "フォルダーのコピーを取り消せません path={}",
						Algorithm::PathToUTF8(root / entry->relative));
				}
			} catch (...) {
				// 残りの取消を続ける
			}
		}
		// 未登録の内容があるフォルダーは残す
		for (size_t index = directories_.size(); index > 0; --index) {
			leases[index - 1].reset();
			const auto& directory = directories_[index - 1];
			StorageFileUtility::RemoveIfIdentity(root / directory.relative, directory.identity, error);
		}
		rootLease.reset();
		StorageFileUtility::RemoveIfIdentity(root, rootIdentity_, error);
		if (error) {
			Logger::Output(LogType::Engine, spdlog::level::err, "コピー途中のフォルダーに未回収の内容があります path={}",
				Algorithm::PathToUTF8(root));
		}
	} catch (...) {
		// 取消の失敗で終了処理を中断しない
	}
}
