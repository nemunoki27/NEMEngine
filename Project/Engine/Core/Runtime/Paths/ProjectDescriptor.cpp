#include "RuntimePathResolution.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <Engine/Core/Foundation/Utility/Algorithm/StringUtility.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <json.hpp>

namespace Engine::RuntimePathDetail {

	std::filesystem::path FindProjectDescriptorIn(const std::filesystem::path& directory) {

		std::error_code ec;
		if (!ExistsDirectory(directory)) {
			return {};
		}

		std::vector<std::filesystem::path> candidates;
		for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {

			if (ec) {
				break;
			}
			if (entry.is_regular_file(ec) && entry.path().extension() == ".nemproject") {
				candidates.emplace_back(entry.path());
			}
		}
		if (candidates.empty()) {
			return {};
		}
		std::sort(candidates.begin(), candidates.end());
		return NormalizePath(candidates.front());
	}

	std::filesystem::path FindProjectDescriptor(const std::filesystem::path& start) {

		for (std::filesystem::path current = start; !current.empty(); current = current.parent_path()) {

			if (std::filesystem::path descriptor = FindProjectDescriptorIn(current); !descriptor.empty()) {
				return descriptor;
			}
			if (current == current.parent_path()) {
				break;
			}
		}

		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator(start, ec)) {

			if (ec) {
				break;
			}
			if (!entry.is_directory(ec)) {
				continue;
			}
			if (std::filesystem::path descriptor = FindProjectDescriptorIn(entry.path()); !descriptor.empty()) {
				return descriptor;
			}
		}
		return {};
	}

	std::filesystem::path FindNamedProjectDescriptor(
		const std::filesystem::path& start, const std::wstring& projectName) {

		if (start.empty() || projectName.empty()) {
			return {};
		}
		const std::filesystem::path fileName = projectName + L".nemproject";
		for (std::filesystem::path current = start; !current.empty(); current = current.parent_path()) {

			const std::array candidates{
				current / fileName,
				current / projectName / fileName,
				current / "Project" / projectName / fileName,
			};
			for (const std::filesystem::path& candidate : candidates) {

				std::error_code ec;
				if (std::filesystem::is_regular_file(candidate, ec)) {
					return NormalizePath(candidate);
				}
			}
			if (current == current.parent_path()) {
				break;
			}
		}
		return {};
	}

	bool LoadProjectDescriptor(const std::filesystem::path& descriptorPath,
		std::string& outGUID,
		std::string& outName, std::filesystem::path& outAssetsDirectory,
		std::filesystem::path& outPackagesDirectory,
		std::filesystem::path& outProjectSettingsDirectory,
		Engine::SceneStorageMode& outSceneStorageMode) {

		if (descriptorPath.empty()) {
			return false;
		}

		std::ifstream file(descriptorPath, std::ios::binary);
		const nlohmann::json data = nlohmann::json::parse(file, nullptr, false);
		if (!data.is_object() || data.value("schemaVersion", 0u) != 1u) {
			return false;
		}

		const std::string guid = data.value("projectGuid", std::string{});
		if (guid.size() != 32 || !std::all_of(guid.begin(), guid.end(), [](unsigned char c) {
				return std::isxdigit(c) != 0;
		})) {
			return false;
		}
		outGUID = Engine::Algorithm::ToLower(guid);
		outName = data.value("name", std::string{});
		if (outName.empty()) {
			return false;
		}

		const std::string sceneStorage =
		data.value("sceneStorage", std::string("ExternalActors"));
		if (sceneStorage == "Monolithic") {
			outSceneStorageMode = Engine::SceneStorageMode::Monolithic;
		} else if (sceneStorage == "ExternalActors") {
			outSceneStorageMode = Engine::SceneStorageMode::ExternalActors;
		} else {
			return false;
		}

		const std::filesystem::path assetsDirectory =
		Engine::Algorithm::PathFromUTF8(data.value("assetsDirectory", std::string{}));
		if (assetsDirectory.empty() || assetsDirectory.is_absolute() || !IsChildPath(assetsDirectory)) {
			return false;
		}
		outAssetsDirectory = assetsDirectory;

		const std::filesystem::path packagesDirectory =
		Engine::Algorithm::PathFromUTF8(data.value("packagesDirectory", std::string{}));
		if (packagesDirectory.empty() || packagesDirectory.is_absolute() || !IsChildPath(packagesDirectory)) {
			return false;
		}
		outPackagesDirectory = packagesDirectory;

		const std::filesystem::path projectSettingsDirectory =
		Engine::Algorithm::PathFromUTF8(data.value("projectSettingsDirectory", std::string{}));
		if (projectSettingsDirectory.empty() || projectSettingsDirectory.is_absolute() ||
			!IsChildPath(projectSettingsDirectory)) {
			return false;
		}
		outProjectSettingsDirectory = projectSettingsDirectory;
		return true;
	}
}
