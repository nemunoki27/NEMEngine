#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>

namespace Engine::AssetMaintenance {

	// 隣接アトラスの参照を補修する
	void ReconcileFontAtlasReferences(const AssetDatabase& database);
	// 孤立metaを走査して既存の削除処理を実行する
	void DetectOrphanMeta(const std::vector<std::filesystem::path>& scanRoots);
}
