#include "AssetDocumentRecovery.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <array>
#include <exception>
#include <utility>

namespace {

	constexpr std::array kSaveKinds{Engine::AssetDocumentSaveKind::Material, Engine::AssetDocumentSaveKind::ShaderGraph,
		Engine::AssetDocumentSaveKind::GeneratedRender, Engine::AssetDocumentSaveKind::Prefab,
		Engine::AssetDocumentSaveKind::Font};
}

//============================================================================
//	AssetDocumentRecovery namespaceMethods
//============================================================================
std::span<const Engine::AssetDocumentSaveKind> Engine::AssetDocumentRecovery::GetSaveKinds() {

	return kSaveKinds;
}

Engine::JsonFileJournal::Scope Engine::AssetDocumentRecovery::MakeScope(AssetDocumentSaveKind kind) {

	const auto gameRoot = RuntimePaths::GetGameAssetsRoot();
	const auto engineRoot = RuntimePaths::GetEngineAssetsRoot();
	const char* folder = "ShaderGraphAssetRecovery";
	switch (kind) {
	case AssetDocumentSaveKind::Material: folder = "MaterialAssetRecovery"; break;
	case AssetDocumentSaveKind::Prefab: folder = "PrefabAssetRecovery"; break;
	case AssetDocumentSaveKind::Font: folder = "FontAssetRecovery"; break;
	case AssetDocumentSaveKind::GeneratedRender: folder = "GeneratedRenderAssetRecovery"; break;
	case AssetDocumentSaveKind::ShaderGraph: break;
	}
	return {RuntimePaths::GetSavedRoot() / folder,
		[gameRoot, engineRoot](const std::filesystem::path& path) {
			return RuntimePaths::CanWritePath(path) && (StorageFileUtility::IsInside(path, gameRoot) || StorageFileUtility::IsInside(path, engineRoot));
		}};
}

bool Engine::AssetDocumentRecovery::RecoverPending(std::vector<AssetDatabaseIssue>& issues) {

	bool recovered = true;
	for (const AssetDocumentSaveKind kind : GetSaveKinds()) {
		const auto scope = MakeScope(kind);
		try {
			for (const auto& directory : JsonFileJournal::GetRecoveries(scope, true)) {
				std::string diagnostic;
				// 退避内容と外部変更を照合してから復旧
				if (JsonFileJournal::Recover(scope, directory, diagnostic, {})) {
					continue;
				}
				recovered = false;
				AssetDatabaseIssue issue;
				issue.type = AssetDatabaseIssueType::UnfinishedAssetSave;
				issue.assetPath = "未完了のAsset保存";
				issue.relatedPath = Algorithm::PathToUTF8(directory);
				issue.detail = std::move(diagnostic);
				issues.emplace_back(std::move(issue));
			}
		} catch (const std::exception& error) {
			recovered = false;
			AssetDatabaseIssue issue;
			issue.type = AssetDatabaseIssueType::UnfinishedAssetSave;
			issue.assetPath = "Asset保存の復旧記録を確認できません";
			issue.relatedPath = Algorithm::PathToUTF8(scope.recoveryRoot);
			issue.detail = error.what();
			issues.emplace_back(std::move(issue));
		}
	}
	return recovered;
}

bool Engine::AssetDocumentRecovery::KeepCurrent(const std::filesystem::path& directory, std::string& diagnostic) {

	// 許可された復旧記録だけを明示確定
	for (const AssetDocumentSaveKind kind : GetSaveKinds()) {
		const auto scope = MakeScope(kind);
		if (StorageFileUtility::IsInside(directory, scope.recoveryRoot)) {
			return JsonFileJournal::KeepCurrent(scope, directory, diagnostic);
		}
	}
	diagnostic = "対象はAsset保存の復旧記録ではありません";
	return false;
}
