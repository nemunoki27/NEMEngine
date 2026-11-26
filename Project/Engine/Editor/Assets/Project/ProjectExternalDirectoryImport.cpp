#include "ProjectAssetCopyUtility.h"

//============================================================================
//	include
//============================================================================
#include "ProjectAssetDocumentPatch.h"
#include "ProjectAssetPath.h"
#include "ProjectDirectoryCopyTransaction.h"
#include "ProjectModelImportPlan.h"
#include <Engine/Core/Assets/Database/AssetFileUtility.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <Engine/Core/Rendering/Meshes/Import/ModelDocumentReferences.h>

// c++
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

namespace {

	// 走査時に確定したコピー元と相対位置
	struct ImportEntry {

		std::filesystem::path source;
		std::filesystem::path relative;
		bool directory = false;
	};

}

//============================================================================
//	ProjectAssetCopyUtility classMethods
//============================================================================
Engine::ProjectAssetFileResult Engine::ProjectAssetCopyUtility::ImportExternalDirectory(ProjectAssetSource targetSource,
	const std::string& targetDirectoryVirtualPath, const std::filesystem::path& externalDirectoryPath) {

	ProjectAssetFileResult result{};
	result.isDirectory = true;
	try {
		// 取り込み元と公開先を確定する
		if (externalDirectoryPath.empty() || !std::filesystem::is_directory(externalDirectoryPath)) {
			result.message = "取り込み元のフォルダーが見つかりません";
			return result;
		}
		const auto targetDirectory = ProjectAssetPath::ResolveVirtualDirectory(targetSource, targetDirectoryVirtualPath);
		if (targetDirectory.empty()) {
			result.message = "取り込み先フォルダーが見つかりません";
			return result;
		}
		std::filesystem::create_directories(targetDirectory);
		const auto root = ProjectAssetPath::MakeUniquePath(targetDirectory / externalDirectoryPath.filename());
		if (root.empty() || ProjectAssetPath::IsSameOrChildPath(root, externalDirectoryPath)) {
			result.message = "取り込み先が無効か取り込み元の内部です";
			return result;
		}

		// 元の階層を保って取り込む範囲を収集する
		std::vector<ImportEntry> entries;
		ProjectModelImportPlan::Layout layout{ .sourceRoot = externalDirectoryPath };
		for (auto iterator = std::filesystem::recursive_directory_iterator(externalDirectoryPath);
			iterator != std::filesystem::recursive_directory_iterator{}; ++iterator) {
			const auto& entry = *iterator;
			const bool directory = entry.is_directory();
			if (directory && AssetFileUtility::IsAssetCopyStagingDirectory(entry.path())) {
				iterator.disable_recursion_pending();
				continue;
			}
			if (!directory && (!entry.is_regular_file() || ProjectAssetDocumentPatch::ShouldSkipCopyFile(entry.path()))) {
				continue;
			}
			const auto relative = std::filesystem::relative(entry.path(), externalDirectoryPath);
			if (!ProjectAssetPath::IsSafeRelativePath(relative)) {
				result.message = "取り込み元の相対パスを解決できません";
				return result;
			}
			entries.push_back({entry.path(), relative, directory});
		}

		// 共有画像とMTLは各モデルの参照を準備してから一度だけコピーする
		std::set<std::string> staged;
		std::vector<std::unique_ptr<ProjectModelImportPlan>> models;
		ProjectDirectoryCopyTransaction transaction(root);
		if (!transaction.Begin(result.message)) return result;
		for (const auto& entry : entries) {
			if (entry.directory || !ModelDocumentReferences::IsDocumentPath(entry.source)) continue;
			auto model = std::make_unique<ProjectModelImportPlan>();
			if (!model->Prepare(entry.source, result.message, &layout) ||
				!model->Stage(transaction, result.message, &staged)) return result;
			models.push_back(std::move(model));
		}
		for (const auto& entry : entries) {
			if (entry.directory) {
				if (!transaction.AddDirectory(entry.relative, result.message)) return result;
			} else if (!staged.contains(StorageFileUtility::PathKey(entry.source)) &&
				!transaction.StageFile(entry.source, entry.relative, result.message)) return result;
		}
		if (!transaction.Publish(result.message)) return result;
		for (const auto& model : models) {
			if (!model->Verify(root, result.message)) return result;
		}
		transaction.Commit();
		result.success = true;
		result.fullPath = root;
		result.assetPath = ProjectAssetPath::ToAssetPath(root);
		return result;
	} catch (const std::exception& error) {
		result.message = "フォルダーを取り込めません: " + std::string(error.what());
		return result;
	}
}
