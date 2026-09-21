#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetMetadata.h>

// c++
#include <filesystem>
#include <string_view>

namespace Engine::AssetMetaStorage {

	std::string_view ResolveImporterName(Engine::AssetType type);

	std::filesystem::path MetaPathOf(const std::filesystem::path& assetFullPath);

	bool ReadMetaFile(const std::filesystem::path& metaFullPath, AssetMeta& out);

	bool WriteMetaFile(const std::filesystem::path& metaFullPath, const AssetMeta& meta);
}
