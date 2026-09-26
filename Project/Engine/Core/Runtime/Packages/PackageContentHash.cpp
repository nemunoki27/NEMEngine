#include "PackageResolution.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <algorithm>
#include <array>
#include <fstream>

namespace Engine::PackageDetail {

	constexpr uint64_t kFnvOffset = 1469598103934665603ull;
	constexpr uint64_t kFnvPrime = 1099511628211ull;

	uint64_t HashBytes(uint64_t hash, const void* data, size_t size) {

		const auto* bytes = static_cast<const uint8_t*>(data);
		for (size_t i = 0; i < size; ++i) {
			hash ^= bytes[i];
			hash *= kFnvPrime;
		}
		return hash;
	}

	std::optional<uint64_t> ComputePackageHash(const std::filesystem::path& root) {

		std::vector<std::filesystem::path> files;
		std::error_code ec;
		for (auto it = std::filesystem::recursive_directory_iterator(
			root, std::filesystem::directory_options::none, ec);
			!ec && it != std::filesystem::recursive_directory_iterator{}; it.increment(ec)) {

			if (it->is_regular_file(ec)) {
				files.emplace_back(it->path());
			}
			if (ec) {
				return std::nullopt;
			}
		}
		// 列挙に失敗した内容を正常なhashとして公開しない
		if (ec) {
			return std::nullopt;
		}
		std::sort(files.begin(), files.end(), [&root](const auto& lhs, const auto& rhs) {
				return lhs.lexically_relative(root).generic_wstring() <
				rhs.lexically_relative(root).generic_wstring();
		});

		uint64_t hash = kFnvOffset;
		std::array<char, 64 * 1024> buffer{};
		for (const std::filesystem::path& path : files) {

			const std::string relative = Engine::Algorithm::PathToUTF8(path.lexically_relative(root));
			hash = HashBytes(hash, relative.data(), relative.size());

			std::ifstream file(path, std::ios::binary);
			if (!file.is_open()) {
				return std::nullopt;
			}
			while (file) {
				file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
				hash = HashBytes(hash, buffer.data(), static_cast<size_t>(file.gcount()));
			}
			if (file.bad() || !file.eof()) {
				return std::nullopt;
			}
		}
		return hash;
	}
}
