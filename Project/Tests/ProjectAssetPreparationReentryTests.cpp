#include "ProjectAssetPreparationReentryTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Assets/Project/ProjectAssetCopyTransaction.h>
#include <Engine/Editor/Assets/Project/ProjectDirectoryCopyTransaction.h>
#include <Engine/Editor/Assets/Project/ProjectAssetMoveTransaction.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>

// c++
#include <iostream>
#include <stdexcept>

namespace {

	// callbackの途中でファイルを公開しない
	bool CheckCopyReentry(const std::filesystem::path& source, const std::filesystem::path& root) {

		using namespace Engine;
		using namespace Engine::StorageFileUtility;
		const auto target = root / "copy.dat";
		ProjectAssetCopyTransaction transaction(root);
		std::string diagnostic = "stale diagnostic";
		if (!transaction.Add(source, target) || !transaction.Stage(diagnostic)) {
			return false;
		}
		bool nestedCalled = false;
		const bool prepared = transaction.Prepare(
			0,
			[&](const auto&, std::string& bytes) {
				std::string nestedDiagnostic;
				const bool prepareRejected = !transaction.Prepare(
					0,
					[&](const auto&, std::string&) {
						nestedCalled = true;
						return true;
					},
					nestedDiagnostic);
				const bool publishRejected = !transaction.Publish(nestedDiagnostic);
				const bool stageRejected = !transaction.Stage(nestedDiagnostic);
				const bool addRejected = !transaction.Add(source, root / "extra.dat");
				transaction.Commit();
				bytes = "copy prepared";
				return prepareRejected && publishRejected && stageRejected && addRejected && !std::filesystem::exists(target);
			},
			diagnostic);
		if (!prepared || nestedCalled || !diagnostic.empty() || std::filesystem::exists(target) ||
			!transaction.Publish(diagnostic)) {
			return false;
		}
		transaction.Commit();
		return ReadVerifiedBytes(target, FileRevision(target)) == "copy prepared";
	}

	// 失敗の説明を保持し、例外後も同じ操作で再試行する
	bool CheckCopyRetry(const std::filesystem::path& source, const std::filesystem::path& root) {

		using namespace Engine;
		using namespace Engine::StorageFileUtility;
		const auto target = root / "retry.dat";
		{
			ProjectAssetCopyTransaction transaction(root);
			std::string diagnostic;
			if (!transaction.Add(source, target) || !transaction.Stage(diagnostic)) {
				return false;
			}
			const auto staged = transaction.GetStagedPath(0);
			bool called = false;
			{
				NEMTests::TestFileReadLock lock(staged, false);
				if (transaction.Prepare(
						0,
						[&](const auto&, std::string&) {
							called = true;
							return true;
						},
						diagnostic) ||
					called || diagnostic.empty()) {
					return false;
				}
			}
			if (transaction.Prepare(
					0,
					[&](const auto&, std::string& bytes) {
						bytes = "not published";
						diagnostic = "specific reference failure";
						return false;
					},
					diagnostic) ||
				diagnostic != "specific reference failure" ||
				ReadVerifiedBytes(staged, FileRevision(staged)) != "source bytes") {
				return false;
			}
			if (transaction.Prepare(
					0, [](const auto&, std::string&) -> bool { throw std::runtime_error("prepare retry fixture"); },
					diagnostic) ||
				diagnostic.find("prepare retry fixture") == std::string::npos) {
				return false;
			}
			if (!transaction.Prepare(
					0,
					[](const auto&, std::string& bytes) {
						bytes = "retry prepared";
						return true;
					},
					diagnostic) ||
				!diagnostic.empty() || !transaction.Publish(diagnostic) ||
				ReadVerifiedBytes(target, FileRevision(target)) != "retry prepared") {
				return false;
			}
		}
		return !std::filesystem::exists(target);
	}

	// 子の準備中に親の計画と公開を進めない
	bool CheckDirectoryReentry(const std::filesystem::path& source, const std::filesystem::path& root) {

		using namespace Engine;
		const auto target = root / "tree";
		{
			ProjectDirectoryCopyTransaction transaction(target);
			std::string diagnostic;
			if (!transaction.Begin(diagnostic)) {
				return false;
			}
			const bool staged =
				transaction.StageFile(source, "nested/file.dat", diagnostic, [&](const auto&, std::string& bytes) {
					std::string nestedDiagnostic;
					const bool directoryRejected = !transaction.AddDirectory("extra", nestedDiagnostic);
					const bool stageRejected = !transaction.StageFile(source, "second.dat", nestedDiagnostic);
					const bool publishRejected = !transaction.Publish(nestedDiagnostic);
					transaction.Commit();
					bytes = "tree prepared";
					return directoryRejected && stageRejected && publishRejected && !std::filesystem::exists(target);
				});
			if (!staged || !diagnostic.empty() || !transaction.Publish(diagnostic) ||
				StorageFileUtility::ReadVerifiedBytes(target / "nested/file.dat",
					StorageFileUtility::FileRevision(target / "nested/file.dat")) != "tree prepared") {
				return false;
			}
		}
		return !std::filesystem::exists(target);
	}

	// 移動の編集失敗で理由と元の所有を保つ
	bool CheckMoveRetry(const std::filesystem::path& root) {

		using namespace Engine;
		using namespace Engine::StorageFileUtility;
		const auto source = root / "move.dat";
		const auto target = root / "moved.dat";
		if (!WriteBytes(source, "move source")) {
			return false;
		}
		ProjectAssetMoveTransaction transaction;
		std::string diagnostic;
		if (!transaction.Add(source, target) ||
			transaction.Prepare(
				0,
				[&](std::string& bytes) {
					bytes = "not published";
					diagnostic = "specific move reference failure";
					return false;
				},
				diagnostic) ||
			diagnostic != "specific move reference failure" ||
			ReadVerifiedBytes(source, FileRevision(source)) != "move source") {
			return false;
		}
		if (!transaction.Prepare(
				0,
				[](std::string& bytes) {
					bytes = "move prepared";
					return true;
				},
				diagnostic) ||
			!diagnostic.empty() || !transaction.Execute(diagnostic)) {
			return false;
		}
		transaction.Commit();
		return ReadVerifiedBytes(target, FileRevision(target)) == "move prepared";
	}
}

bool NEMTests::CheckProjectPreparationReentry() {

	TestDirectory fixture("ProjectPreparationReentry");
	const auto source = fixture.GetPath() / "source.dat";
	if (!Engine::StorageFileUtility::WriteBytes(source, "source bytes")) {
		return false;
	}
	if (!CheckCopyReentry(source, fixture.GetPath())) {
		std::cerr << "Project copy preparation reentry failed\n";
		return false;
	}
	if (!CheckCopyRetry(source, fixture.GetPath())) {
		std::cerr << "Project copy preparation retry failed\n";
		return false;
	}
	if (!CheckDirectoryReentry(source, fixture.GetPath())) {
		std::cerr << "Project directory preparation reentry failed\n";
		return false;
	}
	if (!CheckMoveRetry(fixture.GetPath())) {
		std::cerr << "Project move preparation retry failed\n";
		return false;
	}
	return true;
}
