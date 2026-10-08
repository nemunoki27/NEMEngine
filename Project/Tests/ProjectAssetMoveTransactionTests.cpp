#include "TestContracts.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Assets/Project/ProjectAssetMoveTransaction.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>

// c++
#include <fstream>
#include <stdexcept>

bool NEMTests::TestProjectAssetMoveTransaction() {

	using namespace Engine;
	TestDirectory directory("ProjectAssetMoveTransaction");
	const auto editDocument = [](std::string& bytes) {
		bytes = "edited";
		return true;
	};
	// 編集後の文書も元の所有と内容へ戻す
	for (int mode = 0; mode < 4; ++mode) {
		const auto original = directory.GetPath() / ("document" + std::to_string(mode) + ".json");
		const auto relocated = directory.GetPath() / ("relocated" + std::to_string(mode) + ".json");
		if (!StorageFileUtility::WriteBytes(original, "original")) {
			return false;
		}
		StorageFileUtility::FileIdentity identity;
		std::error_code error;
		if (!StorageFileUtility::ReadIdentity(original, identity, error)) {
			return false;
		}
		{
			ProjectAssetMoveTransaction transaction;
			std::string diagnostic;
			if (!transaction.Add(original, relocated) || !transaction.Prepare(0, editDocument, diagnostic) ||
				!transaction.Execute(diagnostic) ||
				StorageFileUtility::ReadVerifiedBytes(relocated, StorageFileUtility::FileRevision(relocated)) != "edited") {
				return false;
			}
			if (mode == 1) {
				// 同じbyteの外部差替えを復元時に削除しない
				std::filesystem::rename(relocated, directory.GetPath() / "foreignRetained.json");
				if (!StorageFileUtility::WriteBytes(relocated, "edited") || transaction.Rollback() ||
					!std::filesystem::exists(relocated) || std::filesystem::exists(original)) {
					return false;
				}
				std::filesystem::remove(relocated);
				if (!transaction.Rollback()) {
					return false;
				}
			} else if (mode == 2) {
				// 復元元に新しい文書があれば上書きしない
				if (!StorageFileUtility::WriteBytes(original, "foreign") || transaction.Rollback() ||
					StorageFileUtility::ReadVerifiedBytes(original, StorageFileUtility::FileRevision(original)) != "foreign") {
					return false;
				}
				std::filesystem::remove(original);
				if (!transaction.Rollback()) {
					return false;
				}
			} else if (mode == 3) {
				transaction.Commit();
			}
		}
		if (mode == 3) {
			if (std::filesystem::exists(original) || !std::filesystem::exists(relocated)) {
				return false;
			}
		} else {
			StorageFileUtility::FileIdentity restored;
			if (!StorageFileUtility::ReadIdentity(original, restored, error) || restored != identity ||
				StorageFileUtility::ReadVerifiedBytes(original, StorageFileUtility::FileRevision(original)) != "original" ||
				std::filesystem::exists(relocated)) {
				return false;
			}
		}
	}
	// 編集準備後の外部差替えを移動しない
	const auto reentrantSource = directory.GetPath() / "reentrantSource.json";
	const auto reentrantTarget = directory.GetPath() / "reentrantTarget.json";
	if (!StorageFileUtility::WriteBytes(reentrantSource, "original")) {
		return false;
	}
	{
		ProjectAssetMoveTransaction transaction;
		std::string diagnostic;
		bool refused = false;
		if (!transaction.Add(reentrantSource, reentrantTarget) ||
			!transaction.Prepare(
				0,
				[&](std::string& bytes) {
					std::string nestedDiagnostic;
					refused = !transaction.Add(directory.GetPath() / "extra", directory.GetPath() / "extraTarget") &&
							  !transaction.Execute(nestedDiagnostic) && !transaction.Rollback() &&
							  !transaction.Prepare(0, [](std::string&) { return true; }, nestedDiagnostic);
					transaction.Commit();
					bytes = "edited";
					return true;
				},
				diagnostic) ||
			!refused || !transaction.Execute(diagnostic)) {
			return false;
		}
	}
	if (!std::filesystem::exists(reentrantSource) || std::filesystem::exists(reentrantTarget)) {
		return false;
	}
	// 編集準備後の同内容の差替えも拒否する
	const auto changedSource = directory.GetPath() / "changedSource.json";
	const auto changedTarget = directory.GetPath() / "changedTarget.json";
	if (!StorageFileUtility::WriteBytes(changedSource, "original")) {
		return false;
	}
	{
		ProjectAssetMoveTransaction transaction;
		std::string diagnostic;
		if (!transaction.Add(changedSource, changedTarget) || !transaction.Prepare(0, editDocument, diagnostic)) {
			return false;
		}
		std::filesystem::rename(changedSource, directory.GetPath() / "sourceRetained.json");
		if (!StorageFileUtility::WriteBytes(changedSource, "original") || transaction.Execute(diagnostic) ||
			std::filesystem::exists(changedTarget)) {
			return false;
		}
	}
	const auto source = directory.GetPath() / "source.bin";
	const auto target = directory.GetPath() / "target.bin";
	const auto sidecar = directory.GetPath() / "source.meta";
	const auto sidecarTarget = directory.GetPath() / "target.meta";
	const std::string sourceBytes("a\0bc", 4);
	if (!StorageFileUtility::WriteBytes(source, sourceBytes) || !StorageFileUtility::WriteBytes(sidecar, "meta")) {
		return false;
	}
	const auto sourceRevision = StorageFileUtility::FileRevision(source);
	const auto sidecarRevision = StorageFileUtility::FileRevision(sidecar);
	// 照合と削除の間も外部の書込を防ぐ
	const auto guarded = directory.GetPath() / "guarded.bin";
	if (!StorageFileUtility::WriteBytes(guarded, sourceBytes)) {
		return false;
	}
	std::error_code removeError;
	if (StorageFileUtility::RemoveIfRevision(guarded, "wrong", removeError) || !removeError ||
		StorageFileUtility::FileRevision(guarded) != sourceRevision) {
		return false;
	}
	{
		TestFileReadLock lock(guarded);
		if (StorageFileUtility::RemoveIfRevision(guarded, sourceRevision, removeError) || !removeError) {
			return false;
		}
	}
	if (!StorageFileUtility::RemoveIfRevision(guarded, sourceRevision, removeError) || removeError ||
		std::filesystem::exists(guarded)) {
		return false;
	}

	// 同じbyteへ差し替わったファイルも所有情報で保護する
	const auto ownedGuarded = directory.GetPath() / "ownedGuarded.bin";
	StorageFileUtility::FileIdentity guardedIdentity;
	if (!StorageFileUtility::WriteBytes(guarded, sourceBytes) ||
		!StorageFileUtility::ReadIdentity(guarded, guardedIdentity, removeError)) {
		return false;
	}
	std::filesystem::rename(guarded, ownedGuarded);
	if (!StorageFileUtility::WriteBytes(guarded, sourceBytes) ||
		StorageFileUtility::RemoveIfRevision(guarded, sourceRevision, guardedIdentity, removeError) || !removeError ||
		!std::filesystem::is_regular_file(guarded) || !std::filesystem::is_regular_file(ownedGuarded)) {
		return false;
	}
	if (!StorageFileUtility::ReadIdentity(guarded, guardedIdentity, removeError) ||
		!StorageFileUtility::RemoveIfRevision(guarded, sourceRevision, guardedIdentity, removeError) || removeError) {
		return false;
	}
	// 同じ識別情報でも内容が変わったファイルは公開しない
	const auto revisionSource = directory.GetPath() / "revisionSource.bin";
	const auto revisionTarget = directory.GetPath() / "revisionTarget.bin";
	StorageFileUtility::FileIdentity revisionIdentity;
	if (!StorageFileUtility::WriteBytes(revisionSource, sourceBytes) ||
		!StorageFileUtility::ReadIdentity(revisionSource, revisionIdentity, removeError)) {
		return false;
	}
	{
		std::ofstream file(revisionSource, std::ios::binary | std::ios::trunc);
		file << "changed";
	}
	if (StorageFileUtility::MoveIfRevision(revisionSource, revisionTarget, sourceRevision, revisionIdentity, removeError) ||
		!removeError || !std::filesystem::exists(revisionSource) || std::filesystem::exists(revisionTarget)) {
		return false;
	}
	const auto changedRevision = StorageFileUtility::FileRevision(revisionSource);
	{
		TestFileReadLock lock(revisionSource);
		if (StorageFileUtility::MoveIfRevision(
				revisionSource, revisionTarget, changedRevision, revisionIdentity, removeError) ||
			!removeError) {
			return false;
		}
	}
	if (!StorageFileUtility::WriteBytes(revisionTarget, "existing") ||
		StorageFileUtility::MoveIfRevision(revisionSource, revisionTarget, changedRevision, revisionIdentity, removeError) ||
		!removeError || StorageFileUtility::FileRevision(revisionSource) != changedRevision) {
		return false;
	}
	std::filesystem::remove(revisionTarget);
	if (!StorageFileUtility::MoveIfRevision(revisionSource, revisionTarget, changedRevision, revisionIdentity, removeError) ||
		removeError || std::filesystem::exists(revisionSource) ||
		StorageFileUtility::FileRevision(revisionTarget) != changedRevision) {
		return false;
	}
	// 所有するフォルダーでも内容が残る間は削除しない
	const auto ownedDirectory = directory.GetPath() / "ownedDirectory";
	std::filesystem::create_directory(ownedDirectory);
	StorageFileUtility::FileIdentity directoryIdentity;
	if (!StorageFileUtility::ReadIdentity(ownedDirectory, directoryIdentity, removeError) ||
		!StorageFileUtility::WriteBytes(ownedDirectory / "retained.bin", sourceBytes) ||
		StorageFileUtility::RemoveIfIdentity(ownedDirectory, directoryIdentity, removeError) || !removeError) {
		return false;
	}
	std::filesystem::remove(ownedDirectory / "retained.bin");
	if (!StorageFileUtility::RemoveIfIdentity(ownedDirectory, directoryIdentity, removeError) || removeError ||
		std::filesystem::exists(ownedDirectory)) {
		return false;
	}
	// 付随ファイルの衝突では本体も移動しない
	{
		ProjectAssetMoveTransaction transaction;
		std::string diagnostic;
		if (!transaction.Add(source, target) || transaction.Add(source, sidecarTarget) ||
			transaction.Add(target, sidecarTarget) || !transaction.Add(sidecar, sidecarTarget) ||
			!StorageFileUtility::WriteBytes(sidecarTarget, "existing") || transaction.Execute(diagnostic) ||
			diagnostic.empty()) {
			return false;
		}
	}
	if (std::filesystem::exists(target) || StorageFileUtility::FileRevision(source) != sourceRevision ||
		StorageFileUtility::FileRevision(sidecar) != sidecarRevision) {
		return false;
	}
	std::filesystem::remove(sidecarTarget);

	// 先頭の移動後に失敗した場合は全てを元へ戻す
	{
		TestFileReadLock lock(sidecar);
		ProjectAssetMoveTransaction transaction;
		std::string diagnostic;
		if (!transaction.Add(source, target) || !transaction.Add(sidecar, sidecarTarget) || transaction.Execute(diagnostic) ||
			diagnostic.empty() || !std::filesystem::exists(target) || !transaction.Rollback()) {
			return false;
		}
	}
	if (std::filesystem::exists(target) || std::filesystem::exists(sidecarTarget) ||
		StorageFileUtility::FileRevision(source) != sourceRevision ||
		StorageFileUtility::FileRevision(sidecar) != sidecarRevision) {
		return false;
	}

	// 移動後の処理から例外で抜けても元へ戻す
	try {
		ProjectAssetMoveTransaction transaction;
		std::string diagnostic;
		if (!transaction.Add(source, target) || !transaction.Execute(diagnostic) || transaction.Add(sidecar, sidecarTarget) ||
			transaction.Execute(diagnostic)) {
			return false;
		}
		throw std::runtime_error("move transaction test");
	} catch (const std::runtime_error&) {
		if (std::filesystem::exists(target) || StorageFileUtility::FileRevision(source) != sourceRevision) {
			return false;
		}
	}

	// 復元先に新しいファイルがあれば双方を保持する
	{
		ProjectAssetMoveTransaction transaction;
		std::string diagnostic;
		if (!transaction.Add(source, target) || !transaction.Execute(diagnostic) ||
			!StorageFileUtility::WriteBytes(source, "new source") || transaction.Rollback() ||
			StorageFileUtility::FileRevision(target) != sourceRevision) {
			return false;
		}
	}
	std::error_code error;
	// 共通の移動処理でも既存の移動先を置き換えない
	if (StorageFileUtility::MoveWithoutReplacement(target, source, error) || !error ||
		StorageFileUtility::FileRevision(target) != sourceRevision ||
		StorageFileUtility::FileRevision(source) == sourceRevision) {
		return false;
	}
	std::filesystem::remove(source);
	if (!StorageFileUtility::MoveWithoutReplacement(target, source, error) || error) {
		return false;
	}

	// 復元の失敗後も未復元の履歴を保持する
	{
		ProjectAssetMoveTransaction transaction;
		std::string diagnostic;
		if (!transaction.Add(source, target) || !transaction.Add(sidecar, sidecarTarget) || !transaction.Execute(diagnostic) ||
			!StorageFileUtility::WriteBytes(source, "external") || transaction.Rollback() ||
			std::filesystem::exists(sidecarTarget) || StorageFileUtility::FileRevision(sidecar) != sidecarRevision) {
			return false;
		}
		transaction.Commit();
		std::filesystem::remove(source);
		if (!transaction.Rollback() || std::filesystem::exists(target) ||
			StorageFileUtility::FileRevision(source) != sourceRevision) {
			return false;
		}
	}

	// 移動先が別ファイルへ置き換わっても触れない
	{
		const auto retained = directory.GetPath() / "retained.bin";
		ProjectAssetMoveTransaction transaction;
		std::string diagnostic;
		if (!transaction.Add(source, target) || !transaction.Execute(diagnostic) ||
			!StorageFileUtility::MoveWithoutReplacement(target, retained, error) ||
			!StorageFileUtility::WriteBytes(target, "foreign") || transaction.Rollback() || std::filesystem::exists(source) ||
			StorageFileUtility::FileRevision(retained) != sourceRevision ||
			StorageFileUtility::FileRevision(target) == sourceRevision) {
			return false;
		}
		std::filesystem::remove(target);
		if (!StorageFileUtility::MoveWithoutReplacement(retained, target, error) || !transaction.Rollback() ||
			StorageFileUtility::FileRevision(source) != sourceRevision) {
			return false;
		}
	}
	// フォルダーも別の対象へ置き換わった場合は復元しない
	{
		const auto sourceFolder = directory.GetPath() / "folder";
		const auto targetFolder = directory.GetPath() / "moved";
		const auto retainedFolder = directory.GetPath() / "retained";
		std::filesystem::create_directory(sourceFolder);
		if (!StorageFileUtility::WriteBytes(sourceFolder / "owned.bin", sourceBytes)) {
			return false;
		}
		ProjectAssetMoveTransaction transaction;
		std::string diagnostic;
		if (!transaction.Add(sourceFolder, targetFolder) || !transaction.Execute(diagnostic) ||
			!StorageFileUtility::MoveWithoutReplacement(targetFolder, retainedFolder, error)) {
			return false;
		}
		std::filesystem::create_directory(targetFolder);
		if (!StorageFileUtility::WriteBytes(targetFolder / "foreign.bin", "foreign") || transaction.Rollback() ||
			std::filesystem::exists(sourceFolder) || !std::filesystem::exists(targetFolder / "foreign.bin")) {
			return false;
		}
		std::filesystem::remove(targetFolder / "foreign.bin");
		std::filesystem::remove(targetFolder);
		if (!StorageFileUtility::MoveWithoutReplacement(retainedFolder, targetFolder, error) || !transaction.Rollback() ||
			StorageFileUtility::FileRevision(sourceFolder / "owned.bin") != sourceRevision) {
			return false;
		}
	}
	// 確定した移動は終了時に取り消さない
	{
		ProjectAssetMoveTransaction transaction;
		std::string diagnostic;
		if (!transaction.Add(source, target) || !transaction.Add(sidecar, sidecarTarget) || !transaction.Execute(diagnostic)) {
			return false;
		}
		transaction.Commit();
		if (transaction.Rollback()) {
			return false;
		}
	}
	return !std::filesystem::exists(source) && !std::filesystem::exists(sidecar) &&
		   StorageFileUtility::FileRevision(target) == sourceRevision &&
		   StorageFileUtility::FileRevision(sidecarTarget) == sidecarRevision;
}
