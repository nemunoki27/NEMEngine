#include "TestContracts.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Assets/Project/ProjectAssetMoveTransaction.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>

// c++
#include <stdexcept>

bool NEMTests::TestProjectAssetMoveTransaction() {

	using namespace Engine;
	TestDirectory directory("ProjectAssetMoveTransaction");
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
