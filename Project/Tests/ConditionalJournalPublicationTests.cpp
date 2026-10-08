#include "ConditionalJournalPublicationTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/Json/JsonFileJournalInternal.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Serialization/StorageFilePublication.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>

// c++
#include <iostream>
#include <stdexcept>

using namespace Engine;
using namespace Engine::StorageFileUtility;
using namespace Engine::JsonFileJournal::Internal;

bool NEMTests::CheckConditionalJournalPublication(const Path& root) {

	const Path target = root / "conditional-publication.bin";
	const Path recoveryRoot = root / "ConditionalPublicationRecovery";
	JsonFileJournal::Scope scope{recoveryRoot, [target](const Path& path) { return path == target; }};
	auto check = [](bool result, const char* name) {
		if (!result) {
			std::cerr << "Conditional journal publication failed: " << name << '\n';
		}
		return result;
	};
	std::string error;
	JsonFileChange change{target, nullptr};
	change.bytes = "new";
	const auto leavePending = [](const Path&, std::string& diagnostic) {
		diagnostic = "復旧を保留";
		return false;
	};
	for (int mode = 0; mode < 3; ++mode) {
		if (!WriteBytes(target, "old")) {
			return false;
		}
		// 公開後の失敗を残し、退避と公開の間へ戻す
		if (JsonFileJournal::Commit(scope, {change}, "InterruptedPublication", error, leavePending, {},
				[] { throw std::runtime_error("公開後に中断"); })) {
			return false;
		}
		const auto pending = JsonFileJournal::GetRecoveries(scope, true);
		if (!check(pending.size() == 1, "pending record")) {
			return false;
		}
		const Path directory = pending.front();
		auto journal = JsonAdapter::Load(directory / "operation.json");
		auto& entry = journal.at("files").at(0);
		const auto record = DecodePublication(entry.at("publications").at(0));
		const Path stage = target.parent_path() / record.stageName;
		if (mode == 0) {
			std::error_code moveError;
			if (!MoveIfRevision(target, stage / "prepared", record.after, record.afterIdentity, moveError)) {
				return false;
			}
		} else if (mode == 1) {
			// 復旧の退避後に中断した世代も記録から再開
			StorageFilePublication restoration(target, record.after, std::string("old"));
			entry["publications"].push_back(EncodePublication(restoration.GetRecord()));
			if (!JsonAdapter::SaveCanonical(directory / "operation.json", journal)) {
				return false;
			}
			restoration.Persist();
			restoration.Retire();
		} else {
			// 同じbyteの別フォルダーを所有した退避と見なさない
			const Path retained = root / "retained-conditional-stage";
			std::filesystem::rename(stage, retained);
			std::filesystem::create_directory(stage);
			std::filesystem::copy_file(retained / "retired", stage / "retired");
			const Path displaced = root / "displaced-conditional-target";
			std::filesystem::rename(target, displaced);
			if (!check(!JsonFileJournal::Recover(scope, directory, error, {}) && std::filesystem::exists(stage / "retired") &&
						   std::filesystem::exists(retained / "retired") && std::filesystem::exists(displaced),
					"foreign stage preserved")) {
				return false;
			}
			std::filesystem::remove(stage / "retired");
			std::filesystem::remove(stage);
			std::filesystem::rename(retained, stage);
			std::filesystem::rename(displaced, target);
		}
		if (!check(JsonFileJournal::Recover(scope, directory, error, {}) && FileRevision(target) == record.before &&
					   JsonFileJournal::GetRecoveries(scope, true).empty(),
				"interruption restored")) {
			std::cerr << error << '\n';
			return false;
		}
	}

	// 未記録の隣接bakから欠けた保存先を復旧しない
	const Path directory = recoveryRoot / "unowned-backup";
	std::filesystem::create_directory(directory);
	const Path adjacent = Path(target.wstring() + L".bak");
	const Path displaced = root / "unowned-backup-target";
	std::filesystem::rename(target, displaced);
	if (!WriteBytes(adjacent, "old") || !WriteBytes(directory / "0.before", "old")) {
		return false;
	}
	const std::string before = FileRevision(adjacent);
	nlohmann::json journal = {
		{"state", "pending"}, {"files", nlohmann::json::array({{{"path", Algorithm::PathToUTF8(target)}, {"backup", "0.before"},
											{"before", before}, {"after", FileRevision(directory / "0.before")}}})}};
	if (!JsonAdapter::SaveCanonical(directory / "operation.json", journal) ||
		!check(!JsonFileJournal::Recover(scope, directory, error, {}) && !std::filesystem::exists(target) &&
				   std::filesystem::exists(adjacent),
			"unowned adjacent backup refused")) {
		return false;
	}
	std::filesystem::rename(displaced, target);
	if (!JsonFileJournal::KeepCurrent(scope, directory, error)) {
		return false;
	}
	std::filesystem::remove(adjacent);
	std::filesystem::remove(target);
	return true;
}
