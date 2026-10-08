#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>

namespace Engine::AssetDependencyResolver {

	// 索引を参照して依存先と診断を抽出する
	std::vector<AssetID> ExtractDependencies(const AssetDatabase& database, const AssetMeta& meta,
		std::vector<AssetDatabaseIssue>& issues);
}
