#include "StoragePreparationDirectory.h"

//============================================================================
//	include
//============================================================================
#include "StorageDirectoryLease.h"

// c++
#include <algorithm>
#include <stdexcept>

//============================================================================
//	StoragePreparationDirectory classMethods
//============================================================================
Engine::StoragePreparationDirectory::StoragePreparationDirectory(const StorageFileUtility::Path& path) : path_(path) {

	std::error_code error;
	if (!StorageFileUtility::ReadIdentity(path_, identity_, error)) {
		throw std::runtime_error("保存準備先の識別情報を取得できません");
	}
}

Engine::StoragePreparationDirectory::~StoragePreparationDirectory() {

	if (published_) {
		return;
	}
	try {
		std::error_code error;
		{
			// 子ファイルの回収中にフォルダーを差し替えさせない
			StorageDirectoryLease lease(path_, identity_);
			for (auto file = files_.rbegin(); file != files_.rend(); ++file) {
				if (!file->captured) {
					continue;
				}
				try {
					StorageFileUtility::RemoveIfRevision(file->path, file->revision, file->identity, error);
				} catch (...) {
					// 残りの準備ファイルの回収を続ける
				}
			}
		}
		// 未登録や外部変更があるフォルダーは残す
		StorageFileUtility::RemoveIfIdentity(path_, identity_, error);
	} catch (...) {
		// 回収失敗で終了処理を中断しない
	}
}

void Engine::StoragePreparationDirectory::Track(const StorageFileUtility::Path& path, const std::string& revision) {

	if (published_ || !StorageFileUtility::IsInside(path, path_) || revision.empty() || revision == "missing" ||
		std::ranges::any_of(files_, [&](const auto& file) { return file.path == path; })) {
		throw std::invalid_argument("保存準備の回収対象が無効です");
	}
	// 作成後に取消履歴のメモリを確保しない
	files_.push_back({path, revision});
}

void Engine::StoragePreparationDirectory::Capture(const StorageFileUtility::Path& path) {

	CaptureImpl(path, nullptr);
}

void Engine::StoragePreparationDirectory::Capture(
	const StorageFileUtility::Path& path, const StorageFileUtility::FileIdentity& identity) {

	CaptureImpl(path, &identity);
}

void Engine::StoragePreparationDirectory::CaptureImpl(
	const StorageFileUtility::Path& path, const StorageFileUtility::FileIdentity* identity) {

	const auto found = std::ranges::find_if(files_, [&](const auto& file) { return file.path == path; });
	if (published_ || found == files_.end() || found->captured) {
		throw std::invalid_argument("保存準備の識別対象が無効です");
	}
	std::error_code error;
	StorageDirectoryLease lease(path_, identity_);
	if (!StorageFileUtility::ReadIdentity(path, found->identity, error) || (identity && found->identity != *identity)) {
		throw std::runtime_error("保存準備の所有を確認できません");
	}
	found->captured = true;
}

void Engine::StoragePreparationDirectory::Publish() {

	published_ = true;
}

bool Engine::StoragePreparationDirectory::PublishFile(
	const StorageFileUtility::Path& path, const StorageFileUtility::Path& target, std::error_code& error) {

	const auto found = std::ranges::find_if(files_, [&](const auto& file) { return file.path == path; });
	if (found == files_.end() || !found->captured) {
		error = std::make_error_code(std::errc::invalid_argument);
		return false;
	}
	try {
		// 公開中は作業フォルダーの所有を固定する
		StorageDirectoryLease lease(path_, identity_);
		return StorageFileUtility::MoveIfRevision(path, target, found->revision, found->identity, error);
	} catch (const std::system_error& exception) {
		error = exception.code();
		return false;
	}
}
