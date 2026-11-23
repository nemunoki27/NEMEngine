#pragma once

#include <Engine/Core/Assets/Database/AssetMetadata.h>
#include <filesystem>

namespace Engine::ProductAssetCook {

	bool Convert(const std::filesystem::path& source, const AssetMeta& meta,
		std::vector<uint8_t>& bytes, std::string& error);

	bool TryTextureSource(const std::filesystem::path& source, const AssetMeta& meta,
		const std::vector<uint8_t>& cooked, size_t limit, std::vector<uint8_t>& payload);
	bool RestoreTextureSource(const std::vector<uint8_t>& payload, size_t expectedSize,
		std::vector<uint8_t>& cooked);
}
