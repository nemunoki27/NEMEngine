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
	// モデルごとの専用フォルダーと準備済み参照
	struct ModelEntry {

		std::filesystem::path relative;
		std::unique_ptr<Engine::ProjectModelImportPlan> plan;
	};

	// コピー元の名前と競合しないモデル専用フォルダーを選ぶ
	std::filesystem::path ReserveModelRoot(const std::filesystem::path& relative, std::set<std::string>& names) {

		const auto base = relative.parent_path() / relative.stem();
		for (uint32_t index = 0; index < 10000; ++index) {
			const auto candidate = index == 0 ? base : relative.parent_path() /
				Engine::Algorithm::PathFromUTF8(Engine::Algorithm::PathToUTF8(relative.stem()) + " " + std::to_string(index));
			if (names.insert(Engine::StorageFileUtility::PathKey(candidate)).second) {
				return candidate;
			}
		}
		throw std::runtime_error("モデルの取り込み先フォルダーを決定できません");
	}
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

		// 全ての元ファイル名を先に確保し、モデル用の名前と分ける
		std::vector<ImportEntry> entries;
		std::set<std::string> names;
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
			names.insert(StorageFileUtility::PathKey(relative));
			entries.push_back({entry.path(), relative, directory});
		}

		// フォルダー外の参照もモデルごとに取り込む
		std::vector<ModelEntry> models;
		ProjectDirectoryCopyTransaction transaction(root);
		if (!transaction.Begin(result.message)) {
			return result;
		}
		for (const auto& entry : entries) {
			if (entry.directory) {
				if (!transaction.AddDirectory(entry.relative, result.message)) {
					return result;
				}
			} else if (ModelDocumentReferences::IsDocumentPath(entry.source)) {
				ModelEntry model{ReserveModelRoot(entry.relative, names), std::make_unique<ProjectModelImportPlan>()};
				if (!model.plan->Prepare(entry.source, result.message) ||
					!model.plan->Stage(transaction, result.message, model.relative)) {
					return result;
				}
				models.push_back(std::move(model));
			} else if (!transaction.StageFile(entry.source, entry.relative, result.message)) {
				return result;
			}
		}
		if (!transaction.Publish(result.message)) {
			return result;
		}
		// 全モデルの解決結果が一致するまで所有を渡さない
		for (const auto& model : models) {
			if (!model.plan->Verify(root / model.relative, result.message)) {
				return result;
			}
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
