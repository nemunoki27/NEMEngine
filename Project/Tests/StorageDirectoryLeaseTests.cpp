#include "StorageDirectoryLeaseTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/StorageDirectoryLease.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>

// c++
#include <system_error>
#include <iostream>

bool NEMTests::CheckStorageDirectoryLease(const std::filesystem::path& root) {

	using namespace Engine;
	using namespace Engine::StorageFileUtility;
	const auto directory = root / "DirectoryLease";
	const auto retained = root / "RetainedDirectoryLease";
	const auto child = directory / "prepared.bin";
	const auto published = root / "lease-published.bin";
	std::filesystem::create_directory(directory);
	if (!WriteBytes(child, "owned")) {
		return false;
	}
	std::error_code error;
	FileIdentity identity, childIdentity;
	if (!ReadIdentity(directory, identity, error) || !ReadIdentity(child, childIdentity, error)) {
		return false;
	}
	const std::string revision = FileRevision(child);
	bool passed = true;
	const auto check = [&](bool valid, const char* step) {
		if (!valid) {
			std::cerr << "Directory lease failed: " << step << " error=" << error.message() << '\n';
		}
		passed &= valid;
	};
	{
		StorageDirectoryLease lease(directory, identity);
		// 所有フォルダーの移動は失敗し、中身の公開は成功する
		std::filesystem::rename(directory, retained, error);
		check(static_cast<bool>(error) && std::filesystem::is_directory(directory) && !std::filesystem::exists(retained),
			"protect root rename");
		error.clear();
		check(MoveIfRevision(child, published, revision, childIdentity, error) && !error, "publish child");
		check(!RemoveIfIdentity(directory, identity, error) && static_cast<bool>(error), "protect root removal");
	}
	// 保持の終了後は通常のフォルダー操作へ戻る
	std::filesystem::rename(directory, retained, error);
	check(!error && !std::filesystem::exists(directory), "release root rename");
	std::filesystem::rename(retained, directory, error);
	check(!error, "restore root name");

	// 同名でも所有が異なるフォルダーを保持しない
	FileIdentity foreign = identity;
	++foreign.volumeID;
	bool rejected = false;
	try {
		StorageDirectoryLease lease(directory, foreign);
	} catch (const std::system_error&) {
		rejected = true;
	}
	check(rejected, "reject foreign root");
	// 通常ファイルを作業フォルダーとして扱わない
	FileIdentity publishedIdentity;
	check(ReadIdentity(published, publishedIdentity, error), "read published identity");
	rejected = false;
	try {
		StorageDirectoryLease lease(published, publishedIdentity);
	} catch (const std::system_error&) {
		rejected = true;
	}
	check(rejected && FileRevision(published) == revision, "reject normal file");
	check(RemoveIfRevision(published, revision, childIdentity, error) && !error, "remove published child");
	check(RemoveIfIdentity(directory, identity, error) && !error, "remove released root");
	return passed;
}
