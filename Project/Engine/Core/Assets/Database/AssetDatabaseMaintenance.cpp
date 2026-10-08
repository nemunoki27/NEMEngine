#include "AssetDatabase.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetMaintenance.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <algorithm>

bool Engine::AssetDatabase::DeleteOrphanMeta(const std::filesystem::path& metaPath) {

	// 検出済みの対象だけを操作し、復活したAssetのmetaを保護する
	const auto found = std::find_if(issues_.begin(), issues_.end(), [&](const auto& issue) {
		return issue.type == AssetDatabaseIssueType::OrphanMeta &&
			Algorithm::PathFromUTF8(issue.relatedPath).lexically_normal() == metaPath.lexically_normal();
	});
	if (found == issues_.end() || metaPath.extension() != L".meta") {
		return false;
	}
	std::error_code error;
	auto assetPath = metaPath;
	assetPath.replace_extension();
	if (std::filesystem::exists(assetPath, error) || error) {
		return false;
	}
	if (!std::filesystem::remove(metaPath, error) || error) {
		return false;
	}
	Logger::Output(LogType::Engine, "[AssetDatabase] 孤立metaを削除しました path={}", Algorithm::PathToUTF8(metaPath));
	issues_.erase(found);
	return true;
}

bool Engine::AssetDatabase::RepairFontAtlas(AssetID fontID, AssetID atlasID, std::string* diagnostic) {

	if (diagnostic) {
		diagnostic->clear();
	}
	if (!AssetMaintenance::RepairFontAtlas(*this, fontID, atlasID)) {
		return false;
	}
	// 書込成功後に参照と診断を更新する
	const bool indexed = RefreshDependencies(fontID);
	std::erase_if(issues_, [fontID](const auto& issue) {
		return issue.assetID == fontID && issue.type == AssetDatabaseIssueType::FontAtlasRepair;
	});
	++structureRevision_;
	if (!indexed && diagnostic) {
		*diagnostic = "Atlas参照は保存しましたが索引の更新に失敗しました 再検査してください";
	}
	return true;
}
