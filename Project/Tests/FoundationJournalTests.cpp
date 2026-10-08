#include "FoundationJournalTests.h"
#include "StorageDirectoryLeaseTests.h"
#include "StorageFilePublicationTests.h"
#include "ConditionalJournalPublicationTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/Json/JsonFileJournal.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Serialization/StoragePreparationDirectory.h>

// c++
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace NEMTests {

	// 後続ファイルの保存失敗で先行ファイルを復元する
	bool TestJsonJournal(const std::filesystem::path& root) {

		if (!CheckStorageDirectoryLease(root)) {
			std::cerr << "Storage directory lease contract failed\n";
			return false;
		}

		if (!CheckStorageFilePublication(root)) {
			return false;
		}
		if (!CheckConditionalJournalPublication(root)) {
			return false;
		}

		// 未公開の保存準備は所有したファイルだけを片付ける
		const auto preparationRoot = root / "Preparation";
		std::filesystem::create_directory(preparationRoot);
		const auto preparationFile = preparationRoot / "prepared.bin";
		const auto retainedFile = root / "retained-preparation.bin";
		const auto fixtureFile = root / "fixture-preparation.bin";
		if (!Engine::StorageFileUtility::WriteBytes(fixtureFile, "prepared")) {
			return false;
		}
		const auto revision = Engine::StorageFileUtility::FileRevision(fixtureFile);
		{
			Engine::StoragePreparationDirectory preparation(preparationRoot);
			preparation.Track(preparationFile, revision);
			std::filesystem::copy_file(fixtureFile, preparationFile);
			preparation.Capture(preparationFile);
		}
		if (std::filesystem::exists(preparationRoot)) {
			return false;
		}
		// 同じbyteの別ファイルへ差し替わった場合は残す
		std::filesystem::create_directory(preparationRoot);
		{
			Engine::StoragePreparationDirectory preparation(preparationRoot);
			preparation.Track(preparationFile, revision);
			std::filesystem::copy_file(fixtureFile, preparationFile);
			preparation.Capture(preparationFile);
			std::filesystem::rename(preparationFile, retainedFile);
			std::filesystem::copy_file(fixtureFile, preparationFile);
		}
		if (!std::filesystem::is_regular_file(preparationFile) || !std::filesystem::is_regular_file(retainedFile)) {
			return false;
		}
		std::filesystem::remove(preparationFile);
		std::filesystem::remove(preparationRoot);
		std::filesystem::remove(retainedFile);
		// 未取得・所有移譲・フォルダー差替えでは内容を回収しない
		const auto retainedRoot = root / "retained-preparation";
		for (int mode = 0; mode < 3; ++mode) {
			std::filesystem::create_directory(preparationRoot);
			{
				Engine::StoragePreparationDirectory preparation(preparationRoot);
				preparation.Track(preparationFile, revision);
				std::filesystem::copy_file(fixtureFile, preparationFile);
				if (mode != 0) {
					preparation.Capture(preparationFile);
				}
				if (mode == 1) {
					preparation.Publish();
				} else if (mode == 2) {
					std::filesystem::rename(preparationRoot, retainedRoot);
					std::filesystem::create_directory(preparationRoot);
					std::filesystem::copy_file(fixtureFile, preparationFile);
				}
			}
			if (!std::filesystem::is_regular_file(preparationFile) ||
				(mode == 2 && !std::filesystem::is_regular_file(retainedRoot / "prepared.bin"))) {
				return false;
			}
			std::filesystem::remove(preparationFile);
			std::filesystem::remove(preparationRoot);
			if (mode == 2) {
				std::filesystem::remove(retainedRoot / "prepared.bin");
				std::filesystem::remove(retainedRoot);
			}
		}
		std::filesystem::remove(fixtureFile);

		// 新規公開では同じbyteの差替えと内容変更を拒否する
		const auto publishedFile = root / "published-preparation.bin";
		for (int mode = 0; mode < 3; ++mode) {
			std::filesystem::create_directory(preparationRoot);
			{
				Engine::StoragePreparationDirectory preparation(preparationRoot);
				preparation.Track(preparationFile, revision);
				if (!Engine::StorageFileUtility::WriteBytes(preparationFile, "prepared")) {
					return false;
				}
				preparation.Capture(preparationFile);
				if (mode == 1) {
					std::filesystem::rename(preparationFile, retainedFile);
					if (!Engine::StorageFileUtility::WriteBytes(preparationFile, "prepared")) {
						return false;
					}
				} else if (mode == 2) {
					std::ofstream file(preparationFile, std::ios::binary | std::ios::trunc);
					file << "modified";
				}
				std::error_code ec;
				const bool published = preparation.PublishFile(preparationFile, publishedFile, ec);
				if (published != (mode == 0) || static_cast<bool>(ec) == published ||
					std::filesystem::exists(publishedFile) != published) {
					return false;
				}
			}
			if (mode == 0) {
				if (Engine::StorageFileUtility::FileRevision(publishedFile) != revision ||
					std::filesystem::exists(preparationRoot)) {
					return false;
				}
				std::filesystem::remove(publishedFile);
			} else {
				if (!std::filesystem::is_regular_file(preparationFile)) {
					return false;
				}
				std::filesystem::remove(preparationFile);
				std::filesystem::remove(preparationRoot);
				if (mode == 1) {
					std::filesystem::remove(retainedFile);
				}
			}
		}

		const auto first = root / "first.json";
		const auto second = root / "second.json";
		const nlohmann::json original = {{"value", 1}};
		if (!Engine::JsonAdapter::SaveCanonical(first, original) || !Engine::JsonAdapter::SaveCanonical(second, original)) {
			return false;
		}
		const auto before = Engine::StorageFileUtility::FileRevision(first);
		Engine::JsonFileJournal::Scope scope{
			root / "Recovery", [first, second](const auto& path) { return path == first || path == second; }};
		const std::vector<Engine::JsonFileChange> changes{{first, {{"value", 2}}}, {second, {{"value", 3}}}};
		auto recover = [&scope](const auto& directory, std::string& error) {
			return Engine::JsonFileJournal::Recover(scope, directory, error, [](const auto&) {});
		};
		std::string error;
		// 排他を得た後の検証失敗では退避も書込も始めない
		bool checked = false;
		bool nestedAccepted = false;
		if (Engine::JsonFileJournal::Commit(scope, changes, "Rejected", error, recover,
				[&] {
					checked = true;
					std::string nestedError;
					nestedAccepted = Engine::JsonFileJournal::Commit(scope, changes, "Nested", nestedError, recover);
					throw std::runtime_error("保存前の状態が変わりました");
				}) ||
			!checked || nestedAccepted || error.empty() || Engine::StorageFileUtility::FileRevision(first) != before ||
			Engine::JsonAdapter::Load(second) != original || !Engine::JsonFileJournal::GetRecoveries(scope).empty()) {
			return false;
		}
		{
			NEMTests::TestFileReadLock locked(second);
			if (Engine::JsonFileJournal::Commit(scope, changes, "Foundation", error, recover) || error.empty() ||
				Engine::StorageFileUtility::FileRevision(first) != before || Engine::JsonAdapter::Load(second) != original ||
				!Engine::JsonFileJournal::GetRecoveries(scope, true).empty()) {
				return false;
			}
		}
		// 保存後の検証失敗でも全ファイルを元へ戻す
		bool published = false;
		if (Engine::JsonFileJournal::Commit(scope, changes, "PostWriteRejected", error, recover, {},
				[&] {
					published = Engine::JsonAdapter::Load(first) == changes[0].data &&
								Engine::JsonAdapter::Load(second) == changes[1].data;
					throw std::runtime_error("保存後の参照登録に失敗しました");
				}) ||
			!published || error.empty() || Engine::StorageFileUtility::FileRevision(first) != before ||
			Engine::JsonAdapter::Load(second) != original || !Engine::JsonFileJournal::GetRecoveries(scope, true).empty()) {
			return false;
		}
		// 書込不要でも保存後の検証を省かない
		bool unchangedChecked = false;
		if (Engine::JsonFileJournal::Commit(scope, {{first, original}}, "UnchangedRejected", error, recover, {},
				[&] {
					unchangedChecked = true;
					throw std::runtime_error("保存済み参照の登録に失敗しました");
				}) ||
			!unchangedChecked || error.empty() || Engine::JsonAdapter::Load(first) != original) {
			return false;
		}
		if (!Engine::JsonFileJournal::Commit(scope, changes, "Foundation", error, recover) ||
			Engine::JsonAdapter::Load(first) != changes[0].data || Engine::JsonAdapter::Load(second) != changes[1].data) {
			return false;
		}
		// 準備失敗では本体と既存の復旧記録を維持する
		const auto records = Engine::JsonFileJournal::GetRecoveries(scope);
		const nlohmann::json invalid = {{"text", std::string(1, static_cast<char>(0xff))}};
		if (Engine::JsonFileJournal::Commit(scope, {{first, {{"value", 8}}}, {second, invalid}}, "Invalid", error, recover) ||
			Engine::JsonAdapter::Load(first) != changes[0].data || Engine::JsonAdapter::Load(second) != changes[1].data ||
			Engine::JsonFileJournal::GetRecoveries(scope) != records) {
			return false;
		}
		size_t directoryCount = 0;
		for (const auto& entry : std::filesystem::directory_iterator(scope.recoveryRoot)) {
			if (entry.is_directory()) {
				++directoryCount;
			}
		}
		if (directoryCount != records.size()) {
			return false;
		}
		std::filesystem::path completed;
		for (const auto& record : records) {
			if (Engine::JsonAdapter::Load(record / "operation.json").value("state", "") == "completed") {
				completed = record;
			}
		}
		if (completed.empty()) {
			return false;
		}
		// 復旧の後半だけ失敗しても、次回は残りから戻せる
		{
			NEMTests::TestFileReadLock locked(first);
			if (recover(completed, error) || Engine::JsonAdapter::Load(second) != original ||
				Engine::JsonAdapter::Load(first) != changes[0].data ||
				Engine::JsonFileJournal::GetRecoveries(scope, true).empty()) {
				return false;
			}
		}
		if (!recover(completed, error) || Engine::JsonAdapter::Load(first) != original ||
			!Engine::JsonFileJournal::GetRecoveries(scope, true).empty()) {
			return false;
		}

		// 全件確認後の外部変更を復旧前の再確認で保護する
		if (!Engine::JsonFileJournal::Commit(scope, changes, "ExternalChange", error, recover)) {
			return false;
		}
		for (const auto& record : Engine::JsonFileJournal::GetRecoveries(scope)) {
			if (Engine::JsonAdapter::Load(record / "operation.json").value("state", "") == "completed") {
				completed = record;
			}
		}
		const nlohmann::json external = {{"value", 999}};
		const bool recovered = Engine::JsonFileJournal::Recover(scope, completed, error, [&](const auto& target) {
			if (target == second && !Engine::JsonAdapter::SaveCanonical(first, external)) {
				throw std::runtime_error("外部変更の準備に失敗しました");
			}
		});
		if (recovered || Engine::JsonAdapter::Load(first) != external ||
			Engine::JsonFileJournal::GetRecoveries(scope, true).empty()) {
			std::cerr << "Journal external change protection failed: " << error << '\n';
			return false;
		}
		// 外部変更を退避済みの値へ戻して復旧を再開する
		if (!Engine::JsonAdapter::SaveCanonical(first, changes[0].data) || !recover(completed, error) ||
			Engine::JsonAdapter::Load(first) != original || Engine::JsonAdapter::Load(second) != original) {
			return false;
		}
		// 不明な状態の記録を完了済みと判断しない
		// 競合した現在の文書は明示確定でだけ維持する
		if (Engine::JsonFileJournal::Commit(
				scope, changes, "KeepCurrent", error,
				[](const auto&, std::string& diagnostic) {
					diagnostic = "復旧を保留";
					return false;
				},
				{}, [] { throw std::runtime_error("保存後の中断を検証"); })) {
			return false;
		}
		const auto pending = Engine::JsonFileJournal::GetRecoveries(scope, true);
		if (pending.size() != 1 || !Engine::JsonAdapter::SaveCanonical(first, external)) {
			return false;
		}
		completed = pending.front();
		if (recover(completed, error) || !Engine::JsonFileJournal::KeepCurrent(scope, completed, error) ||
			Engine::JsonAdapter::Load(first) != external || Engine::JsonAdapter::Load(second) != changes[1].data ||
			!Engine::JsonFileJournal::GetRecoveries(scope, true).empty() ||
			Engine::JsonFileJournal::KeepCurrent(scope, completed, error)) {
			return false;
		}
		// 明示確定後も元の退避内容を維持する
		if (!recover(completed, error) || Engine::JsonAdapter::Load(first) != original ||
			Engine::JsonAdapter::Load(second) != original) {
			return false;
		}
		// 不明な状態の記録を完了済みと判断しない
		const auto validJournal = Engine::JsonAdapter::Load(completed / "operation.json");
		auto invalidJournal = validJournal;
		invalidJournal["state"] = "invalid";
		if (!Engine::JsonAdapter::SaveCanonical(completed / "operation.json", invalidJournal) ||
			Engine::JsonFileJournal::GetRecoveries(scope, true).empty() ||
			Engine::JsonFileJournal::Commit(scope, changes, "InvalidState", error, recover) ||
			Engine::JsonAdapter::Load(first) != original) {
			return false;
		}
		if (!Engine::JsonAdapter::SaveCanonical(completed / "operation.json", validJournal)) {
			return false;
		}
		// 削除の途中失敗でも先行ファイルを復元
		std::vector<Engine::JsonFileChange> removals{{first, nullptr}, {second, nullptr}};
		for (auto& removal : removals) {
			removal.remove = true;
		}
		{
			TestFileReadLock locked(second);
			if (Engine::JsonFileJournal::Commit(scope, removals, "DeleteLocked", error, recover) || error.empty() ||
				Engine::JsonAdapter::Load(first) != original || Engine::JsonAdapter::Load(second) != original ||
				!Engine::JsonFileJournal::GetRecoveries(scope, true).empty()) {
				return false;
			}
		}
		// 削除後の検証失敗も元の保存byteへ戻す
		bool removed = false;
		if (Engine::JsonFileJournal::Commit(scope, removals, "DeleteRejected", error, recover, {},
				[&] {
					removed = !std::filesystem::exists(first) && !std::filesystem::exists(second);
					throw std::runtime_error("削除後に中断しました");
				}) ||
			!removed || Engine::JsonAdapter::Load(first) != original || Engine::JsonAdapter::Load(second) != original ||
			!Engine::JsonFileJournal::GetRecoveries(scope, true).empty()) {
			return false;
		}
		// 同じパスへの競合要求は何も保存せず拒否する
		return !Engine::JsonFileJournal::Commit(scope, {changes[0], changes[0]}, "Duplicate", error, recover);
	}

	// 新規保存の衝突と復旧時の外部変更を確認する
	bool TestJsonJournalCreation(const std::filesystem::path& root) {

		const auto first = root / "created.json";
		const auto second = root / "created.bin";
		Engine::JsonFileJournal::Scope scope{
			root / "CreationRecovery", [first, second](const auto& path) { return path == first || path == second; }};
		std::vector<Engine::JsonFileChange> changes{{first, {{"value", 1}}}, {second, nullptr}};
		for (auto& change : changes) {
			change.createOnly = true;
		}
		changes[1].bytes = std::string("a\0b", 3);
		auto recover = [&scope](const auto& directory, std::string& error) {
			return Engine::JsonFileJournal::Recover(scope, directory, error, {});
		};
		std::string error;
		bool published = false;
		// 公開後の失敗では新規ファイルをまとめて取り消す
		if (Engine::JsonFileJournal::Commit(scope, changes, "CreateRejected", error, recover, {},
				[&] {
					published = Engine::JsonAdapter::Load(first) == changes[0].data && std::filesystem::file_size(second) == 3;
					throw std::runtime_error("公開後に中断しました");
				}) ||
			!published || std::filesystem::exists(first) || std::filesystem::exists(second) ||
			!Engine::JsonFileJournal::GetRecoveries(scope, true).empty()) {
			return false;
		}
		// 外部で変更された内容は削除せず復旧を保留する
		const nlohmann::json foreign = {{"value", 2}};
		if (Engine::JsonFileJournal::Commit(scope, changes, "CreateConflict", error, recover, {},
				[&] {
					if (!Engine::JsonAdapter::SaveCanonical(first, foreign)) {
						throw std::runtime_error("外部変更に失敗しました");
					}
					throw std::runtime_error("外部変更後に中断しました");
				}) ||
			Engine::JsonAdapter::Load(first) != foreign || Engine::JsonFileJournal::GetRecoveries(scope, true).size() != 1) {
			return false;
		}
		if (!Engine::JsonAdapter::SaveCanonical(first, changes[0].data) ||
			!Engine::JsonFileJournal::RecoverPending(scope, error) || std::filesystem::exists(first) ||
			std::filesystem::exists(second)) {
			return false;
		}
		if (!Engine::JsonFileJournal::Commit(scope, changes, "Create", error, recover)) {
			return false;
		}
		const auto revision = Engine::StorageFileUtility::FileRevision(first);
		const auto savedTime = std::filesystem::last_write_time(first);
		// 同じ内容でも既存ファイルへの新規保存は拒否する
		if (Engine::JsonFileJournal::Commit(scope, changes, "DuplicateCreate", error, recover) ||
			Engine::StorageFileUtility::FileRevision(first) != revision ||
			std::filesystem::last_write_time(first) != savedTime) {
			return false;
		}
		changes[0].remove = true;
		return !Engine::JsonFileJournal::Commit(scope, {changes[0]}, "InvalidCreate", error, recover) &&
			   Engine::StorageFileUtility::FileRevision(first) == revision;
	}

}
