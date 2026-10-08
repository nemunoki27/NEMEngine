#include "StorageFilePublicationTests.h"

//============================================================================
//	include
//============================================================================
#include "TestFixtures.h"
#include <Engine/Core/Foundation/Serialization/StorageFilePublication.h>
#include <Engine/Core/Foundation/Serialization/StoragePreparationDirectory.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>

// c++
#include <iostream>
#include <stdexcept>

bool NEMTests::CheckStorageFilePublication(const std::filesystem::path& root) {

	using namespace Engine;
	using namespace Engine::StorageFileUtility;
	const Path target = root / "publication.bin";
	if (!WriteBytes(target, "original")) {
		return false;
	}
	const std::string before = FileRevision(target);
	std::error_code error;
	Path stage;
	bool passed = true;
	const auto check = [&](bool valid, const char* step) {
		if (!valid) {
			std::cerr << "File publication failed: " << step << " error=" << error.message() << '\n';
		}
		passed &= valid;
	};
	const auto rejected = [](const auto& action) {
		try {
			action();
		} catch (const std::exception&) {
			return true;
		}
		return false;
	};
	{
		// 新規書込は同じ内容の既存ファイルも置き換えない
		const Path created = root / "created-owned-bytes.bin";
		FileIdentity createdIdentity;
		check(WriteBytesWithoutReplacement(created, "created", createdIdentity), "create owned bytes");
		FileIdentity currentIdentity;
		const std::string revision = FileRevision(created);
		check(ReadIdentity(created, currentIdentity, error) && currentIdentity == createdIdentity,
			"return created handle identity");
		FileIdentity refusedIdentity;
		check(!WriteBytesWithoutReplacement(created, "created", refusedIdentity) &&
				  !WriteBytesWithoutReplacement(created, "other", refusedIdentity),
			"refuse existing byte target");
		check(ReadIdentity(created, currentIdentity, error) && currentIdentity == createdIdentity &&
				  FileRevision(created) == revision,
			"preserve existing byte identity");
		check(RemoveIfRevision(created, revision, createdIdentity, error), "remove owned byte fixture");
	}
	{
		// 作成後に差し替わったファイルを回収対象へ取り込まない
		const Path directory = root / "ExactCapture";
		const Path file = directory / "prepared";
		const Path retained = directory / "retained";
		std::filesystem::create_directory(directory);
		FileIdentity originalIdentity, foreignIdentity, rootIdentity;
		std::string revision;
		{
			StoragePreparationDirectory preparation(directory);
			rootIdentity = preparation.GetIdentity();
			check(WriteBytesWithoutReplacement(file, "same bytes", originalIdentity), "create exact capture fixture");
			revision = FileRevision(file);
			preparation.Track(file, revision);
			check(MoveIfRevision(file, retained, revision, originalIdentity, error), "retain exact capture original");
			check(WriteBytesWithoutReplacement(file, "same bytes", foreignIdentity), "substitute exact capture fixture");
			check(rejected([&] { preparation.Capture(file, originalIdentity); }), "refuse foreign capture identity");
		}
		check(std::filesystem::exists(file) && std::filesystem::exists(retained), "preserve unowned capture files");
		check(RemoveIfRevision(file, revision, foreignIdentity, error) &&
				  RemoveIfRevision(retained, revision, originalIdentity, error) &&
				  RemoveIfIdentity(directory, rootIdentity, error),
			"remove exact capture fixtures");
	}
	{
		StorageFilePublication publication(target, before, "prepared");
		stage = target.parent_path() / Algorithm::PathFromUTF8(publication.GetRecord().stageName);
		// 記録前には元ファイルを動かさない
		check(rejected([&] { publication.Retire(); }) && FileRevision(target) == before, "require journal intent");
	}
	check(!std::filesystem::exists(stage), "unrecorded preparation cleanup");

	{
		// 保存先の欠損だけでは退避済みと判定しない
		StorageFilePublication publication(target, before, std::nullopt);
		const auto record = publication.GetRecord();
		publication.Persist();
		const Path unrelated = root / "unrelated-retired.bin";
		check(MoveIfRevision(target, unrelated, before, record.beforeIdentity, error), "simulate unrelated disappearance");
		check(!publication.IsPublished() && !publication.HasRetiredOriginal(), "require owned retirement proof");
		check(rejected([&] { publication.Publish(); }), "refuse missing target without retirement");
		check(MoveIfRevision(unrelated, target, before, record.beforeIdentity, error), "restore unrelated fixture move");
		check(StorageFilePublication::Cleanup(target, record, error), "missing target stage cleanup");
	}

	StorageFilePublicationRecord interrupted;
	{
		StorageFilePublication publication(target, before, "replacement");
		interrupted = publication.GetRecord();
		publication.Persist();
		publication.Retire();
		check(FileRevision(target) == "missing" && publication.HasRetiredOriginal(), "retirement ownership");
	}
	{
		// 中断前の記録から元ファイルを復元する
		auto publication = StorageFilePublication::Resume(target, interrupted);
		publication.RestoreRetired();
		check(FileRevision(target) == before, "restore interrupted retirement");
		check(StorageFilePublication::Cleanup(target, interrupted, error) && !error, "restored stage cleanup");
	}

	{
		StorageFilePublication publication(target, before, "replacement");
		const auto record = publication.GetRecord();
		publication.Persist();
		publication.Retire();
		// 退避後に現れた別ファイルを公開も復旧も上書きしない
		check(WriteBytes(target, "foreign"), "create foreign target");
		const std::string foreignRevision = FileRevision(target);
		FileIdentity foreignIdentity;
		check(ReadIdentity(target, foreignIdentity, error), "capture foreign target");
		check(
			rejected([&] { publication.Publish(); }) && FileRevision(target) == foreignRevision, "refuse foreign publication");
		check(
			rejected([&] { publication.RestoreRetired(); }) && publication.HasRetiredOriginal(), "refuse foreign restoration");
		check(RemoveIfRevision(target, foreignRevision, foreignIdentity, error), "remove fixture foreign target");
		publication.RestoreRetired();
		check(FileRevision(target) == before, "restore after explicit conflict removal");
		check(StorageFilePublication::Cleanup(target, record, error), "conflict stage cleanup");
	}

	{
		StorageFilePublication publication(target, before, "replacement");
		const auto record = publication.GetRecord();
		stage = target.parent_path() / Algorithm::PathFromUTF8(record.stageName);
		publication.Persist();
		publication.Retire();
		// 同じ内容の別ファイルへ差し替わっても公開しない
		const Path prepared = stage / "prepared";
		const Path retained = stage / "retained-prepared";
		check(MoveIfRevision(prepared, retained, record.after, record.afterIdentity, error), "retain owned preparation");
		check(WriteBytes(prepared, "replacement"), "substitute equal bytes");
		FileIdentity substituted;
		check(ReadIdentity(prepared, substituted, error), "capture substituted preparation");
		check(rejected([&] { publication.Publish(); }) && FileRevision(target) == "missing", "refuse equal-byte foreign owner");
		check(!StorageFilePublication::Cleanup(target, record, error) && std::filesystem::exists(prepared) &&
				  publication.HasRetiredOriginal(),
			"preserve foreign preparation and retired original");
		check(RemoveIfRevision(prepared, record.after, substituted, error), "remove substituted fixture");
		check(MoveIfRevision(retained, prepared, record.after, record.afterIdentity, error), "restore owned preparation");
		publication.Publish();
		check(publication.IsPublished() && FileRevision(target) == record.after, "publish replacement");
		publication.Publish();
		check(StorageFilePublication::Cleanup(target, record, error), "completed stage cleanup");
	}

	{
		// 読込中の元ファイルを退避できなければ保持する
		const std::string current = FileRevision(target);
		StorageFilePublication publication(target, current, "locked replacement");
		const auto record = publication.GetRecord();
		publication.Persist();
		{
			TestFileReadLock locked(target);
			check(rejected([&] { publication.Retire(); }) && FileRevision(target) == current, "retain locked target");
		}
		check(StorageFilePublication::Cleanup(target, record, error), "locked preparation cleanup");
	}

	{
		StorageFilePublication publication(target, FileRevision(target), std::nullopt);
		const auto record = publication.GetRecord();
		publication.Persist();
		publication.Retire();
		publication.Publish();
		check(publication.IsPublished() && publication.HasRetiredOriginal(), "recorded removal");
		check(StorageFilePublication::Cleanup(target, record, error), "removed stage cleanup");
	}
	{
		StorageFilePublication publication(target, "missing", "created");
		const auto record = publication.GetRecord();
		publication.Persist();
		publication.Retire();
		publication.Publish();
		check(publication.IsPublished(), "create without replacement");
		check(StorageFilePublication::Cleanup(target, record, error), "created stage cleanup");
		check(RemoveIfRevision(target, record.after, record.afterIdentity, error), "remove created fixture");
	}
	{
		StorageFilePublicationRecord invalid;
		invalid.stageName = "../outside";
		check(rejected([&] { StorageFilePublication::Resume(target, invalid); }), "refuse stage path traversal");
	}
	return passed;
}
