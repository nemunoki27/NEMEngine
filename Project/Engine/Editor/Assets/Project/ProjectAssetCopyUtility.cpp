#include "ProjectAssetCopyUtility.h"
#include "ProjectAssetCopyTransaction.h"
#include "ProjectAssetPath.h"
#include "ProjectAssetDocumentPatch.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/World/Scene/Runtime/SceneSystem.h>
#include <Engine/Core/World/Scene/Serialization/SceneAssetStorage.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Assets/Utility/AssetTypeResolver.h>
#include <Engine/Core/Assets/Database/AssetFileUtility.h>

// c++
#include <system_error>
#include <vector>

namespace {

	// この操作で作成したフォルダーだけを片付ける
	void RemoveFailedCopyDirectory(const std::filesystem::path& directory) {

		std::error_code error;
		std::filesystem::remove_all(directory, error);
		if (error) {
			Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::err,
				"Assetコピー途中のフォルダーを削除できません path={} 詳細={}", Engine::Algorithm::PathToUTF8(directory),
				error.message());
		}
	}

	// Assetと付随ファイルを全て準備してから公開する
	bool CopyAssetFiles(const Engine::ProjectAssetEntry& asset, const std::filesystem::path& source,
		const std::filesystem::path& target, std::string& diagnostic) {

		Engine::ProjectAssetCopyTransaction transaction(target.parent_path());
		if (!transaction.Add(source, target)) {
			diagnostic = "Assetのコピー計画を作成できません";
			return false;
		}

		// 同じstemへ付随ファイルの拡張子を引き継ぐ
		std::error_code error;
		for (const auto& sidecar : asset.sidecarFiles) {
			const auto sidecarSource = source.parent_path() / Engine::Algorithm::PathFromUTF8(sidecar);
			const bool exists = std::filesystem::exists(sidecarSource, error);
			if (error) {
				diagnostic = "付随ファイルを確認できません";
				return false;
			}
			if (!exists) {
				continue;
			}
			const auto sidecarTarget = Engine::ProjectAssetPath::MakeSiblingPath(target, sidecarSource.extension());
			if (!transaction.Add(sidecarSource, sidecarTarget)) {
				diagnostic = "付随ファイルのコピー先が重複しています";
				return false;
			}
		}
		if (!transaction.Stage(diagnostic)) {
			return false;
		}

		// 公開前のAssetへ表示名を適用する
		if (!Engine::ProjectAssetDocumentPatch::PatchDuplicatedJsonAsset(transaction.GetStagedPath(0), asset.type)) {
			diagnostic = "コピーしたAssetの表示名を保存できません";
			return false;
		}
		if (!transaction.Publish(diagnostic)) {
			return false;
		}
		transaction.Commit();
		return true;
	}
}

//============================================================================
//	ProjectAssetCopyUtility classMethods
//============================================================================
Engine::ProjectAssetFileResult Engine::ProjectAssetCopyUtility::DuplicateAsset(
	const ProjectAssetEntry& asset, const std::shared_ptr<SceneAssetStorage>& storage) {

	ProjectAssetFileResult result{};

	// 元のアセットパスを解決し存在しなければ中断
	const std::filesystem::path sourcePath = RuntimePaths::ResolveAssetPath(asset.assetPath);
	if (sourcePath.empty() || !std::filesystem::exists(sourcePath)) {
		result.message = "複製元のアセットが見つかりません";
		return result;
	}

	// 複製先のパスを既存アセットとの競合回避で決定する
	const std::filesystem::path targetPath = ProjectAssetPath::MakeUniquePath(sourcePath);
	if (targetPath.empty()) {
		result.message = "アセットの複製先を決定できません";
		return result;
	}

	// シーンは外部Actorと内部参照も合わせて複製する
	if (asset.type == AssetType::Scene) {

		result.success = SceneSystem::CopySceneAssets({{sourcePath, targetPath}}, result.message, storage);
		result.fullPath = targetPath;
		result.assetPath = ProjectAssetPath::ToAssetPath(targetPath);
		return result;
	}
	// 付随ファイルの失敗も操作結果へ返す
	if (!CopyAssetFiles(asset, sourcePath, targetPath, result.message)) {
		return result;
	}

	result.success = true;
	result.fullPath = targetPath;
	result.assetPath = ProjectAssetPath::ToAssetPath(targetPath);
	return result;
}

Engine::ProjectAssetFileResult Engine::ProjectAssetCopyUtility::CopyAsset(const ProjectAssetEntry& asset,
	ProjectAssetSource targetSource, const std::string& targetDirectoryVirtualPath,
	const std::shared_ptr<SceneAssetStorage>& storage) {

	ProjectAssetFileResult result{};

	// 元のアセットパスとコピー先ディレクトリを解決する
	const std::filesystem::path sourcePath = RuntimePaths::ResolveAssetPath(asset.assetPath);
	const std::filesystem::path targetDirectory =
		ProjectAssetPath::ResolveVirtualDirectory(targetSource, targetDirectoryVirtualPath);
	if (sourcePath.empty() || !std::filesystem::exists(sourcePath) || targetDirectory.empty()) {
		result.message = "コピー元のアセットまたはコピー先フォルダーが見つかりません";
		return result;
	}

	// コピー先ディレクトリを確保する
	std::error_code ec;
	std::filesystem::create_directories(targetDirectory, ec);
	if (ec) {
		result.message = "コピー先フォルダーを作成できません";
		return result;
	}

	// コピー先のパスを既存アセットとの競合回避で決定する
	const std::filesystem::path targetPath = ProjectAssetPath::MakeUniquePath(targetDirectory / sourcePath.filename());
	if (targetPath.empty()) {
		result.message = "アセットのコピー先を決定できません";
		return result;
	}

	// コピペでも単体複製と同じシーン保存処理を使う
	if (asset.type == AssetType::Scene) {

		result.success = SceneSystem::CopySceneAssets({{sourcePath, targetPath}}, result.message, storage);
		result.fullPath = targetPath;
		result.assetPath = ProjectAssetPath::ToAssetPath(targetPath);
		return result;
	}
	// 付随ファイルの失敗も操作結果へ返す
	if (!CopyAssetFiles(asset, sourcePath, targetPath, result.message)) {
		return result;
	}

	result.success = true;
	result.fullPath = targetPath;
	result.assetPath = ProjectAssetPath::ToAssetPath(targetPath);
	return result;
}

Engine::ProjectAssetFileResult Engine::ProjectAssetCopyUtility::DuplicateDirectory(
	ProjectAssetSource source, const std::string& directoryVirtualPath, const std::shared_ptr<SceneAssetStorage>& storage) {

	ProjectAssetFileResult result{};
	result.isDirectory = true;
	const std::filesystem::path sourcePath = ProjectAssetPath::ResolveVirtualDirectory(source, directoryVirtualPath);
	if (sourcePath.empty() || sourcePath == ProjectAssetPath::GetSourceRoot(source) ||
		!std::filesystem::is_directory(sourcePath)) {

		result.message = "複製元フォルダーが存在しないかルートフォルダーです";
		return result;
	}
	const std::filesystem::path targetPath = ProjectAssetPath::MakeUniquePath(sourcePath);
	if (targetPath.empty()) {

		result.message = "フォルダーの複製先を決定できません";
		return result;
	}
	std::error_code ec;
	if (!std::filesystem::create_directory(targetPath, ec) || ec) {

		result.message = "複製先フォルダーを作成できません";
		return result;
	}
	// 新規作成した複製先だけを取り消す
	const auto fail = [&](const char* message) {
		result.message = message;
		RemoveFailedCopyDirectory(targetPath);
		return result;
	};
	std::vector<SceneAssetCopy> sceneCopies;
	try {

		// シーン本体は後でActorと一括複製し、その他のファイルは従来どおりコピーする
		for (auto iterator = std::filesystem::recursive_directory_iterator(sourcePath);
			iterator != std::filesystem::recursive_directory_iterator{}; ++iterator) {

			const auto& entry = *iterator;
			// 別のコピー操作が準備しているファイルを含めない
			if (entry.is_directory() && AssetFileUtility::IsAssetCopyStagingDirectory(entry.path())) {
				iterator.disable_recursion_pending();
				continue;
			}

			const std::filesystem::path relative = std::filesystem::relative(entry.path(), sourcePath);
			if (!ProjectAssetPath::IsSafeRelativePath(relative)) {

				return fail("複製元フォルダーに不正な相対パスがあります");
			}
			const std::filesystem::path destination = targetPath / relative;
			if (entry.is_directory()) {

				std::filesystem::create_directories(destination);
			} else if (entry.is_regular_file() && !ProjectAssetDocumentPatch::ShouldSkipCopyFile(entry.path())) {

				if (AssetTypeResolver::GuessByPath(entry.path()) == AssetType::Scene) {

					sceneCopies.push_back({entry.path(), destination});
					continue;
				}
				std::filesystem::copy_file(entry.path(), destination);
			}
		}
		if (!ProjectAssetDocumentPatch::PatchDuplicatedDirectoryAssets(targetPath)) {
			return fail("複製したAssetの表示名を保存できません");
		}
	} catch (const std::exception&) {

		return fail("フォルダーの内容を複製できません");
	}
	// Actorはフォルダー外に保存されるため、失敗時の取り消しもシーン側でまとめて行う
	std::string sceneError;
	if (!SceneSystem::CopySceneAssets(sceneCopies, sceneError, storage)) {

		return fail(sceneError.c_str());
	}
	result.success = true;
	result.fullPath = targetPath;
	result.assetPath = ProjectAssetPath::ToAssetPath(targetPath);
	return result;
}

Engine::ProjectAssetFileResult Engine::ProjectAssetCopyUtility::ImportExternalFile(ProjectAssetSource targetSource,
	const std::string& targetDirectoryVirtualPath, const std::filesystem::path& externalFilePath) {

	ProjectAssetFileResult result{};

	std::error_code ec;
	// ディレクトリや存在しないものは取り込まない
	if (externalFilePath.empty() || !std::filesystem::is_regular_file(externalFilePath, ec) || ec) {
		result.message = "取り込み元のファイルが見つかりません";
		return result;
	}

	// 取り込み先ディレクトリを解決して確保する
	const std::filesystem::path targetDirectory =
		ProjectAssetPath::ResolveVirtualDirectory(targetSource, targetDirectoryVirtualPath);
	if (targetDirectory.empty()) {
		result.message = "取り込み先フォルダーが見つかりません";
		return result;
	}
	std::filesystem::create_directories(targetDirectory, ec);
	if (ec) {
		result.message = "取り込み先フォルダーを作成できません";
		return result;
	}

	// 既存Assetと競合しない公開先を選ぶ
	const std::filesystem::path targetPath = ProjectAssetPath::MakeUniquePath(targetDirectory / externalFilePath.filename());
	if (targetPath.empty()) {
		result.message = "取り込み先のファイル名を決定できません";
		return result;
	}
	// コピー失敗時のファイルを公開しない
	ProjectAssetCopyTransaction transaction(targetDirectory);
	if (!transaction.Add(externalFilePath, targetPath)) {
		result.message = "ファイルの取り込み計画を作成できません";
		return result;
	}
	if (!transaction.Stage(result.message) || !transaction.Publish(result.message)) {
		return result;
	}
	transaction.Commit();

	result.success = true;
	result.fullPath = targetPath;
	result.assetPath = ProjectAssetPath::ToAssetPath(targetPath);
	return result;
}

Engine::ProjectAssetFileResult Engine::ProjectAssetCopyUtility::ImportExternalDirectory(ProjectAssetSource targetSource,
	const std::string& targetDirectoryVirtualPath, const std::filesystem::path& externalDirectoryPath) {

	ProjectAssetFileResult result{};
	result.isDirectory = true;

	std::error_code ec;
	// フォルダ以外や存在しないものは取り込まない
	if (externalDirectoryPath.empty() || !std::filesystem::is_directory(externalDirectoryPath, ec)) {
		result.message = "Dropped path is not a folder.";
		return result;
	}

	// 取り込み先ディレクトリを解決して確保する
	const std::filesystem::path targetDirectory =
		ProjectAssetPath::ResolveVirtualDirectory(targetSource, targetDirectoryVirtualPath);
	if (targetDirectory.empty()) {
		result.message = "Target folder was not found.";
		return result;
	}
	std::filesystem::create_directories(targetDirectory, ec);
	if (ec) {
		result.message = "Failed to create target folder.";
		return result;
	}

	// ドロップしたフォルダ名で取り込み先に新フォルダを作る、既存と衝突したら連番にする
	const std::filesystem::path destinationRoot =
		ProjectAssetPath::MakeUniquePath(targetDirectory / externalDirectoryPath.filename());
	if (destinationRoot.empty()) {
		result.message = "Failed to build import folder path.";
		return result;
	}
	// 既存フォルダーを取り込み先として所有しない
	if (ProjectAssetPath::IsSameOrChildPath(destinationRoot, externalDirectoryPath)) {
		result.message = "取り込み元の内部へフォルダーを取り込めません";
		return result;
	}
	if (!std::filesystem::create_directory(destinationRoot, ec) || ec) {
		result.message = "Failed to create imported folder.";
		return result;
	}
	const auto fail = [&](const char* message) {
		result.message = message;
		RemoveFailedCopyDirectory(destinationRoot);
		return result;
	};

	// 中身を再帰的にコピーする、.meta等のサイドカーはRebuildで再発番させるためスキップする
	auto iterator = std::filesystem::recursive_directory_iterator(externalDirectoryPath, ec);
	const std::filesystem::recursive_directory_iterator end{};
	for (; iterator != end; iterator.increment(ec)) {
		if (ec) {
			return fail("取り込み元のフォルダーを走査できません");
		}
		const auto& entry = *iterator;

		const std::filesystem::path relative = std::filesystem::relative(entry.path(), externalDirectoryPath, ec);
		if (ec || !ProjectAssetPath::IsSafeRelativePath(relative)) {
			return fail("取り込み元の相対パスを解決できません");
		}

		const std::filesystem::path destination = destinationRoot / relative;
		const bool isDirectory = entry.is_directory(ec);
		if (ec) {
			return fail("取り込み元のファイル種別を確認できません");
		}
		if (isDirectory) {
			// 作業中のファイルを取込先へ公開しない
			if (AssetFileUtility::IsAssetCopyStagingDirectory(entry.path())) {
				iterator.disable_recursion_pending();
				continue;
			}
			std::filesystem::create_directories(destination, ec);
		} else if (entry.is_regular_file(ec) && !ProjectAssetDocumentPatch::ShouldSkipCopyFile(entry.path())) {
			std::filesystem::create_directories(destination.parent_path(), ec);
			if (!ec) {
				std::filesystem::copy_file(entry.path(), destination, std::filesystem::copy_options::none, ec);
			}
		}
		if (ec) {
			return fail("取り込み元のファイルをコピーできません");
		}
	}
	if (ec) {
		return fail("取り込み元のフォルダーを走査できません");
	}

	result.success = true;
	result.fullPath = destinationRoot;
	result.assetPath = ProjectAssetPath::ToAssetPath(destinationRoot);
	return result;
}
