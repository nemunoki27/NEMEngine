#include "PackageResolution.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace Engine::PackageDetail {

	constexpr uint32_t kPackageLockSchemaVersion = 1;

	std::string ToHex(uint64_t value) {

		std::ostringstream stream;
		stream << std::hex << std::setfill('0') << std::setw(16) << value;
		return stream.str();
	}

	bool SaveLockFile(const std::filesystem::path& path,
		const std::filesystem::path& projectRoot,
		const std::vector<Engine::ResolvedPackage>& packages) {

		nlohmann::json dependencies = nlohmann::json::object();
		for (const Engine::ResolvedPackage& package : packages) {

			dependencies[package.name] = {
				{ "version", package.version },
				{ "source", package.source },
				{ "path", Engine::Algorithm::PathToUTF8(package.root.lexically_relative(projectRoot)) },
				{ "contentHash", ToHex(package.contentHash) },
			};
		}
		const nlohmann::json lock = {
			{ "schemaVersion", kPackageLockSchemaVersion },
			{ "dependencies", std::move(dependencies) },
		};
		const std::string serialized = lock.dump(2) + '\n';

		std::ifstream currentFile(path, std::ios::binary);
		const std::string current((std::istreambuf_iterator<char>(currentFile)),
			std::istreambuf_iterator<char>());
		if (current == serialized) {
			return true;
		}

		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file.is_open()) {
			return false;
		}
		file << serialized;
		return file.good();
	}
}
