#pragma once

#include <Engine/Core/Assets/AssetTypes.h>
#include <filesystem>
#include <vector>

namespace Engine {
	class AssetDatabase;
}

namespace Engine::PublishedAssetChanges {

	// エディターで生成したファイルとmetaを登録する
	AssetID Import(AssetDatabase& database, const std::string& path, AssetType type);
	bool Matches(const AssetDatabase& database, const std::filesystem::path& path);
	bool CollectAdditions(const AssetDatabase& database, uint64_t previous, std::vector<AssetID>& additions);
}
