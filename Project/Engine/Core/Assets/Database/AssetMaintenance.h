#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>

namespace Engine::AssetMaintenance {

	// 旧ペイント素材をGUIDを保ったまま標準マテリアルへ移す
	std::filesystem::path MigratePaintMaterial(const std::filesystem::path& source);

	// 欠損したFont参照と隣接Atlasの候補を検出する
	void DetectFontAtlasReferences(const AssetDatabase& database, std::vector<AssetDatabaseIssue>& issues);
	// 元Assetが存在しないmetaを診断へ追加する
	void DetectOrphanMeta(const std::vector<std::filesystem::path>& scanRoots, std::vector<AssetDatabaseIssue>& issues);
	// 対象を再検査してFont参照を置換する
	bool RepairFontAtlas(const AssetDatabase& database, AssetID fontID, AssetID atlasID);
}
