#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetMetadata.h>
#include <array>
#include <optional>
#include <filesystem>

namespace Engine::ProductContentPack {

	inline constexpr std::array<char, 8> kCompressedMagic{ 'N', 'E', 'M', 'P', 'A', 'K', '0', '2' };

	struct Header {

		std::array<char, 8> magic{ 'N', 'E', 'M', 'P', 'A', 'K', '0', '1' };
		uint64_t indexOffset = 0;
		std::array<char, 64> indexHash{};
	};
	static_assert(sizeof(Header) == 80);

	bool Mount(const std::filesystem::path& path, std::string& error);
	bool Verify(const std::filesystem::path& path, std::string& error, const std::string& expectedGuid = {});
	bool IsMounted();
	std::filesystem::path Root();
	std::string ProjectGuid();
	const std::vector<AssetMeta>& Assets();

	bool Contains(const std::filesystem::path& path);
	std::optional<uint64_t> Size(const std::filesystem::path& path);
	bool Read(const std::filesystem::path& path, std::vector<uint8_t>& bytes);
	bool ReadJson(const std::filesystem::path& path, nlohmann::json& value);
}
