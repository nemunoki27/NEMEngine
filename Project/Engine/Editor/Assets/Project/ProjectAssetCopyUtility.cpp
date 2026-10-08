#include "ProjectAssetCopyUtility.h"
#include "ProjectAssetCopyTransaction.h"
#include "ProjectDirectoryCopyTransaction.h"
#include "ProjectAssetPath.h"
#include "ProjectAssetDocumentPatch.h"
#include "ProjectModelImportPlan.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/World/Scene/Runtime/SceneSystem.h>
#include <Engine/Core/World/Scene/Serialization/SceneAssetStorage.h>
#include <Engine/Core/Assets/Utility/AssetTypeResolver.h>
#include <Engine/Core/Assets/Database/AssetFileUtility.h>
#include <Engine/Core/Rendering/Meshes/Import/ModelDocumentReferences.h>

// c++
#include <system_error>
#include <vector>

namespace {

	// Assetと付随ファイルを全て準備してから公開する
	bool CopyAssetFiles(const Engine::ProjectAssetEntry& asset, const std::filesystem::path& source,
		const std::filesystem::path& target, std::string& diagnostic) {

		Engine::ProjectAssetCopyTransaction transaction(target.parent_path());
		const bool model = Engine::ModelDocumentReferences::IsDocumentPath(source);
		if (!transaction.Add(source, target)) {
			diagnostic = "Assetのコピー計画を作成できません";
			return false;
		}

		// 同じstemへ付随ファイルの拡張子を引き継ぐ
		std::error_code error;
		for (const auto& sidecar : asset.sidecarFiles) {
			// モデルの参照ファイルは複製元と共有する
			if (model) {
				break;
			}
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

		// コピー先から共有先への相対参照を準備する
		if (model && !transaction.Prepare(
						 0,
						 [&](const auto&, std::string& bytes) {
							 return Engine::ModelDocumentReferences::Rebase(source, target, bytes, diagnostic);
						 },
						 diagnostic)) {
			return false;
		}

		// 公開前のAssetへ表示名を適用する
		if (Engine::AssetTypeResolver::IsJsonAssetFile(asset.type, target) &&
			!transaction.Prepare(
				0,
				[&](const auto& path, std::string& bytes) {
					return Engine::ProjectAssetDocumentPatch::PrepareJsonAssetName(path, asset.type, bytes);
				},
				diagnostic)) {
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
	ProjectDirectoryCopyTransaction transaction(targetPath);
	if (!transaction.Begin(result.message)) {
		return result;
	}
	// 失敗したコピーの所有は操作側へ残す
	const auto fail = [&](const char* message) {
		result.message = message;
		return result;
	};
	std::vector<SceneAssetCopy> sceneCopies;
	try {

		// シーン本体は後でActorと一括複製する
		for (auto iterator = std::filesystem::recursive_directory_iterator(sourcePath);
			iterator != std::filesystem::recursive_directory_iterator{}; ++iterator) {

			const auto& entry = *iterator;
			// 別のコピー操作が準備しているファイルを含めない
			if (entry.is_directory() && (AssetFileUtility::IsAssetCopyStagingDirectory(entry.path()) ||
											AssetFileUtility::IsExternalActorsDirectory(entry.path()))) {
				iterator.disable_recursion_pending();
				continue;
			}

			const std::filesystem::path relative = std::filesystem::relative(entry.path(), sourcePath);
			if (!ProjectAssetPath::IsSafeRelativePath(relative)) {

				return fail("複製元フォルダーに不正な相対パスがあります");
			}
			const std::filesystem::path destination = targetPath / relative;
			if (entry.is_directory()) {

				if (!transaction.AddDirectory(relative, result.message)) {
					return result;
				}
			} else if (entry.is_regular_file() && !ProjectAssetDocumentPatch::ShouldSkipCopyFile(entry.path())) {

				if (AssetTypeResolver::GuessByPath(entry.path()) == AssetType::Scene) {

					if (!transaction.AddDirectory(relative.parent_path(), result.message)) {
						return result;
					}
					sceneCopies.push_back({entry.path(), destination});
					continue;
				}
				const AssetType type = AssetTypeResolver::GuessByPath(entry.path());
				AssetCopyPreparation prepare;
				if (AssetTypeResolver::IsJsonAssetFile(type, entry.path())) {
					prepare = [type](const std::filesystem::path& staged, std::string& bytes) {
						return ProjectAssetDocumentPatch::PrepareJsonAssetName(staged, type, bytes);
					};
				}
				if (!transaction.StageFile(entry.path(), relative, result.message, prepare)) {
					return result;
				}
			}
		}
		if (!transaction.Publish(result.message)) {
			return result;
		}
	} catch (const std::exception&) {

		return fail("フォルダーの内容を複製できません");
	}
	// Actorはフォルダー外に保存されるため、失敗時の取り消しもシーン側でまとめて行う
	std::string sceneError;
	if (!SceneSystem::CopySceneAssets(sceneCopies, sceneError, storage)) {

		return fail(sceneError.c_str());
	}
	transaction.Commit();
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
	if (ModelDocumentReferences::IsDocumentPath(externalFilePath)) {
		try {
			// 本体と参照ファイルをモデル名の専用フォルダーへ揃える
			const auto root = ProjectAssetPath::MakeUniquePath(targetDirectory / externalFilePath.stem());
			if (root.empty()) {
				result.message = "モデルの取り込み先フォルダーを決定できません";
				return result;
			}
			ProjectModelImportPlan plan;
			if (!plan.Prepare(externalFilePath, result.message)) {
				return result;
			}
			ProjectDirectoryCopyTransaction transaction(root);
			if (!transaction.Begin(result.message) || !plan.Stage(transaction, result.message) ||
				!transaction.Publish(result.message) || !plan.Verify(root, result.message)) {
				return result;
			}
			transaction.Commit();
			result.success = true;
			result.fullPath = root / plan.GetMainRelativePath();
			result.assetPath = ProjectAssetPath::ToAssetPath(result.fullPath);
			return result;
		} catch (const std::exception& error) {
			result.message = "モデルを取り込めません: " + std::string(error.what());
			return result;
		}
	}
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
