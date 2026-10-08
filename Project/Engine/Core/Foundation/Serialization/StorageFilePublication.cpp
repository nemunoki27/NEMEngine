#include "StorageFilePublication.h"

//============================================================================
//	include
//============================================================================
#include "StorageDirectoryLease.h"
#include "StoragePreparationDirectory.h"
#include "ContentHash.h"
#include <Engine/Core/Foundation/Identity/AssetGUID.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>

// c++
#include <span>
#include <string_view>
#include <stdexcept>
#include <utility>

using namespace Engine;
using namespace Engine::StorageFileUtility;

namespace {

	// 同名の別ファイルを公開済みや退避済みと見なさない
	bool MatchesFile(const Path& path, const std::string& revision, const FileIdentity& identity) {

		std::error_code error;
		FileIdentity current;
		return revision != "missing" && ReadIdentity(path, current, error) && current == identity &&
			   FileRevision(path) == revision;
	}

	// 記録に含まれる専用フォルダー名を検証する
	Path StagePath(const Path& target, const StorageFilePublicationRecord& record) {

		constexpr std::string_view prefix = ".nem-copy-";
		if (!record.stageName.starts_with(prefix) || record.stageName.size() != prefix.size() + 32 ||
			!TryParseAssetGUID32Hex(std::string_view(record.stageName).substr(prefix.size()))) {
			throw std::invalid_argument("公開用の作業先名が不正です");
		}
		return target.parent_path() / Algorithm::PathFromUTF8(record.stageName);
	}
}

Engine::StorageFilePublication::StorageFilePublication(
	const Path& target, const std::string& before, const std::optional<std::string>& bytes) {

	if (target.empty() || target.filename().empty() || target.filename() == "." || target.filename() == ".." ||
		before.empty()) {
		throw std::invalid_argument("ファイルの公開先か内容が不正です");
	}
	target_ = Algorithm::ToFileSystemPath(target);
	record_.before = before;
	std::error_code error;
	if (before != "missing" && !ReadIdentity(target_, record_.beforeIdentity, error)) {
		throw std::system_error(error, "公開前の所有を取得できません");
	}
	if (FileRevision(target_) != before) {
		throw std::runtime_error("公開準備中に外部変更を検出しました");
	}

	// 公開先と同じボリュームに準備する
	std::filesystem::create_directories(target_.parent_path());
	record_.stageName = ".nem-copy-" + ToString(AssetGUID::New());
	const Path stage = GetStagePath();
	if (!std::filesystem::create_directory(stage)) {
		throw std::runtime_error("公開用の作業先が既に存在します");
	}
	preparation_ = std::make_unique<StoragePreparationDirectory>(stage);
	record_.stageIdentity = preparation_->GetIdentity();
	StorageDirectoryLease lease(stage, record_.stageIdentity);
	if (bytes) {
		const auto data = std::span(reinterpret_cast<const uint8_t*>(bytes->data()), bytes->size());
		record_.after = ContentHash::SHA256(data);
		const Path prepared = stage / "prepared";
		preparation_->Track(prepared, record_.after);
		if (!WriteBytesWithoutReplacement(prepared, *bytes, record_.afterIdentity)) {
			throw std::runtime_error("公開用のファイルを準備できません");
		}
		preparation_->Capture(prepared, record_.afterIdentity);
		if (!MatchesFile(prepared, record_.after, record_.afterIdentity)) {
			throw std::runtime_error("公開用のファイルの所有と内容を確認できません");
		}
	}
}

Engine::StorageFilePublication::~StorageFilePublication() = default;
Engine::StorageFilePublication::StorageFilePublication(StorageFilePublication&& other) noexcept = default;
Engine::StorageFilePublication& Engine::StorageFilePublication::operator=(StorageFilePublication&& other) noexcept = default;

void Engine::StorageFilePublication::Persist() {

	// 準備ファイルの回収を保存済みの記録へ渡す
	if (preparation_) {
		preparation_->Publish();
		preparation_.reset();
	}
	persisted_ = true;
}

Engine::StorageFilePublication Engine::StorageFilePublication::Resume(
	const Path& target, const StorageFilePublicationRecord& record) {

	StorageFilePublication result;
	result.target_ = Algorithm::ToFileSystemPath(target);
	result.record_ = record;
	result.GetStagePath();
	result.persisted_ = true;
	return result;
}

void Engine::StorageFilePublication::Retire() {

	RequirePersisted();
	const Path stage = GetStagePath();
	StorageDirectoryLease lease(stage, record_.stageIdentity);
	if (record_.before == "missing") {
		if (FileRevision(target_) != "missing" && !IsPublished()) {
			throw std::runtime_error("新規公開先が使用されています");
		}
		return;
	}
	// 中断後の再開では所有した退避データを再利用する
	if (MatchesFile(stage / "retired", record_.before, record_.beforeIdentity)) {
		if (FileRevision(target_) != "missing" && !IsPublished()) {
			throw std::runtime_error("退避後の公開先が使用されています");
		}
		return;
	}
	std::error_code error;
	if (!MoveIfRevision(target_, stage / "retired", record_.before, record_.beforeIdentity, error)) {
		throw std::system_error(error, "公開前のファイルを退避できません");
	}
}

void Engine::StorageFilePublication::Publish() {

	RequirePersisted();
	const Path stage = GetStagePath();
	StorageDirectoryLease lease(stage, record_.stageIdentity);
	// 退避していない既存ファイルを置き換えない
	if (record_.before != "missing" && !MatchesFile(stage / "retired", record_.before, record_.beforeIdentity)) {
		throw std::runtime_error("公開前の退避ファイルを確認できません");
	}
	if (IsPublished()) {
		return;
	}
	if (record_.after == "missing") {
		if (FileRevision(target_) != "missing") {
			throw std::runtime_error("削除後の公開先が使用されています");
		}
		return;
	}
	std::error_code error;
	if (!MoveIfRevision(stage / "prepared", target_, record_.after, record_.afterIdentity, error)) {
		throw std::system_error(error, "準備したファイルを公開できません");
	}
}

void Engine::StorageFilePublication::RestoreRetired() {

	RequirePersisted();
	const Path stage = GetStagePath();
	StorageDirectoryLease lease(stage, record_.stageIdentity);
	std::error_code error;
	// 新しく現れた公開先は復旧でも置き換えない
	if (!MoveIfRevision(stage / "retired", target_, record_.before, record_.beforeIdentity, error)) {
		throw std::system_error(error, "退避した元ファイルを復元できません");
	}
}

bool Engine::StorageFilePublication::HasRetiredOriginal() const {

	const Path stage = GetStagePath();
	StorageDirectoryLease lease(stage, record_.stageIdentity);
	return MatchesFile(stage / "retired", record_.before, record_.beforeIdentity);
}

bool Engine::StorageFilePublication::IsPublished() const {

	// 外部で消えたファイルを削除済みと見なさない
	if (record_.after == "missing") {
		return FileRevision(target_) == "missing" && (record_.before == "missing" || HasRetiredOriginal());
	}
	return MatchesFile(target_, record_.after, record_.afterIdentity);
}

bool Engine::StorageFilePublication::Cleanup(
	const Path& target, const StorageFilePublicationRecord& record, std::error_code& error) {

	const Path stage = StagePath(Algorithm::ToFileSystemPath(target), record);
	if (!std::filesystem::exists(stage, error)) {
		return !error;
	}
	try {
		{
			StorageDirectoryLease lease(stage, record.stageIdentity);
			// 所有した準備と退避だけを削除する
			if (record.after != "missing" && !RemoveIfRevision(stage / "prepared", record.after, record.afterIdentity, error)) {
				return false;
			}
			if (record.before != "missing" &&
				!RemoveIfRevision(stage / "retired", record.before, record.beforeIdentity, error)) {
				return false;
			}
		}
		// 外部のファイルが残るフォルダーは削除しない
		return RemoveIfIdentity(stage, record.stageIdentity, error);
	} catch (const std::system_error& exception) {
		error = exception.code();
		return false;
	}
}

void Engine::StorageFilePublication::RequirePersisted() const {

	if (!persisted_) {
		throw std::logic_error("公開前に所有記録を保存してください");
	}
}

Engine::StorageFileUtility::Path Engine::StorageFilePublication::GetStagePath() const {

	return StagePath(target_, record_);
}
