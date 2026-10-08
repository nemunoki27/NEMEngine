#include "ProjectCopyPreparationTests.h"
#include "ProjectAssetPreparationReentryTests.h"

//============================================================================
//	include
//============================================================================
#include "TestFixtures.h"
#include <Engine/Editor/Assets/Project/ProjectAssetCopyTransaction.h>
#include <Engine/Editor/Assets/Project/ProjectDirectoryCopyTransaction.h>
#include <Engine/Editor/Assets/Project/ProjectAssetPath.h>
#include <Engine/Editor/Assets/Project/ProjectAssetDocumentFactory.h>
#include <Engine/Editor/Assets/Project/ProjectAssetFileUtility.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <system_error>

using namespace Engine;
using namespace Engine::StorageFileUtility;

namespace {

	// 既存文書と孤立metaを新規作成で書き換えない
	bool CheckProjectCreationOwnership() {

		NEMTests::TestDirectory assets("ProjectCreationOwnership", RuntimePaths::GetGameAssetsRoot());
		const auto existing = assets.GetPath() / "existing.txt";
		if (!WriteBytes(existing, "owned by another operation")) {
			return false;
		}
		FileIdentity originalIdentity, currentIdentity;
		std::error_code error;
		if (!ReadIdentity(existing, originalIdentity, error) ||
			ProjectAssetDocumentFactory::CreateTextFile(existing, "replace") ||
			!ReadIdentity(existing, currentIdentity, error) || currentIdentity != originalIdentity ||
			ReadVerifiedBytes(existing, FileRevision(existing)) != "owned by another operation") {
			return false;
		}
		// 書込できない先は完成した文書として返さない
		const auto missing = existing / "failed.txt";
		if (ProjectAssetDocumentFactory::CreateTextFile(missing, "not published") || std::filesystem::exists(missing, error)) {
			return false;
		}
		const auto preferred = assets.GetPath() / "new.material.json";
		const auto meta = ProjectAssetPath::MakeMetaPath(preferred);
		if (!WriteBytes(meta, "foreign identity")) {
			return false;
		}
		const auto target = ProjectAssetPath::MakeUniquePath(preferred);
		if (target == preferred || !ProjectAssetPath::IsSameOrChildPath(target, assets.GetPath())) {
			return false;
		}
		const auto directory = std::string("GameAssets/") + assets.GetPath().filename().string();
		const auto created =
			ProjectAssetFileUtility::Create(ProjectAssetSource::Game, directory, ProjectAssetFileKind::Material, "new");
		if (!created.success || created.fullPath != target || !std::filesystem::is_regular_file(target) ||
			ReadVerifiedBytes(meta, FileRevision(meta)) != "foreign identity") {
			return false;
		}
		// 同名Scriptも選ばれたファイル名で別の型を作る
		const auto first =
			ProjectAssetFileUtility::Create(ProjectAssetSource::Game, directory, ProjectAssetFileKind::Script, "Example");
		const auto second =
			ProjectAssetFileUtility::Create(ProjectAssetSource::Game, directory, ProjectAssetFileKind::Script, "Example");
		if (!first.success || !second.success || first.fullPath == second.fullPath ||
			ReadVerifiedBytes(first.fullPath, FileRevision(first.fullPath)).find("class Example :") == std::string::npos ||
			ReadVerifiedBytes(second.fullPath, FileRevision(second.fullPath)).find("class Example1 :") == std::string::npos) {
			return false;
		}
		return true;
	}

	// 仮想ディレクトリから所属ルートの外へ出ないことを確認する
	bool CheckProjectRelativePaths() {

		for (const wchar_t* path :
			{L"C:/outside", L"C:relative", L"\\outside", L"\\\\server\\share", L"../outside", L"child/../outside"}) {
			if (ProjectAssetPath::IsSafeRelativePath(path)) {
				return false;
			}
			for (auto source : {ProjectAssetSource::Game, ProjectAssetSource::Engine}) {
				const auto virtualPath =
					std::string(ProjectAssetPath::GetSourceVirtualRoot(source)) + "/" + Algorithm::PathToUTF8(path);
				if (!ProjectAssetPath::ResolveVirtualDirectory(source, virtualPath).empty()) {
					return false;
				}
			}
		}
		for (const wchar_t* path : {L"", L".", L"child", L"child/nested", L"child\\nested", L"child/"}) {
			if (!ProjectAssetPath::IsSafeRelativePath(path)) {
				return false;
			}
			for (auto source : {ProjectAssetSource::Game, ProjectAssetSource::Engine}) {
				const auto virtualPath =
					std::string(ProjectAssetPath::GetSourceVirtualRoot(source)) + "/" + Algorithm::PathToUTF8(path);
				const auto resolved = ProjectAssetPath::ResolveVirtualDirectory(source, virtualPath);
				if (resolved.empty() ||
					!ProjectAssetPath::IsSameOrChildPath(resolved, ProjectAssetPath::GetSourceRoot(source))) {
					return false;
				}
			}
		}
		return true;
	}
}

bool NEMTests::TestProjectCopyPreparationOwnership() {

	if (!CheckProjectRelativePaths() || !CheckProjectCreationOwnership() || !CheckProjectPreparationReentry()) {
		return false;
	}
	TestDirectory fixture("ProjectCopyPreparationOwnership");
	const auto source = fixture.GetPath() / "source.dat";
	// 分割境界と零byteを含むコピーを照合する
	std::string original(1024 * 1024 + 73, '\0');
	for (size_t index = 0; index < original.size(); ++index) {
		original[index] = static_cast<char>(index % 251);
	}
	if (!WriteBytes(source, original)) {
		return false;
	}
	const auto copy = fixture.GetPath() / "copy.dat";
	FileIdentity created, current;
	std::string revision;
	std::error_code error;
	if (!CopyFileWithoutReplacement(source, copy, created, revision) || !ReadIdentity(copy, current, error) ||
		current != created || ReadVerifiedBytes(copy, revision) != original || revision != FileRevision(source) ||
		CopyFileWithoutReplacement(source, copy, created, revision)) {
		return false;
	}
	bool rejectedRevision = false;
	try {
		ReadVerifiedBytes(copy, "wrong revision");
	} catch (const std::runtime_error&) {
		rejectedRevision = true;
	}
	if (!rejectedRevision) {
		return false;
	}
	// コピー元の読込拒否では何も作成しない
	{
		TestFileReadLock lock(source, false);
		const auto blocked = fixture.GetPath() / "blocked.dat";
		if (CopyFileWithoutReplacement(source, blocked, created, revision) || std::filesystem::exists(blocked)) {
			return false;
		}
	}

	for (int mode = 0; mode < 7; ++mode) {
		const auto directory = fixture.GetPath() / std::to_string(mode);
		std::filesystem::create_directory(directory);
		const auto target = directory / "target.dat";
		const auto retained = directory / "retained.dat";
		const auto movedRoot = directory / "retained-root";
		std::filesystem::path staged;
		bool called = false;
		bool rootHeld = false;
		{
			ProjectAssetCopyTransaction transaction(directory);
			std::string diagnostic;
			if (!transaction.Add(source, target) || !transaction.Stage(diagnostic)) {
				return false;
			}
			staged = transaction.GetStagedPath(0);
			if (mode == 5) {
				// 呼出し間の同名フォルダー差替えは拒否する
				std::filesystem::rename(staged.parent_path(), movedRoot);
				std::filesystem::create_directory(staged.parent_path());
				std::filesystem::copy_file(source, staged);
			}
			const bool prepared = transaction.Prepare(
				0,
				[&](const auto& path, std::string& bytes) {
					called = true;
					bytes = "prepared";
					if (mode == 0) {
						return false;
					}
					if (mode == 1) {
						throw std::runtime_error("prepare fixture failed");
					}
					if (mode == 2) {
						// 同じbyteでも別ファイルを編集対象へ取り込まない
						std::filesystem::rename(path, retained);
						FileIdentity replacement;
						std::string replacementRevision;
						if (!CopyFileWithoutReplacement(source, path, replacement, replacementRevision)) {
							throw std::runtime_error("replacement fixture failed");
						}
					} else if (mode == 3) {
						if (!WriteBytes(path, "foreign")) {
							throw std::runtime_error("external edit fixture failed");
						}
					} else if (mode == 4) {
						std::error_code renameError;
						std::filesystem::rename(path.parent_path(), movedRoot, renameError);
						rootHeld = static_cast<bool>(renameError);
					} else if (mode == 6) {
						// 同じファイルへの外部書換えも削除しない
						std::ofstream file(path, std::ios::binary | std::ios::trunc);
						file << "foreign";
						if (!file) {
							throw std::runtime_error("in-place edit fixture failed");
						}
					}
					return true;
				},
				diagnostic);
			if (mode < 2) {
				// 編集失敗では元の作業内容を保持する
				if (prepared || diagnostic.empty() || ReadVerifiedBytes(staged, FileRevision(source)) != original) {
					return false;
				}
			} else if (mode == 4) {
				if (!prepared || !rootHeld || !transaction.Publish(diagnostic) ||
					ReadVerifiedBytes(target, FileRevision(target)) != "prepared") {
					return false;
				}
			} else if (prepared || diagnostic.empty() || transaction.Publish(diagnostic) || (mode == 5 && called)) {
				return false;
			}
		}
		if (std::filesystem::exists(target)) {
			return false;
		}
		if (mode == 2 || mode == 5) {
			if (!std::filesystem::exists(staged) || ReadVerifiedBytes(staged, FileRevision(source)) != original) {
				return false;
			}
			const auto owned = mode == 2 ? retained : movedRoot / staged.filename();
			if (!std::filesystem::exists(owned) || ReadVerifiedBytes(owned, FileRevision(source)) != original) {
				return false;
			}
		} else if (mode == 3 || mode == 6) {
			if (!std::filesystem::exists(staged) || ReadVerifiedBytes(staged, FileRevision(staged)) != "foreign") {
				return false;
			}
		} else if (std::filesystem::exists(staged.parent_path())) {
			return false;
		}
	}
	return true;
}

bool NEMTests::TestProjectDirectoryCopyOwnership() {

	TestDirectory fixture("ProjectDirectoryCopyOwnership");
	const auto source = fixture.GetPath() / "source.dat";
	if (!WriteBytes(source, "original")) {
		return false;
	}
	for (bool replaceRoot : {false, true}) {
		const auto directory = fixture.GetPath() / (replaceRoot ? "root" : "child");
		std::filesystem::create_directory(directory);
		const auto target = directory / "target";
		const auto retained = directory / "retained";
		std::filesystem::path staged;
		{
			ProjectDirectoryCopyTransaction transaction(target);
			std::string diagnostic;
			if (!transaction.Begin(diagnostic) || !transaction.StageFile(source, "nested/model.dat", diagnostic)) {
				std::cerr << "Directory ownership stage failed: " << diagnostic << '\n';
				return false;
			}
			for (const auto& entry : std::filesystem::directory_iterator(directory)) {
				if (entry.path().filename().wstring().starts_with(L".nem-copy-")) {
					staged = entry.path();
				}
			}
			if (staged.empty()) {
				return false;
			}
			// 呼出し間に差し替わったフォルダーへ書き込まない
			std::filesystem::rename(replaceRoot ? staged : staged / "nested", retained);
			std::filesystem::create_directories(staged / "nested");
			std::filesystem::copy_file(source, staged / "nested/model.dat");
			if (transaction.AddDirectory("nested/new", diagnostic) || diagnostic.empty() ||
				transaction.StageFile(source, "nested/second.dat", diagnostic) || transaction.Publish(diagnostic)) {
				std::cerr << "Directory ownership replacement accepted: " << replaceRoot << ' ' << diagnostic << '\n';
				return false;
			}
		}
		const auto owned = replaceRoot ? retained / "nested/model.dat" : retained / "model.dat";
		if (std::filesystem::exists(target) || std::filesystem::exists(staged / "nested/new") ||
			std::filesystem::exists(staged / "nested/second.dat") ||
			ReadVerifiedBytes(staged / "nested/model.dat", FileRevision(source)) != "original" ||
			ReadVerifiedBytes(owned, FileRevision(source)) != "original") {
			std::cerr << "Directory ownership retained content failed: " << replaceRoot << '\n';
			return false;
		}
	}

	// 編集中は作業先と全ての親フォルダーを保持する
	const auto target = fixture.GetPath() / "published";
	ProjectDirectoryCopyTransaction transaction(target);
	std::string diagnostic;
	bool parentHeld = false, rootHeld = false;
	if (!transaction.Begin(diagnostic) ||
		!transaction.StageFile(source, "nested/model.dat", diagnostic,
			[&](const auto& path, std::string& bytes) {
				const auto parent = path.parent_path().parent_path();
				std::error_code error;
				std::filesystem::rename(parent, fixture.GetPath() / "moved-parent", error);
				parentHeld = static_cast<bool>(error);
				std::filesystem::rename(parent.parent_path(), fixture.GetPath() / "moved-root", error);
				rootHeld = static_cast<bool>(error);
				bytes = "prepared";
				return parentHeld && rootHeld;
			}) ||
		!transaction.Publish(diagnostic)) {
		std::cerr << "Directory parent retention failed: " << parentHeld << ' ' << rootHeld << ' ' << diagnostic << '\n';
		return false;
	}
	transaction.Commit();
	return parentHeld && rootHeld &&
		   ReadVerifiedBytes(target / "nested/model.dat", FileRevision(target / "nested/model.dat")) == "prepared";
}
