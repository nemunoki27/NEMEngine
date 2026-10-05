#include "ProjectAssetCopyTransaction.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Identity/AssetGUID.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>

// c++
#include <algorithm>
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

	// 失敗したコピーも専用の作業先に閉じ込める
	for (const auto& entry : entries_) {
		if (!std::filesystem::copy_file(
				entry.source, stagingDirectory_ / entry.target.filename(), std::filesystem::copy_options::none, error) ||
			error) {
			diagnostic = "Assetまたは付随ファイルをコピーできません";
			return false;
		}
	}
	staged_ = true;
	return true;
}

bool Engine::ProjectAssetCopyTransaction::Publish(std::string& diagnostic) {

	if (!staged_ || publishedCount_ || committed_) {
		diagnostic = "Assetのコピー準備が完了していません";
		return false;
	}

	// 付随ファイルの後にAsset本体を公開する
	for (size_t index = entries_.size(); index > 0; --index) {
		const auto& entry = entries_[index - 1];
		std::error_code error;
		if (!StorageFileUtility::MoveWithoutReplacement(GetStagedPath(index - 1), entry.target, error)) {
			diagnostic = "コピーしたファイルを公開できません";
			return false;
		}
		++publishedCount_;
	}
	return true;
}

void Engine::ProjectAssetCopyTransaction::Commit() {

	// 全ファイルを公開した操作だけを確定する
	if (staged_ && publishedCount_ == entries_.size()) {
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

void Engine::ProjectAssetCopyTransaction::Rollback() {

	// 公開途中に作成したファイルだけを逆順に戻す
	std::error_code error;
	while (publishedCount_ > 0) {
		const size_t index = entries_.size() - publishedCount_;
		std::filesystem::remove(entries_[index].target, error);
		if (error) {
			Logger::Output(LogType::Engine, spdlog::level::err, "Assetのコピーを取り消せません path={}",
				Algorithm::PathToUTF8(entries_[index].target));
		}
		--publishedCount_;
	}
}

void Engine::ProjectAssetCopyTransaction::RemoveStaging() {

	if (stagingDirectory_.empty()) {
		return;
	}
	std::error_code error;
	std::filesystem::remove_all(stagingDirectory_, error);
	if (error) {
		Logger::Output(LogType::Engine, spdlog::level::err, "Assetのコピー作業先を削除できません path={}",
			Algorithm::PathToUTF8(stagingDirectory_));
		return;
	}
	stagingDirectory_.clear();
}
