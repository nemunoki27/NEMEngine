#include "TestContracts.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Assets/Project/ProjectAssetCopyTransaction.h>
#include <Engine/Editor/Assets/Project/ProjectAssetCopyUtility.h>
#include <Engine/Editor/Assets/Project/ProjectAssetMoveUtility.h>
#include <Engine/Editor/Assets/Project/ProjectAssetDocumentPatch.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <fstream>
#include <iterator>
#include <iostream>

bool NEMTests::TestProjectAssetCopyTransaction() {

	TestDirectory directory("ProjectAssetCopyTransaction");
	const auto sourceRoot = directory.GetPath() / "Source";
	const auto targetRoot = directory.GetPath() / "Target";
	std::filesystem::create_directories(sourceRoot);
	std::filesystem::create_directories(targetRoot);
	const auto source = sourceRoot / "model.gltf";
	const auto sidecar = sourceRoot / "model.bin";
	const auto target = targetRoot / "copy.gltf";
	const auto sidecarTarget = targetRoot / "copy.bin";
	{
		std::ofstream file(source);
		file << "source";
	}
	{
		std::ofstream file(sidecar);
		file << "sidecar";
	}
	const auto clean = [&] {
		for (const auto& entry : std::filesystem::directory_iterator(targetRoot)) {
			if (entry.path().filename().wstring().starts_with(L".nem-copy-")) {
				return false;
			}
		}
		return true;
	};

	// 作業開始後に現れた既存ファイルを置き換えない
	{
		Engine::ProjectAssetCopyTransaction transaction(targetRoot);
		std::string diagnostic;
		if (!transaction.Add(source, target) || !transaction.Add(sidecar, sidecarTarget) || !transaction.Stage(diagnostic) ||
			transaction.Add(source, targetRoot / "late.gltf")) {
			return false;
		}
		{
			std::ofstream file(target);
			file << "existing";
		}
		if (transaction.Publish(diagnostic) || diagnostic.empty()) {
			return false;
		}
	}
	std::ifstream retained(target);
	const std::string retainedBytes((std::istreambuf_iterator<char>(retained)), std::istreambuf_iterator<char>());
	retained.close();
	if (retainedBytes != "existing" || std::filesystem::exists(sidecarTarget) || !clean()) {
		return false;
	}
	std::filesystem::remove(target);

	// 付随ファイルの読込失敗では何も公開しない
	{
		Engine::ProjectAssetCopyTransaction transaction(targetRoot);
		std::string diagnostic;
		if (!transaction.Add(source, target) || !transaction.Add(sourceRoot / "missing.bin", sidecarTarget) ||
			transaction.Stage(diagnostic) || diagnostic.empty()) {
			return false;
		}
	}
	if (std::filesystem::exists(target) || std::filesystem::exists(sidecarTarget) || !clean()) {
		return false;
	}

	// 確定した全ファイルだけを公開先へ残す
	{
		Engine::ProjectAssetCopyTransaction transaction(targetRoot);
		std::string diagnostic;
		if (!transaction.Add(source, target) || transaction.Add(source, target) || !transaction.Add(sidecar, sidecarTarget) ||
			!transaction.Stage(diagnostic) || !transaction.Publish(diagnostic)) {
			return false;
		}
		transaction.Commit();
		if (transaction.Stage(diagnostic) || transaction.Publish(diagnostic)) {
			return false;
		}
	}
	if (!std::filesystem::exists(source) || !std::filesystem::exists(sidecar) || !std::filesystem::exists(target) ||
		!std::filesystem::exists(sidecarTarget) || !clean()) {
		return false;
	}

	// 末尾に区切り文字がある公開先も同じフォルダーとして扱う
	{
		Engine::ProjectAssetCopyTransaction transaction(targetRoot / "");
		std::string diagnostic;
		if (!transaction.Add(source, targetRoot / "trailing.gltf") || !transaction.Stage(diagnostic) ||
			!transaction.Publish(diagnostic)) {
			return false;
		}
		transaction.Commit();
	}
	if (!std::filesystem::is_regular_file(targetRoot / "trailing.gltf") || !clean()) {
		return false;
	}

	// 改名・保存の失敗では元のファイルを保持する
	using namespace Engine;
	TestDirectory assets("ProjectAssetOperation", RuntimePaths::GetGameAssetsRoot());
	const auto material = assets.GetPath() / "original.material.json";
	nlohmann::json data = {{"name", "savedName"}, {"custom", 19}};
	if (!JsonFile::Save(material, data)) {
		std::cerr << "Project copy fixture save failed\n";
		return false;
	}
	ProjectAssetEntry entry;
	entry.assetPath = RuntimePaths::ToAssetPath(material);
	entry.type = AssetType::Material;
	{
		TestFileReadLock lock(material);
		const auto renamed = ProjectAssetMoveUtility::RenameAsset(entry, "renamed");
		if (renamed.success || renamed.message.empty() || !std::filesystem::exists(material) ||
			std::filesystem::exists(assets.GetPath() / "renamed.material.json")) {
			std::cerr << "Project rename lock contract failed: " << renamed.message << '\n';
			return false;
		}
		if (ProjectAssetDocumentPatch::PatchRenamedJsonAsset(material, AssetType::Material)) {
			std::cerr << "Project JSON save lock contract failed\n";
			return false;
		}
	}
	if (!JsonFile::TryLoad(material, data) || data["name"] != "savedName" || data["custom"] != 19) {
		std::cerr << "Project rename retained data failed\n";
		return false;
	}

	// 拡張子がjsonでないMaterialも表示名を更新する
	const auto plainMaterial = assets.GetPath() / "plain.material";
	if (!JsonFile::Save(plainMaterial, data) ||
		!ProjectAssetDocumentPatch::PatchDuplicatedJsonAsset(plainMaterial, AssetType::Material) ||
		!JsonFile::TryLoad(plainMaterial, data) || data["name"] != "plain" || data["custom"] != 19) {
		std::cerr << "Project plain material update failed\n";
		return false;
	}

	// JSON破損を含む複製は公開しない
	const auto invalid = assets.GetPath() / "invalid.material.json";
	std::ofstream(invalid) << "{";
	entry.assetPath = RuntimePaths::ToAssetPath(invalid);
	const auto duplicated = ProjectAssetCopyUtility::DuplicateAsset(entry, {});
	if (duplicated.success || duplicated.message.empty()) {
		std::cerr << "Project corrupt copy rejection failed: " << duplicated.message << '\n';
		return false;
	}
	if (std::distance(std::filesystem::directory_iterator(assets.GetPath()), std::filesystem::directory_iterator{}) != 3) {
		std::cerr << "Project corrupt copy cleanup failed\n";
		return false;
	}

	// 取込中の読込失敗は作成したフォルダーごと取り消す
	const auto externalDirectory = sourceRoot / "external";
	std::filesystem::create_directory(externalDirectory);
	const auto stagingName = ".nem-copy-00112233445566778899aabbccddeeff";
	std::filesystem::create_directory(externalDirectory / stagingName);
	std::ofstream(externalDirectory / stagingName / "pending.txt") << "pending";
	const auto locked = externalDirectory / "locked.txt";
	std::ofstream(locked) << "external";
	const auto virtualDirectory = "GameAssets/" + assets.GetPath().filename().string();
	{
		TestFileReadLock lock(locked, false);
		const auto imported =
			ProjectAssetCopyUtility::ImportExternalDirectory(ProjectAssetSource::Game, virtualDirectory, externalDirectory);
		if (imported.success || imported.message.empty() || std::filesystem::exists(assets.GetPath() / "external")) {
			std::cerr << "Project folder import failure cleanup failed: " << imported.message << '\n';
			return false;
		}
	}
	// 単体取込の失敗も公開先に残さない
	{
		TestFileReadLock lock(locked, false);
		const auto importedFile =
			ProjectAssetCopyUtility::ImportExternalFile(ProjectAssetSource::Game, virtualDirectory, locked);
		if (importedFile.success || importedFile.message.empty() || std::filesystem::exists(assets.GetPath() / "locked.txt")) {
			std::cerr << "Project file import failure cleanup failed\n";
			return false;
		}
	}
	const auto importedFile =
		ProjectAssetCopyUtility::ImportExternalFile(ProjectAssetSource::Game, virtualDirectory, locked);
	if (!importedFile.success || !std::filesystem::is_regular_file(importedFile.fullPath)) {
		std::cerr << "Project file import retry failed\n";
		return false;
	}
	std::ifstream importedBytes(importedFile.fullPath);
	const std::string contents((std::istreambuf_iterator<char>(importedBytes)), std::istreambuf_iterator<char>());
	if (contents != "external") {
		std::cerr << "Project file import bytes differ\n";
		return false;
	}
	importedBytes.close();
	const auto trailingImport =
		ProjectAssetCopyUtility::ImportExternalFile(ProjectAssetSource::Game, virtualDirectory + "/", locked);
	if (!trailingImport.success || !std::filesystem::is_regular_file(trailingImport.fullPath)) {
		std::cerr << "Project file import trailing separator failed\n";
		return false;
	}

	// 失敗後の再取込は全ファイルを公開する
	const auto imported =
		ProjectAssetCopyUtility::ImportExternalDirectory(ProjectAssetSource::Game, virtualDirectory, externalDirectory);
	if (!imported.success || !std::filesystem::exists(imported.fullPath / "locked.txt") ||
		std::filesystem::exists(imported.fullPath / stagingName)) {
		std::cerr << "Project folder import retry failed: " << imported.message << '\n';
		return false;
	}
	// フォルダー複製でも準備中のファイルを含めない
	std::filesystem::create_directory(imported.fullPath / stagingName);
	std::ofstream(imported.fullPath / stagingName / "pending.txt") << "pending";
	const auto copiedDirectory = ProjectAssetCopyUtility::DuplicateDirectory(ProjectAssetSource::Game,
		virtualDirectory + "/" + imported.fullPath.filename().string(), {});
	if (!copiedDirectory.success || !std::filesystem::exists(copiedDirectory.fullPath / "locked.txt") ||
		std::filesystem::exists(copiedDirectory.fullPath / stagingName)) {
		std::cerr << "Project folder copy staging isolation failed\n";
		return false;
	}
	// 自身の子孫への取込で走査範囲を増やさない
	const auto recursive =
		ProjectAssetCopyUtility::ImportExternalDirectory(ProjectAssetSource::Game, virtualDirectory, assets.GetPath());
	if (recursive.success || recursive.message.empty()) {
		std::cerr << "Project recursive import rejection failed\n";
	}
	return !recursive.success && !recursive.message.empty();
}
